#!/usr/bin/env python3
"""Build a QGroundControl offline tile database from a GeoServer WMS layer.

The converter requests Web Mercator images aligned to the XYZ tile grid in
blocks, splits each response into 256x256 PNG tiles, and streams the tiles into
QGC's qgctiledb SQLite schema. Completed blocks are recorded so interrupted
runs can be resumed safely.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import io
import itertools
import json
import math
import os
import shutil
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence

from PIL import Image, UnidentifiedImageError


TILE_SIZE = 256
WEB_MERCATOR_LIMIT = 20037508.342789244
DEFAULT_MAP_TYPE = "Geoserver Map"
CONVERTER_VERSION = "3"
MANIFEST_SCHEMA_VERSION = 1
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# Verified against the Qt 5.15.2 qHash(QString) used by this QGC tree. A custom
# map type or a QGC build on a different Qt major version must provide its ID.
DEFAULT_MAP_TYPE_IDS = {DEFAULT_MAP_TYPE: 685018728}


@dataclass(frozen=True)
class GeoBounds:
    west: float
    south: float
    east: float
    north: float

    def validate(self) -> None:
        if not (-180.0 <= self.west < self.east <= 180.0):
            raise ValueError("bounds must satisfy -180 <= west < east <= 180")
        if not (-85.05112878 <= self.south < self.north <= 85.05112878):
            raise ValueError(
                "bounds must stay inside Web Mercator latitude limits and satisfy south < north"
            )


@dataclass(frozen=True)
class LayerRule:
    min_zoom: int
    max_zoom: int
    layers: tuple[str, ...]

    def applies(self, zoom: int) -> bool:
        return self.min_zoom <= zoom <= self.max_zoom


@dataclass(frozen=True)
class TileBlock:
    zoom: int
    x0: int
    y0: int
    x1: int
    y1: int
    layers: tuple[str, ...]

    @property
    def key(self) -> str:
        return f"{self.zoom}/{self.x0}/{self.y0}/{self.x1}/{self.y1}"

    @property
    def width(self) -> int:
        return self.x1 - self.x0 + 1

    @property
    def height(self) -> int:
        return self.y1 - self.y0 + 1


@dataclass(frozen=True)
class RenderedBlock:
    block: TileBlock
    tiles: tuple[tuple[int, int, bytes], ...]
    response_bytes: int


@dataclass(frozen=True)
class ConversionSettings:
    wms_url: str
    output: Path
    bounds: GeoBounds
    min_zoom: int
    max_zoom: int
    layer_rules: tuple[LayerRule, ...]
    map_type: str
    map_type_id: int
    set_name: str
    block_tiles: int
    workers: int
    timeout: float
    retries: int
    wms_format: str
    png_compress_level: int
    skip_transparent: bool
    zoom_areas: tuple[tuple[int, GeoBounds], ...] = ()
    source_plan_sha256: str | None = None

    def validate(self) -> None:
        self.bounds.validate()
        if not self.wms_url.lower().startswith(("http://", "https://")):
            raise ValueError("wms_url must be an http or https URL")
        if not (0 <= self.min_zoom <= self.max_zoom <= 30):
            raise ValueError("zoom range must satisfy 0 <= min_zoom <= max_zoom <= 30")
        if not (1 <= self.block_tiles <= 16):
            raise ValueError("block_tiles must be between 1 and 16")
        if not (1 <= self.workers <= 32):
            raise ValueError("workers must be between 1 and 32")
        if self.timeout <= 0 or self.retries < 0:
            raise ValueError("timeout must be positive and retries cannot be negative")
        if not (0 <= self.png_compress_level <= 9):
            raise ValueError("png_compress_level must be between 0 and 9")
        if not (0 <= self.map_type_id <= 2_147_483_647):
            raise ValueError("map_type_id must fit QGC's signed 32-bit provider ID")
        for zoom in range(self.min_zoom, self.max_zoom + 1):
            if not layers_for_zoom(self.layer_rules, zoom):
                raise ValueError(f"no layer rule applies at zoom {zoom}")
        for zoom, bounds in self.zoom_areas:
            if not self.min_zoom <= zoom <= self.max_zoom:
                raise ValueError(f"zoom area z{zoom} is outside the configured zoom range")
            bounds.validate()


def parse_bounds(value: str | Sequence[float]) -> GeoBounds:
    if isinstance(value, str):
        parts = [part.strip() for part in value.split(",")]
    else:
        parts = list(value)
    if len(parts) != 4:
        raise ValueError("bounds must contain west,south,east,north")
    return GeoBounds(*(float(part) for part in parts))


def parse_layer_rule(value: str | dict[str, object]) -> LayerRule:
    if isinstance(value, dict):
        min_zoom = int(value["min_zoom"])
        max_zoom = int(value.get("max_zoom", min_zoom))
        layers_value = value["layers"]
        if not isinstance(layers_value, list):
            raise ValueError("layer rule 'layers' must be a JSON list")
        layers = tuple(str(layer).strip() for layer in layers_value if str(layer).strip())
    else:
        try:
            zoom_text, layers_text = value.split("=", 1)
        except ValueError as error:
            raise ValueError("layer rule must look like 0-12=workspace:layer") from error
        if "-" in zoom_text:
            min_text, max_text = zoom_text.split("-", 1)
            min_zoom, max_zoom = int(min_text), int(max_text)
        else:
            min_zoom = max_zoom = int(zoom_text)
        layers = tuple(layer.strip() for layer in layers_text.split(",") if layer.strip())
    if min_zoom > max_zoom or not layers:
        raise ValueError("layer rule has an invalid zoom range or no layers")
    return LayerRule(min_zoom, max_zoom, layers)


def layers_for_zoom(rules: Sequence[LayerRule], zoom: int) -> tuple[str, ...]:
    layers: list[str] = []
    for rule in rules:
        if rule.applies(zoom):
            for layer in rule.layers:
                if layer not in layers:
                    layers.append(layer)
    return tuple(layers)


def lon_to_tile_x(lon: float, zoom: int) -> int:
    count = 1 << zoom
    return min(count - 1, max(0, int(math.floor((lon + 180.0) / 360.0 * count))))


def lat_to_tile_y(lat: float, zoom: int) -> int:
    count = 1 << zoom
    lat_radians = math.radians(lat)
    value = (1.0 - math.asinh(math.tan(lat_radians)) / math.pi) / 2.0 * count
    return min(count - 1, max(0, int(math.floor(value))))


def tile_range(bounds: GeoBounds, zoom: int) -> tuple[int, int, int, int]:
    return (
        lon_to_tile_x(bounds.west, zoom),
        lat_to_tile_y(bounds.north, zoom),
        lon_to_tile_x(bounds.east, zoom),
        lat_to_tile_y(bounds.south, zoom),
    )


def tile_to_mercator_x(x: int, zoom: int) -> float:
    return -WEB_MERCATOR_LIMIT + (2.0 * WEB_MERCATOR_LIMIT * x / (1 << zoom))


def tile_to_mercator_y(y: int, zoom: int) -> float:
    return WEB_MERCATOR_LIMIT - (2.0 * WEB_MERCATOR_LIMIT * y / (1 << zoom))


def _intersect_bounds(left: GeoBounds, right: GeoBounds) -> GeoBounds | None:
    west = max(left.west, right.west)
    south = max(left.south, right.south)
    east = min(left.east, right.east)
    north = min(left.north, right.north)
    if west >= east or south >= north:
        return None
    return GeoBounds(west, south, east, north)


def areas_for_zoom(settings: ConversionSettings, zoom: int) -> tuple[GeoBounds, ...] | None:
    areas = tuple(bounds for area_zoom, bounds in settings.zoom_areas if area_zoom == zoom)
    return areas or None


def iter_blocks_for_zoom(settings: ConversionSettings, zoom: int) -> Iterator[TileBlock]:
    package_x0, package_y0, package_x1, package_y1 = tile_range(settings.bounds, zoom)
    layers = layers_for_zoom(settings.layer_rules, zoom)
    areas = areas_for_zoom(settings, zoom)
    if areas is None:
        for block_y in range(package_y0, package_y1 + 1, settings.block_tiles):
            for block_x in range(package_x0, package_x1 + 1, settings.block_tiles):
                yield TileBlock(
                    zoom,
                    block_x,
                    block_y,
                    min(package_x1, block_x + settings.block_tiles - 1),
                    min(package_y1, block_y + settings.block_tiles - 1),
                    layers,
                )
        return

    # Sparse high zooms are aligned to the package's ordinary block grid. A
    # source footprint may touch only part of an 8x8 block; rendering the small
    # fringe is intentional and avoids hundreds of tiny WMS requests.
    selected_blocks: set[tuple[int, int]] = set()
    for area in areas:
        clipped = _intersect_bounds(settings.bounds, area)
        if clipped is None:
            continue
        area_x0, area_y0, area_x1, area_y1 = tile_range(clipped, zoom)
        block_column0 = (area_x0 - package_x0) // settings.block_tiles
        block_column1 = (area_x1 - package_x0) // settings.block_tiles
        block_row0 = (area_y0 - package_y0) // settings.block_tiles
        block_row1 = (area_y1 - package_y0) // settings.block_tiles
        for block_row in range(block_row0, block_row1 + 1):
            for block_column in range(block_column0, block_column1 + 1):
                selected_blocks.add((block_column, block_row))

    for block_column, block_row in sorted(selected_blocks, key=lambda value: (value[1], value[0])):
        block_x = package_x0 + block_column * settings.block_tiles
        block_y = package_y0 + block_row * settings.block_tiles
        yield TileBlock(
            zoom,
            block_x,
            block_y,
            min(package_x1, block_x + settings.block_tiles - 1),
            min(package_y1, block_y + settings.block_tiles - 1),
            layers,
        )


def iter_blocks(settings: ConversionSettings) -> Iterator[TileBlock]:
    for zoom in range(settings.min_zoom, settings.max_zoom + 1):
        yield from iter_blocks_for_zoom(settings, zoom)


def plan_rows(settings: ConversionSettings) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for zoom in range(settings.min_zoom, settings.max_zoom + 1):
        blocks = list(iter_blocks_for_zoom(settings, zoom))
        rows.append(
            {
                "zoom": zoom,
                "candidate_tiles": sum(block.width * block.height for block in blocks),
                "blocks": len(blocks),
                "layers": layers_for_zoom(settings.layer_rules, zoom),
                "sparse": areas_for_zoom(settings, zoom) is not None,
            }
        )
    return rows


def print_plan(settings: ConversionSettings) -> None:
    rows = plan_rows(settings)
    candidate_tiles = sum(int(row["candidate_tiles"]) for row in rows)
    print("Zoom  Candidate tiles  WMS blocks  Layers")
    for row in rows:
        print(
            f"{row['zoom']:>4}  {row['candidate_tiles']:>15,}  "
            f"{row['blocks']:>10,}  {'sparse ' if row['sparse'] else ''}"
            f"{', '.join(row['layers'])}"
        )
    print(
        f"TOTAL {candidate_tiles:>15,}  "
        f"{sum(int(row['blocks']) for row in rows):>10,}"
    )
    if settings.skip_transparent:
        print("Candidate tiles are an upper bound; fully transparent tiles are omitted.")
    else:
        print("Transparent tiles are retained, so every candidate tile will be stored.")
    if settings.png_compress_level == 0:
        sample = Image.new("RGBA", (TILE_SIZE, TILE_SIZE), (0, 0, 0, 0))
        encoded = io.BytesIO()
        sample.save(encoded, format="PNG", compress_level=0)
        estimated_bytes = candidate_tiles * len(encoded.getvalue())
        print(
            "Estimated uncompressed RGBA tile payload: "
            f"{estimated_bytes / 1_000_000_000:.2f} GB "
            f"({estimated_bytes / (1024**3):.2f} GiB), before SQLite overhead."
        )


def build_wms_url(settings: ConversionSettings, block: TileBlock) -> str:
    min_x = tile_to_mercator_x(block.x0, block.zoom)
    max_x = tile_to_mercator_x(block.x1 + 1, block.zoom)
    max_y = tile_to_mercator_y(block.y0, block.zoom)
    min_y = tile_to_mercator_y(block.y1 + 1, block.zoom)
    params = {
        "SERVICE": "WMS",
        "VERSION": "1.1.1",
        "REQUEST": "GetMap",
        "LAYERS": ",".join(block.layers),
        "STYLES": "",
        "SRS": "EPSG:900913",
        "BBOX": f"{min_x:.10f},{min_y:.10f},{max_x:.10f},{max_y:.10f}",
        "WIDTH": str(block.width * TILE_SIZE),
        "HEIGHT": str(block.height * TILE_SIZE),
        "FORMAT": settings.wms_format,
        "TRANSPARENT": "TRUE",
        "EXCEPTIONS": "application/vnd.ogc.se_xml",
    }
    separator = "&" if "?" in settings.wms_url else "?"
    return settings.wms_url + separator + urllib.parse.urlencode(params)


def _http_get(url: str, timeout: float) -> tuple[bytes, str]:
    request = urllib.request.Request(url, headers={"User-Agent": "QGC GeoServer converter/1"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read(), response.headers.get_content_type()


def _is_transparent(tile: Image.Image) -> bool:
    if "A" not in tile.getbands() and "transparency" not in tile.info:
        return False
    return tile.convert("RGBA").getchannel("A").getbbox() is None


def split_block_png(
    block: TileBlock,
    payload: bytes,
    skip_transparent: bool,
    png_compress_level: int = 6,
) -> tuple[tuple[int, int, bytes], ...]:
    expected_size = (block.width * TILE_SIZE, block.height * TILE_SIZE)
    try:
        with Image.open(io.BytesIO(payload)) as image:
            image.load()
            if image.size != expected_size:
                raise RuntimeError(
                    f"GeoServer returned {image.size[0]}x{image.size[1]} for block "
                    f"{block.key}; expected {expected_size[0]}x{expected_size[1]}"
                )
            output: list[tuple[int, int, bytes]] = []
            for row in range(block.height):
                for column in range(block.width):
                    tile = image.crop(
                        (
                            column * TILE_SIZE,
                            row * TILE_SIZE,
                            (column + 1) * TILE_SIZE,
                            (row + 1) * TILE_SIZE,
                        )
                    )
                    if "transparency" in image.info:
                        tile.info["transparency"] = image.info["transparency"]
                    if skip_transparent and _is_transparent(tile):
                        continue
                    encoded = io.BytesIO()
                    tile.save(
                        encoded,
                        format="PNG",
                        compress_level=png_compress_level,
                    )
                    output.append((block.x0 + column, block.y0 + row, encoded.getvalue()))
            return tuple(output)
    except UnidentifiedImageError as error:
        preview = payload[:300].decode("utf-8", errors="replace")
        raise RuntimeError(f"GeoServer did not return an image for {block.key}: {preview}") from error


def render_block(settings: ConversionSettings, block: TileBlock) -> RenderedBlock:
    url = build_wms_url(settings, block)
    last_error: Exception | None = None
    for attempt in range(settings.retries + 1):
        try:
            payload, content_type = _http_get(url, settings.timeout)
            if content_type in {"text/xml", "application/xml", "text/html"}:
                preview = payload[:1000].decode("utf-8", errors="replace")
                raise RuntimeError(f"GeoServer error for {block.key}: {preview}")
            tiles = split_block_png(
                block,
                payload,
                settings.skip_transparent,
                settings.png_compress_level,
            )
            return RenderedBlock(block, tiles, len(payload))
        except (OSError, RuntimeError, urllib.error.URLError) as error:
            last_error = error
            if attempt < settings.retries:
                time.sleep(0.5 * (2**attempt))
    raise RuntimeError(
        f"failed WMS block {block.key} after {settings.retries + 1} attempts: {last_error}"
    )


def qgc_tile_hash(map_type_id: int, x: int, y: int, zoom: int) -> str:
    return f"{map_type_id:010d}{x:08d}{y:08d}{zoom:03d}"


def configuration_signature(settings: ConversionSettings) -> str:
    value = {
        "wms_url": settings.wms_url,
        "bounds": [
            settings.bounds.west,
            settings.bounds.south,
            settings.bounds.east,
            settings.bounds.north,
        ],
        "min_zoom": settings.min_zoom,
        "max_zoom": settings.max_zoom,
        "layer_rules": [
            {
                "min_zoom": rule.min_zoom,
                "max_zoom": rule.max_zoom,
                "layers": list(rule.layers),
            }
            for rule in settings.layer_rules
        ],
        "map_type": settings.map_type,
        "map_type_id": settings.map_type_id,
        "set_name": settings.set_name,
        "block_tiles": settings.block_tiles,
        "wms_format": settings.wms_format,
        "png_compress_level": settings.png_compress_level,
        "skip_transparent": settings.skip_transparent,
        "zoom_areas": [
            {"zoom": zoom, "bounds": [bounds.west, bounds.south, bounds.east, bounds.north]}
            for zoom, bounds in settings.zoom_areas
        ],
        "source_plan_sha256": settings.source_plan_sha256,
    }
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


class QgcTileDatabase:
    def __init__(
        self,
        settings: ConversionSettings,
        resume: bool,
        path: Path | None = None,
        server_capabilities_sha256: str | None = None,
    ) -> None:
        self.settings = settings
        self.path = path or settings.output
        if self.path.exists() and not resume:
            raise FileExistsError(
                f"output already exists: {self.path} (use --resume or choose another file)"
            )
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.path, timeout=60.0)
        try:
            self.connection.execute("PRAGMA journal_mode=WAL")
            self.connection.execute("PRAGMA synchronous=NORMAL")
            self.connection.execute("PRAGMA cache_size=-262144")
            self._create_schema()
            self._validate_configuration()
            if server_capabilities_sha256 is not None:
                self._validate_source(server_capabilities_sha256, resume)
            self.set_id = self._get_or_create_set()
        except Exception:
            self.connection.close()
            raise

    def _create_schema(self) -> None:
        self.connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS Tiles (
                tileID INTEGER PRIMARY KEY NOT NULL,
                hash TEXT NOT NULL UNIQUE,
                format TEXT NOT NULL,
                tile BLOB NULL,
                size INTEGER,
                type INTEGER,
                date INTEGER DEFAULT 0
            );
            CREATE INDEX IF NOT EXISTS hash ON Tiles (hash, size, type);
            CREATE TABLE IF NOT EXISTS TileSets (
                setID INTEGER PRIMARY KEY NOT NULL,
                name TEXT NOT NULL UNIQUE,
                typeStr TEXT,
                topleftLat REAL DEFAULT 0.0,
                topleftLon REAL DEFAULT 0.0,
                bottomRightLat REAL DEFAULT 0.0,
                bottomRightLon REAL DEFAULT 0.0,
                minZoom INTEGER DEFAULT 3,
                maxZoom INTEGER DEFAULT 3,
                type INTEGER DEFAULT -1,
                numTiles INTEGER DEFAULT 0,
                defaultSet INTEGER DEFAULT 0,
                date INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS SetTiles (setID INTEGER, tileID INTEGER);
            CREATE TABLE IF NOT EXISTS TilesDownload (
                setID INTEGER,
                hash TEXT NOT NULL UNIQUE,
                type INTEGER,
                x INTEGER,
                y INTEGER,
                z INTEGER,
                state INTEGER DEFAULT 0
            );
            CREATE TABLE IF NOT EXISTS ConversionBlocks (
                blockKey TEXT PRIMARY KEY NOT NULL,
                tileCount INTEGER NOT NULL,
                responseBytes INTEGER NOT NULL,
                date INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS ConversionMetadata (
                key TEXT PRIMARY KEY NOT NULL,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS ConversionStats (
                setID INTEGER PRIMARY KEY NOT NULL,
                tileBytes INTEGER NOT NULL DEFAULT 0,
                responseBytes INTEGER NOT NULL DEFAULT 0
            );
            """
        )
        self.connection.commit()

    def _configuration_signature(self) -> str:
        return configuration_signature(self.settings)

    def _validate_configuration(self) -> None:
        signature = self._configuration_signature()
        row = self.connection.execute(
            "SELECT value FROM ConversionMetadata WHERE key = 'configuration_sha256'"
        ).fetchone()
        if row and row[0] != signature:
            raise RuntimeError(
                "the existing database was created with different bounds, zooms, layers, "
                "or tile settings; use the original options or a new output file"
            )
        self.connection.execute(
            "INSERT OR IGNORE INTO ConversionMetadata(key, value) VALUES('configuration_sha256', ?)",
            (signature,),
        )
        self.connection.commit()

    def _validate_source(self, server_capabilities_sha256: str, resume: bool) -> None:
        row = self.connection.execute(
            "SELECT value FROM ConversionMetadata "
            "WHERE key = 'server_capabilities_sha256'"
        ).fetchone()
        if row and row[0] != server_capabilities_sha256:
            raise RuntimeError(
                "GeoServer capabilities changed since this conversion started; "
                "do not resume into a database rendered from a different catalog"
            )
        completed_blocks = self.connection.execute(
            "SELECT COUNT(*) FROM ConversionBlocks"
        ).fetchone()[0]
        if not row and resume and completed_blocks:
            raise RuntimeError(
                "this legacy partial database has no GeoServer source fingerprint; "
                "start a new production conversion instead of mixing unknown sources"
            )
        self.connection.execute(
            "INSERT OR IGNORE INTO ConversionMetadata(key, value) "
            "VALUES('server_capabilities_sha256', ?)",
            (server_capabilities_sha256,),
        )
        self.connection.execute(
            "INSERT OR IGNORE INTO ConversionMetadata(key, value) "
            "VALUES('converter_version', ?)",
            (CONVERTER_VERSION,),
        )
        self.connection.commit()

    def _get_or_create_set(self) -> int:
        row = self.connection.execute(
            "SELECT setID, typeStr, type FROM TileSets WHERE name = ?", (self.settings.set_name,)
        ).fetchone()
        if row:
            if row[1] != self.settings.map_type or row[2] != self.settings.map_type_id:
                raise RuntimeError("existing tile set map type does not match this conversion")
            set_id = int(row[0])
            self.connection.execute(
                """
                INSERT OR IGNORE INTO ConversionStats(setID, tileBytes, responseBytes)
                SELECT ?, COALESCE(SUM(Tiles.size), 0), 0
                FROM Tiles JOIN SetTiles USING(tileID) WHERE SetTiles.setID = ?
                """,
                (set_id, set_id),
            )
            self.connection.commit()
            return set_id
        now = int(time.time())
        cursor = self.connection.execute(
            """
            INSERT INTO TileSets(
                name, typeStr, topleftLat, topleftLon, bottomRightLat,
                bottomRightLon, minZoom, maxZoom, type, numTiles, defaultSet, date
            ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, ?)
            """,
            (
                self.settings.set_name,
                self.settings.map_type,
                self.settings.bounds.north,
                self.settings.bounds.west,
                self.settings.bounds.south,
                self.settings.bounds.east,
                self.settings.min_zoom,
                self.settings.max_zoom,
                self.settings.map_type_id,
                now,
            ),
        )
        self.connection.commit()
        set_id = int(cursor.lastrowid)
        self.connection.execute(
            "INSERT OR IGNORE INTO ConversionStats(setID, tileBytes, responseBytes) VALUES(?, 0, 0)",
            (set_id,),
        )
        self.connection.commit()
        return set_id

    def completed_block_keys(self) -> set[str]:
        return {str(row[0]) for row in self.connection.execute("SELECT blockKey FROM ConversionBlocks")}

    def save_block(self, rendered: RenderedBlock) -> tuple[int, int]:
        inserted_tiles = 0
        inserted_bytes = 0
        now = int(time.time())
        with self.connection:
            for x, y, payload in rendered.tiles:
                tile_hash = qgc_tile_hash(
                    self.settings.map_type_id, x, y, rendered.block.zoom
                )
                cursor = self.connection.execute(
                    """
                    INSERT OR IGNORE INTO Tiles(hash, format, tile, size, type, date)
                    VALUES(?, 'png', ?, ?, ?, ?)
                    """,
                    (
                        tile_hash,
                        sqlite3.Binary(payload),
                        len(payload),
                        self.settings.map_type_id,
                        now,
                    ),
                )
                if cursor.rowcount:
                    self.connection.execute(
                        "INSERT INTO SetTiles(tileID, setID) VALUES(?, ?)",
                        (int(cursor.lastrowid), self.set_id),
                    )
                    inserted_tiles += 1
                    inserted_bytes += len(payload)
            self.connection.execute(
                """
                INSERT OR REPLACE INTO ConversionBlocks(blockKey, tileCount, responseBytes, date)
                VALUES(?, ?, ?, ?)
                """,
                (
                    rendered.block.key,
                    inserted_tiles,
                    rendered.response_bytes,
                    now,
                ),
            )
            self.connection.execute(
                "UPDATE TileSets SET numTiles = numTiles + ? WHERE setID = ?",
                (inserted_tiles, self.set_id),
            )
            self.connection.execute(
                """
                INSERT INTO ConversionStats(setID, tileBytes, responseBytes)
                VALUES(?, ?, ?)
                ON CONFLICT(setID) DO UPDATE SET
                    tileBytes = tileBytes + excluded.tileBytes,
                    responseBytes = responseBytes + excluded.responseBytes
                """,
                (self.set_id, inserted_bytes, rendered.response_bytes),
            )
        return inserted_tiles, inserted_bytes

    def counts(self) -> tuple[int, int, int]:
        tiles = self.connection.execute(
            "SELECT numTiles FROM TileSets WHERE setID = ?",
            (self.set_id,),
        ).fetchone()[0]
        stats = self.connection.execute(
            "SELECT tileBytes FROM ConversionStats WHERE setID = ?", (self.set_id,)
        ).fetchone()
        tile_bytes = stats[0] if stats else 0
        blocks = self.connection.execute("SELECT COUNT(*) FROM ConversionBlocks").fetchone()[0]
        return int(blocks), int(tiles), int(tile_bytes)

    def mark_complete(self, planned_blocks: int) -> None:
        completed_blocks = self.connection.execute(
            "SELECT COUNT(*) FROM ConversionBlocks"
        ).fetchone()[0]
        if completed_blocks != planned_blocks:
            raise RuntimeError(
                f"cannot finalize: {completed_blocks:,} of {planned_blocks:,} blocks are complete"
            )
        completed_at = dt.datetime.now(dt.timezone.utc).isoformat()
        with self.connection:
            self.connection.execute(
                "INSERT OR REPLACE INTO ConversionMetadata(key, value) "
                "VALUES('conversion_status', 'complete')"
            )
            self.connection.execute(
                "INSERT OR REPLACE INTO ConversionMetadata(key, value) "
                "VALUES('completed_at_utc', ?)",
                (completed_at,),
            )
            self.connection.execute(
                "INSERT OR REPLACE INTO ConversionMetadata(key, value) "
                "VALUES('planned_blocks', ?)",
                (str(planned_blocks),),
            )

    def close(self) -> None:
        try:
            self.connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
            self.connection.execute("PRAGMA journal_mode=DELETE")
            self.connection.commit()
        finally:
            self.connection.close()

    def __enter__(self) -> "QgcTileDatabase":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


def partial_output_path(output: Path) -> Path:
    return output.with_name(output.name + ".partial")


def manifest_output_path(output: Path) -> Path:
    return output.with_name(output.name + ".manifest.json")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(16 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def available_bytes(path: Path) -> int:
    candidate = path.resolve()
    while not candidate.exists() and candidate != candidate.parent:
        candidate = candidate.parent
    return shutil.disk_usage(candidate).free


def write_progress_file(path: Path | None, **values: object) -> None:
    if path is None:
        return
    try:
        payload: dict[str, object] = {}
        if path.is_file():
            existing = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(existing, dict):
                payload.update(existing)
        payload.update(values)
        payload["updated_at_utc"] = dt.datetime.now(dt.timezone.utc).isoformat()
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary, path)
    except (OSError, ValueError) as error:
        print(f"warning: could not update progress file {path}: {error}", file=sys.stderr)


def verify_database(
    path: Path,
    settings: ConversionSettings,
    server_capabilities_sha256: str,
) -> dict[str, int | str]:
    if not path.is_file():
        raise RuntimeError(f"database does not exist: {path}")
    uri = path.resolve().as_uri() + "?mode=ro"
    connection = sqlite3.connect(uri, uri=True, timeout=60.0)
    try:
        quick_check = [str(row[0]) for row in connection.execute("PRAGMA quick_check")]
        if quick_check != ["ok"]:
            raise RuntimeError("SQLite quick_check failed: " + "; ".join(quick_check[:10]))

        required_tables = {
            "Tiles",
            "TileSets",
            "SetTiles",
            "ConversionBlocks",
            "ConversionMetadata",
        }
        actual_tables = {
            str(row[0])
            for row in connection.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        missing_tables = sorted(required_tables - actual_tables)
        if missing_tables:
            raise RuntimeError("database is missing tables: " + ", ".join(missing_tables))

        metadata = {
            str(key): str(value)
            for key, value in connection.execute("SELECT key, value FROM ConversionMetadata")
        }
        if metadata.get("configuration_sha256") != configuration_signature(settings):
            raise RuntimeError("database configuration fingerprint does not match this config")
        if metadata.get("server_capabilities_sha256") != server_capabilities_sha256:
            raise RuntimeError("database GeoServer fingerprint does not match the current server")

        expected_blocks = {block.key for block in iter_blocks(settings)}
        actual_blocks = {
            str(row[0]) for row in connection.execute("SELECT blockKey FROM ConversionBlocks")
        }
        missing_blocks = expected_blocks - actual_blocks
        unexpected_blocks = actual_blocks - expected_blocks
        if missing_blocks or unexpected_blocks:
            raise RuntimeError(
                "block coverage mismatch: "
                f"{len(missing_blocks):,} missing, {len(unexpected_blocks):,} unexpected"
            )

        tile_set = connection.execute(
            "SELECT setID, numTiles, typeStr, type, minZoom, maxZoom "
            "FROM TileSets WHERE name = ?",
            (settings.set_name,),
        ).fetchone()
        if tile_set is None:
            raise RuntimeError(f"tile set is missing: {settings.set_name}")
        set_id, recorded_tiles, type_string, type_id, min_zoom, max_zoom = tile_set
        if (
            type_string != settings.map_type
            or type_id != settings.map_type_id
            or min_zoom != settings.min_zoom
            or max_zoom != settings.max_zoom
        ):
            raise RuntimeError("tile set metadata does not match this config")

        tile_count, tile_bytes = connection.execute(
            "SELECT COUNT(*), COALESCE(SUM(length(Tiles.tile)), 0) "
            "FROM SetTiles JOIN Tiles USING(tileID) WHERE SetTiles.setID = ?",
            (set_id,),
        ).fetchone()
        if int(recorded_tiles) != int(tile_count):
            raise RuntimeError(
                f"TileSets.numTiles is {recorded_tiles:,}, but {tile_count:,} tiles are linked"
            )
        duplicate_links = connection.execute(
            "SELECT COUNT(*) - COUNT(DISTINCT tileID) FROM SetTiles WHERE setID = ?",
            (set_id,),
        ).fetchone()[0]
        if duplicate_links:
            raise RuntimeError(f"SetTiles contains {duplicate_links:,} duplicate links")
        orphan_links = connection.execute(
            "SELECT COUNT(*) FROM SetTiles LEFT JOIN Tiles USING(tileID) "
            "WHERE setID = ? AND Tiles.tileID IS NULL",
            (set_id,),
        ).fetchone()[0]
        if orphan_links:
            raise RuntimeError(f"SetTiles contains {orphan_links:,} orphan links")
        invalid_tiles = connection.execute(
            "SELECT COUNT(*) FROM SetTiles JOIN Tiles USING(tileID) "
            "WHERE setID = ? AND (tile IS NULL OR size != length(tile) "
            "OR format != 'png' OR substr(tile, 1, 8) != ?)",
            (set_id, sqlite3.Binary(PNG_SIGNATURE)),
        ).fetchone()[0]
        if invalid_tiles:
            raise RuntimeError(f"database contains {invalid_tiles:,} invalid PNG tile records")
        recorded_block_tiles = connection.execute(
            "SELECT COALESCE(SUM(tileCount), 0) FROM ConversionBlocks"
        ).fetchone()[0]
        if int(recorded_block_tiles) != int(tile_count):
            raise RuntimeError(
                "completed-block tile counts do not match the number of linked tiles"
            )

        sample_count = min(int(tile_count), 32)
        for sample_index in range(sample_count):
            offset = sample_index * max(int(tile_count) - 1, 0) // max(sample_count - 1, 1)
            row = connection.execute(
                "SELECT Tiles.tile FROM SetTiles JOIN Tiles USING(tileID) "
                "WHERE SetTiles.setID = ? ORDER BY Tiles.tileID LIMIT 1 OFFSET ?",
                (set_id, offset),
            ).fetchone()
            if row is None:
                raise RuntimeError("could not load a verification tile sample")
            try:
                with Image.open(io.BytesIO(row[0])) as image:
                    image.verify()
                    if image.size != (TILE_SIZE, TILE_SIZE):
                        raise RuntimeError(
                            f"tile sample has size {image.size}; expected {TILE_SIZE}x{TILE_SIZE}"
                        )
            except (OSError, UnidentifiedImageError) as error:
                raise RuntimeError(f"tile sample could not be decoded: {error}") from error

        return {
            "planned_blocks": len(expected_blocks),
            "completed_blocks": len(actual_blocks),
            "tile_count": int(tile_count),
            "tile_bytes": int(tile_bytes),
            "sqlite_quick_check": "ok",
            "decoded_tile_samples": sample_count,
        }
    finally:
        connection.close()


def finalize_database(
    working_path: Path,
    settings: ConversionSettings,
    server_capabilities_sha256: str,
) -> dict[str, object]:
    print("Verifying completed database...", flush=True)
    verification = verify_database(working_path, settings, server_capabilities_sha256)
    print("Computing release SHA-256...", flush=True)
    database_hash = file_sha256(working_path)
    completed_at = dt.datetime.now(dt.timezone.utc).isoformat()
    manifest: dict[str, object] = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "converter_version": CONVERTER_VERSION,
        "status": "complete",
        "database_file": settings.output.name,
        "database_size_bytes": working_path.stat().st_size,
        "database_sha256": database_hash,
        "completed_at_utc": completed_at,
        "source": {
            "wms_url": settings.wms_url,
            "server_capabilities_sha256": server_capabilities_sha256,
            "source_plan_sha256": settings.source_plan_sha256,
        },
        "tile_set": {
            "name": settings.set_name,
            "map_type": settings.map_type,
            "map_type_id": settings.map_type_id,
            "bounds": [
                settings.bounds.west,
                settings.bounds.south,
                settings.bounds.east,
                settings.bounds.north,
            ],
            "min_zoom": settings.min_zoom,
            "max_zoom": settings.max_zoom,
            "layer_rules": [
                {
                    "min_zoom": rule.min_zoom,
                    "max_zoom": rule.max_zoom,
                    "layers": list(rule.layers),
                }
                for rule in settings.layer_rules
            ],
            "zoom_areas": [
                {
                    "zoom": zoom,
                    "bounds": [bounds.west, bounds.south, bounds.east, bounds.north],
                }
                for zoom, bounds in settings.zoom_areas
            ],
        },
        "verification": verification,
    }
    manifest_path = manifest_output_path(settings.output)
    temporary_manifest = manifest_path.with_name(manifest_path.name + ".partial")
    temporary_manifest.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if working_path != settings.output:
        os.replace(working_path, settings.output)
    os.replace(temporary_manifest, manifest_path)
    return manifest


def capability_document(wms_url: str, timeout: float) -> bytes:
    params = urllib.parse.urlencode(
        {"SERVICE": "WMS", "VERSION": "1.1.1", "REQUEST": "GetCapabilities"}
    )
    separator = "&" if "?" in wms_url else "?"
    payload, _ = _http_get(wms_url + separator + params, timeout)
    return payload


def capability_layer_names(wms_url: str, timeout: float) -> set[str]:
    payload = capability_document(wms_url, timeout)
    root = ET.fromstring(payload)
    return {
        (element.text or "").strip()
        for element in root.iter()
        if element.tag.rsplit("}", 1)[-1] == "Name" and (element.text or "").strip()
    }


def validate_server(settings: ConversionSettings) -> str:
    capabilities = capability_document(settings.wms_url, settings.timeout)
    root = ET.fromstring(capabilities)
    available = {
        (element.text or "").strip()
        for element in root.iter()
        if element.tag.rsplit("}", 1)[-1] == "Name" and (element.text or "").strip()
    }
    required = {
        layer
        for zoom in range(settings.min_zoom, settings.max_zoom + 1)
        for layer in layers_for_zoom(settings.layer_rules, zoom)
    }
    missing = sorted(required - available)
    if missing:
        # GeoServer does not advertise nested layer groups in every WMS
        # configuration even though GetMap can address them. Probe those names
        # directly before treating them as missing.
        min_x = tile_to_mercator_x(lon_to_tile_x(settings.bounds.west, 8), 8)
        max_x = tile_to_mercator_x(lon_to_tile_x(settings.bounds.east, 8) + 1, 8)
        max_y = tile_to_mercator_y(lat_to_tile_y(settings.bounds.north, 8), 8)
        min_y = tile_to_mercator_y(lat_to_tile_y(settings.bounds.south, 8) + 1, 8)
        unavailable: list[str] = []
        for layer in missing:
            params = urllib.parse.urlencode(
                {
                    "SERVICE": "WMS",
                    "VERSION": "1.1.1",
                    "REQUEST": "GetMap",
                    "LAYERS": layer,
                    "STYLES": "",
                    "SRS": "EPSG:900913",
                    "BBOX": f"{min_x},{min_y},{max_x},{max_y}",
                    "WIDTH": "1",
                    "HEIGHT": "1",
                    "FORMAT": "image/png",
                    "TRANSPARENT": "TRUE",
                }
            )
            separator = "&" if "?" in settings.wms_url else "?"
            payload, content_type = _http_get(
                settings.wms_url + separator + params, settings.timeout
            )
            if not content_type.startswith("image/") or not payload.startswith(b"\x89PNG"):
                unavailable.append(layer)
        if unavailable:
            raise RuntimeError(
                "GeoServer is reachable but its catalog is missing these layers: "
                + ", ".join(unavailable)
                + ". Verify GEOSERVER_DATA_DIR points at the extracted catalog."
            )
    return hashlib.sha256(capabilities).hexdigest()


def convert(
    settings: ConversionSettings,
    resume: bool,
    max_blocks: int | None,
    max_hours: float | None = None,
    minimum_free_gib: float = 5.0,
    pause_file: Path | None = None,
    progress_file: Path | None = None,
) -> None:
    server_capabilities_sha256 = validate_server(settings)
    rows = plan_rows(settings)
    planned_blocks = sum(int(row["blocks"]) for row in rows)
    started = time.monotonic()
    deadline = started + max_hours * 3600.0 if max_hours is not None else None
    working_path = partial_output_path(settings.output)
    write_progress_file(
        progress_file,
        status="starting",
        output=str(settings.output),
        planned_blocks=planned_blocks,
        completed_blocks=0,
        tile_count=0,
        tile_bytes=0,
        message="Validating output and opening the database",
    )

    if settings.output.exists() and working_path.exists():
        raise RuntimeError(
            f"both final and partial databases exist; preserve them and resolve manually: "
            f"{settings.output}, {working_path}"
        )
    if settings.output.exists():
        if not resume:
            raise FileExistsError(
                f"output already exists: {settings.output} (choose another file)"
            )
        write_progress_file(
            progress_file,
            status="verifying",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=planned_blocks,
            message="Verifying the existing completed package",
        )
        manifest = finalize_database(
            settings.output, settings, server_capabilities_sha256
        )
        verification = manifest["verification"]
        write_progress_file(
            progress_file,
            status="complete",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=verification["completed_blocks"],
            tile_count=verification["tile_count"],
            tile_bytes=verification["tile_bytes"],
            database_size_bytes=settings.output.stat().st_size,
            message="Package is complete and verified",
        )
        print(
            f"Database is already complete and verified: "
            f"{verification['tile_count']:,} tiles in {settings.output}"
        )
        return

    required_free_bytes = int(minimum_free_gib * 1024**3)
    current_free_bytes = available_bytes(settings.output.parent)
    if current_free_bytes < required_free_bytes:
        raise RuntimeError(
            f"only {current_free_bytes / (1024**3):.2f} GiB is free on the output drive; "
            f"at least {minimum_free_gib:.2f} GiB is required"
        )

    with QgcTileDatabase(
        settings,
        resume,
        path=working_path,
        server_capabilities_sha256=server_capabilities_sha256,
    ) as database:
        completed = database.completed_block_keys()
        initial_blocks, initial_tiles, initial_tile_bytes = database.counts()
        write_progress_file(
            progress_file,
            status="running",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=initial_blocks,
            tile_count=initial_tiles,
            tile_bytes=initial_tile_bytes,
            message="Rendering GeoServer blocks",
        )
        blocks: Iterable[TileBlock] = (
            block for block in iter_blocks(settings) if block.key not in completed
        )
        if max_blocks is not None:
            blocks = itertools.islice(blocks, max_blocks)
        iterator = iter(blocks)
        pending: dict[concurrent.futures.Future[RenderedBlock], TileBlock] = {}
        processed_this_run = 0
        response_bytes = 0
        last_report = 0.0
        last_space_check = 0.0
        stop_reason: str | None = None

        def may_schedule() -> bool:
            nonlocal stop_reason, last_space_check
            if stop_reason is not None:
                return False
            now = time.monotonic()
            if pause_file is not None and pause_file.exists():
                stop_reason = "a pause was requested"
                return False
            if deadline is not None and now >= deadline:
                stop_reason = f"the {max_hours:g}-hour run limit was reached"
                return False
            if now - last_space_check >= 60.0:
                free_bytes = available_bytes(settings.output.parent)
                last_space_check = now
                if free_bytes < required_free_bytes:
                    stop_reason = (
                        f"free space fell below {minimum_free_gib:.2f} GiB "
                        f"({free_bytes / (1024**3):.2f} GiB remains)"
                    )
                    return False
            return True

        with concurrent.futures.ThreadPoolExecutor(max_workers=settings.workers) as executor:
            while len(pending) < settings.workers * 2 and may_schedule():
                try:
                    block = next(iterator)
                except StopIteration:
                    break
                pending[executor.submit(render_block, settings, block)] = block
            while pending:
                done, _ = concurrent.futures.wait(
                    pending, return_when=concurrent.futures.FIRST_COMPLETED
                )
                for future in done:
                    block = pending.pop(future)
                    try:
                        rendered = future.result()
                    except Exception as error:
                        for other in pending:
                            other.cancel()
                        raise RuntimeError(f"conversion stopped at block {block.key}: {error}") from error
                    database.save_block(rendered)
                    processed_this_run += 1
                    response_bytes += rendered.response_bytes
                    if may_schedule():
                        try:
                            next_block = next(iterator)
                        except StopIteration:
                            pass
                        else:
                            pending[executor.submit(render_block, settings, next_block)] = next_block
                    now = time.monotonic()
                    if now - last_report >= 1.0 or not pending:
                        total_blocks, total_tiles, total_tile_bytes = database.counts()
                        elapsed = max(now - started, 0.001)
                        print(
                            f"blocks {total_blocks:,}/{planned_blocks:,}; "
                            f"tiles {total_tiles:,}; tile data {total_tile_bytes / (1024**3):.2f} GiB; "
                            f"this run {processed_this_run / elapsed:.2f} blocks/s, "
                            f"{response_bytes / (1024**2):.1f} MiB received",
                            flush=True,
                        )
                        write_progress_file(
                            progress_file,
                            status="pausing" if stop_reason else "running",
                            output=str(settings.output),
                            planned_blocks=planned_blocks,
                            completed_blocks=total_blocks,
                            tile_count=total_tiles,
                            tile_bytes=total_tile_bytes,
                            elapsed_seconds=now - started,
                            message=stop_reason or "Rendering GeoServer blocks",
                        )
                        last_report = now
        total_blocks, total_tiles, total_tile_bytes = database.counts()
        print(
            f"Saved {total_tiles:,} tiles ({total_tile_bytes / (1024**3):.2f} GiB tile data) "
            f"from {total_blocks:,} completed blocks to {working_path}"
        )
        complete = total_blocks == planned_blocks
        if complete:
            database.mark_complete(planned_blocks)

    if complete:
        write_progress_file(
            progress_file,
            status="verifying",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=total_blocks,
            tile_count=total_tiles,
            tile_bytes=total_tile_bytes,
            message="Checking database integrity and computing SHA-256",
        )
        manifest = finalize_database(
            working_path, settings, server_capabilities_sha256
        )
        verification = manifest["verification"]
        write_progress_file(
            progress_file,
            status="complete",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=verification["completed_blocks"],
            tile_count=verification["tile_count"],
            tile_bytes=verification["tile_bytes"],
            database_size_bytes=settings.output.stat().st_size,
            message="Package is complete and verified",
        )
        print(
            f"Production package finalized: {settings.output}\n"
            f"Manifest: {manifest_output_path(settings.output)}\n"
            f"SHA-256: {manifest['database_sha256']}"
        )
    else:
        reason = f" ({stop_reason})" if stop_reason else ""
        write_progress_file(
            progress_file,
            status="paused" if stop_reason == "a pause was requested" else "partial",
            output=str(settings.output),
            planned_blocks=planned_blocks,
            completed_blocks=total_blocks,
            tile_count=total_tiles,
            tile_bytes=total_tile_bytes,
            message=stop_reason or "Run stopped before all blocks were completed",
        )
        print(
            f"The database is partial{reason}: {working_path}\n"
            "Run the same command with --resume to continue."
        )


def load_json_config(path: Path | None) -> dict[str, object]:
    if path is None:
        return {}
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("configuration root must be a JSON object")
    return value


def build_settings(arguments: argparse.Namespace, config: dict[str, object]) -> ConversionSettings:
    def choose(name: str, fallback: object = None) -> object:
        cli_value = getattr(arguments, name)
        return cli_value if cli_value is not None else config.get(name, fallback)

    output_value = choose("output")
    wms_url_value = choose("wms_url")
    bounds_value = choose("bounds")
    raw_rules = arguments.layer_rule or config.get("layer_rules")
    if not output_value or not wms_url_value or bounds_value is None or not raw_rules:
        raise ValueError("output, wms_url, bounds, and at least one layer rule are required")
    map_type = str(choose("map_type", DEFAULT_MAP_TYPE))
    map_type_id_value = choose("map_type_id", DEFAULT_MAP_TYPE_IDS.get(map_type))
    if map_type_id_value is None:
        raise ValueError("map_type_id is required for map types other than 'Geoserver Map'")
    raw_zoom_areas = config.get("zoom_areas", {})
    zoom_areas: list[tuple[int, GeoBounds]] = []
    if raw_zoom_areas:
        if not isinstance(raw_zoom_areas, dict):
            raise ValueError("zoom_areas must be a JSON object keyed by zoom")
        for zoom_text, area_values in raw_zoom_areas.items():
            if not isinstance(area_values, list):
                raise ValueError(f"zoom_areas[{zoom_text}] must be a list of bounds")
            for area_value in area_values:
                if not isinstance(area_value, list):
                    raise ValueError(f"zoom_areas[{zoom_text}] entries must be bounds lists")
                zoom_areas.append((int(zoom_text), parse_bounds(area_value)))
    settings = ConversionSettings(
        wms_url=str(wms_url_value),
        output=Path(str(output_value)).expanduser().resolve(),
        bounds=parse_bounds(bounds_value),
        min_zoom=int(choose("min_zoom", 0)),
        max_zoom=int(choose("max_zoom", 16)),
        layer_rules=tuple(parse_layer_rule(rule) for rule in raw_rules),
        map_type=map_type,
        map_type_id=int(map_type_id_value),
        set_name=str(choose("set_name", "New Zealand GeoServer")),
        block_tiles=int(choose("block_tiles", 8)),
        workers=int(choose("workers", 2)),
        timeout=float(choose("timeout", 120.0)),
        retries=int(choose("retries", 3)),
        wms_format=str(choose("wms_format", "image/png")),
        png_compress_level=int(choose("png_compress_level", 6)),
        skip_transparent=not bool(choose("keep_transparent", False)),
        zoom_areas=tuple(sorted(zoom_areas, key=lambda value: (value[0], value[1].west, value[1].south))),
        source_plan_sha256=(
            str(config["source_plan_sha256"])
            if config.get("source_plan_sha256")
            else None
        ),
    )
    settings.validate()
    return settings


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--config", type=Path, help="JSON conversion configuration")
    result.add_argument("--wms-url", dest="wms_url")
    result.add_argument("--output")
    result.add_argument("--bounds", help="west,south,east,north in WGS 84")
    result.add_argument("--min-zoom", dest="min_zoom", type=int)
    result.add_argument("--max-zoom", dest="max_zoom", type=int)
    result.add_argument(
        "--layer-rule",
        action="append",
        help="repeatable rule such as 0-12=tiger:New Zealand merge",
    )
    result.add_argument("--map-type", dest="map_type")
    result.add_argument("--map-type-id", dest="map_type_id", type=int)
    result.add_argument("--set-name", dest="set_name")
    result.add_argument("--block-tiles", dest="block_tiles", type=int)
    result.add_argument("--workers", type=int)
    result.add_argument("--timeout", type=float)
    result.add_argument("--retries", type=int)
    result.add_argument("--wms-format", dest="wms_format")
    result.add_argument(
        "--png-compress-level",
        dest="png_compress_level",
        type=int,
        help="PNG compression level from 0 (large/fast) to 9 (small/slow)",
    )
    result.add_argument("--keep-transparent", action="store_true", default=None)
    result.add_argument("--resume", action="store_true")
    result.add_argument("--plan", action="store_true", help="show request counts without connecting")
    result.add_argument(
        "--max-blocks",
        type=int,
        help="stop after this many new blocks; useful for a proof-of-concept run",
    )
    result.add_argument(
        "--max-hours",
        type=float,
        help="stop scheduling new blocks after this many hours; in-flight blocks finish safely",
    )
    result.add_argument(
        "--minimum-free-gib",
        type=float,
        default=5.0,
        help="pause before free space on the output drive falls below this value (default: 5)",
    )
    result.add_argument(
        "--verify",
        action="store_true",
        help="verify a completed output and regenerate its SHA-256 manifest",
    )
    result.add_argument(
        "--pause-file",
        type=Path,
        help="finish in-flight blocks and stop safely when this file exists",
    )
    result.add_argument(
        "--progress-file",
        type=Path,
        help="write machine-readable JSON progress for a controller",
    )
    return result


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    try:
        config = load_json_config(arguments.config)
        settings = build_settings(arguments, config)
        if arguments.max_blocks is not None and arguments.max_blocks < 1:
            raise ValueError("max_blocks must be positive")
        if arguments.max_hours is not None and arguments.max_hours <= 0:
            raise ValueError("max_hours must be positive")
        if arguments.minimum_free_gib < 0:
            raise ValueError("minimum_free_gib cannot be negative")
        if arguments.verify and (arguments.max_blocks is not None or arguments.max_hours is not None):
            raise ValueError("--verify cannot be combined with --max-blocks or --max-hours")
        print_plan(settings)
        if not arguments.plan:
            if arguments.verify:
                server_capabilities_sha256 = validate_server(settings)
                manifest = finalize_database(
                    settings.output, settings, server_capabilities_sha256
                )
                verification = manifest["verification"]
                print(
                    f"Verified {verification['tile_count']:,} tiles. "
                    f"Manifest: {manifest_output_path(settings.output)}"
                )
            else:
                convert(
                    settings,
                    arguments.resume,
                    arguments.max_blocks,
                    arguments.max_hours,
                    arguments.minimum_free_gib,
                    arguments.pause_file,
                    arguments.progress_file,
                )
        return 0
    except (FileExistsError, OSError, RuntimeError, ValueError, ET.ParseError) as error:
        if 'arguments' in locals():
            write_progress_file(
                getattr(arguments, "progress_file", None),
                status="error",
                message=str(error),
            )
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
