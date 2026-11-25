#!/usr/bin/env python3
"""Preview completed QGC tile databases together in a local offline browser map."""

from __future__ import annotations

import argparse
import functools
import hashlib
import io
import json
import math
import sqlite3
import threading
import urllib.error
import urllib.request
import webbrowser
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Sequence

from PIL import Image


MAP_TYPE_ID = 685018728


HTML_PAGE = r"""
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>New Zealand offline-map QA</title>
<style>
  :root { color-scheme: light dark; font-family: Segoe UI, sans-serif; }
  * { box-sizing: border-box; }
  html, body { margin: 0; width: 100%; height: 100%; overflow: hidden; background: Canvas; color: CanvasText; }
  #map { position: absolute; inset: 0; width: 100%; height: 100%; touch-action: none; }
  .toolbar { position: absolute; top: 12px; left: 12px; right: 12px; display: flex; gap: 8px; align-items: center; flex-wrap: wrap; padding: 9px; background: color-mix(in srgb, Canvas 92%, transparent); border: 1px solid GrayText; border-radius: 8px; }
  button, select { font: inherit; padding: 6px 9px; }
  select { min-width: 220px; }
  .status { margin-left: auto; font-variant-numeric: tabular-nums; }
  .detail { position: absolute; left: 12px; bottom: 12px; max-width: min(620px, calc(100% - 24px)); padding: 8px 10px; background: color-mix(in srgb, Canvas 92%, transparent); border: 1px solid GrayText; border-radius: 8px; }
  .muted { color: GrayText; }
</style>
</head>
<body>
<canvas id="map" aria-label="Offline map preview"></canvas>
<div class="toolbar">
  <button id="minus" type="button" aria-label="Zoom out">−</button>
  <button id="plus" type="button" aria-label="Zoom in">+</button>
  <select id="packages" aria-label="Jump to package"><option value="">All New Zealand</option></select>
  <label><input id="bounds" type="checkbox" checked> package edges</label>
  <span class="status" id="status"></span>
</div>
<div class="detail" id="detail">Drag to pan, use the mouse wheel to zoom, and click a tile to see which database supplied it.</div>
<script>
const canvas = document.getElementById('map');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const detailEl = document.getElementById('detail');
const packageSelect = document.getElementById('packages');
const boundsToggle = document.getElementById('bounds');
let centerLon = 173.5, centerLat = -41.2, zoom = 6;
let dragging = false, dragX = 0, dragY = 0, dragCenter = null;
let packages = [];
const images = new Map();

function lonToWorldX(lon, z) { return (lon + 180) / 360 * 256 * 2 ** z; }
function latToWorldY(lat, z) {
  const r = lat * Math.PI / 180;
  return (1 - Math.asinh(Math.tan(r)) / Math.PI) / 2 * 256 * 2 ** z;
}
function worldXToLon(x, z) { return x / (256 * 2 ** z) * 360 - 180; }
function worldYToLat(y, z) {
  const n = Math.PI - 2 * Math.PI * y / (256 * 2 ** z);
  return 180 / Math.PI * Math.atan(Math.sinh(n));
}
function centerWorld() { return [lonToWorldX(centerLon, zoom), latToWorldY(centerLat, zoom)]; }

function resize() {
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(innerWidth * dpr);
  canvas.height = Math.round(innerHeight * dpr);
  canvas.style.width = innerWidth + 'px';
  canvas.style.height = innerHeight + 'px';
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  draw();
}

function requestTile(z, x, y) {
  const n = 2 ** z;
  const wrappedX = ((x % n) + n) % n;
  const key = `${z}/${wrappedX}/${y}`;
  if (images.has(key)) return images.get(key);
  const item = { state: 'loading', image: null };
  const image = new Image();
  image.onload = () => { item.state = 'ready'; item.image = image; draw(); };
  image.onerror = () => { item.state = 'missing'; draw(); };
  image.src = `/tiles/${z}/${wrappedX}/${y}.png`;
  images.set(key, item);
  if (images.size > 1200) {
    for (const oldKey of images.keys()) { images.delete(oldKey); if (images.size <= 900) break; }
  }
  return item;
}

function draw() {
  const width = innerWidth, height = innerHeight;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = getComputedStyle(document.body).backgroundColor;
  ctx.fillRect(0, 0, width, height);
  const [cx, cy] = centerWorld();
  const left = cx - width / 2, top = cy - height / 2;
  const firstX = Math.floor(left / 256), lastX = Math.floor((left + width) / 256);
  const firstY = Math.floor(top / 256), lastY = Math.floor((top + height) / 256);
  const n = 2 ** zoom;
  for (let y = firstY; y <= lastY; y++) {
    if (y < 0 || y >= n) continue;
    for (let x = firstX; x <= lastX; x++) {
      const sx = Math.round(x * 256 - left), sy = Math.round(y * 256 - top);
      const tile = requestTile(zoom, x, y);
      if (tile.state === 'ready') ctx.drawImage(tile.image, sx, sy, 256, 256);
      else if (tile.state === 'missing') {
        ctx.fillStyle = 'color-mix(in srgb, GrayText 8%, transparent)';
        ctx.fillRect(sx, sy, 256, 256);
      }
    }
  }
  if (boundsToggle.checked) {
    ctx.lineWidth = 2;
    ctx.font = '12px Segoe UI';
    for (const item of packages) {
      if (zoom < item.min_zoom || zoom > item.max_zoom) continue;
      const x0 = lonToWorldX(item.west, zoom) - left;
      const x1 = lonToWorldX(item.east, zoom) - left;
      const y0 = latToWorldY(item.north, zoom) - top;
      const y1 = latToWorldY(item.south, zoom) - top;
      ctx.strokeStyle = 'rgba(255, 110, 30, .9)';
      ctx.strokeRect(x0, y0, x1 - x0, y1 - y0);
      if (x1 >= 0 && x0 <= width && y1 >= 0 && y0 <= height) {
        ctx.fillStyle = 'rgba(255, 110, 30, .95)';
        ctx.fillText(item.name, Math.max(5, x0 + 4), Math.max(56, y0 + 15));
      }
    }
  }
  statusEl.textContent = `z${zoom}  ${centerLat.toFixed(4)}, ${centerLon.toFixed(4)}`;
}

let maxAvailableZoom = 19;

function changeZoom(newZoom, anchorX = innerWidth / 2, anchorY = innerHeight / 2) {
  newZoom = Math.max(0, Math.min(maxAvailableZoom, newZoom));
  if (newZoom === zoom) return;
  const [cx, cy] = centerWorld();
  const wx = cx + anchorX - innerWidth / 2;
  const wy = cy + anchorY - innerHeight / 2;
  const lon = worldXToLon(wx, zoom), lat = worldYToLat(wy, zoom);
  zoom = newZoom;
  const targetX = lonToWorldX(lon, zoom), targetY = latToWorldY(lat, zoom);
  const newCx = targetX - anchorX + innerWidth / 2;
  const newCy = targetY - anchorY + innerHeight / 2;
  centerLon = worldXToLon(newCx, zoom);
  centerLat = worldYToLat(newCy, zoom);
  draw();
}

canvas.addEventListener('pointerdown', event => {
  dragging = true; dragX = event.clientX; dragY = event.clientY; dragCenter = centerWorld();
  canvas.setPointerCapture(event.pointerId);
});
canvas.addEventListener('pointermove', event => {
  if (!dragging) return;
  centerLon = worldXToLon(dragCenter[0] - (event.clientX - dragX), zoom);
  centerLat = worldYToLat(dragCenter[1] - (event.clientY - dragY), zoom);
  draw();
});
canvas.addEventListener('pointerup', event => { dragging = false; canvas.releasePointerCapture(event.pointerId); });
canvas.addEventListener('wheel', event => { event.preventDefault(); changeZoom(zoom + (event.deltaY < 0 ? 1 : -1), event.clientX, event.clientY); }, { passive: false });
canvas.addEventListener('click', async event => {
  if (Math.abs(event.clientX - dragX) > 3 || Math.abs(event.clientY - dragY) > 3) return;
  const [cx, cy] = centerWorld();
  const wx = cx + event.clientX - innerWidth / 2, wy = cy + event.clientY - innerHeight / 2;
  const x = Math.floor(wx / 256), y = Math.floor(wy / 256);
  const result = await fetch(`/inspect?z=${zoom}&x=${x}&y=${y}`).then(r => r.json());
  if (!result.matches.length) detailEl.textContent = `z${zoom}/${x}/${y}: no stored tile`;
  else {
    const names = result.matches.map(m => m.name + (m.same_as_first ? '' : ' (different pixels)'));
    detailEl.textContent = `z${zoom}/${x}/${y}: ${names.join(' · ')}`;
  }
});
document.getElementById('plus').onclick = () => changeZoom(zoom + 1);
document.getElementById('minus').onclick = () => changeZoom(zoom - 1);
boundsToggle.onchange = draw;
packageSelect.onchange = () => {
  const item = packages.find(value => value.file === packageSelect.value);
  if (!item) { centerLon = 173.5; centerLat = -41.2; zoom = 6; draw(); return; }
  centerLon = (item.west + item.east) / 2;
  centerLat = (item.south + item.north) / 2;
  zoom = item.min_zoom >= 13 ? 13 : 7;
  draw();
};

fetch('/sources.json').then(response => response.json()).then(data => {
  packages = data.sources;
  maxAvailableZoom = Math.max(0, ...packages.map(item => item.max_zoom));
  for (const item of packages) {
    const option = document.createElement('option');
    option.value = item.file; option.textContent = item.name;
    packageSelect.appendChild(option);
  }
  draw();
});
window.addEventListener('resize', resize);
resize();
</script>
</body>
</html>
"""


@dataclass
class TileSource:
    path: Path
    name: str
    west: float
    south: float
    east: float
    north: float
    min_zoom: int
    max_zoom: int
    connection: sqlite3.Connection
    lock: threading.Lock

    def contains(self, zoom: int, x: int, y: int) -> bool:
        if not self.min_zoom <= zoom <= self.max_zoom:
            return False
        x0, y0, x1, y1 = tile_range(self.west, self.south, self.east, self.north, zoom)
        return x0 <= x <= x1 and y0 <= y <= y1


def lon_to_tile_x(lon: float, zoom: int) -> int:
    count = 1 << zoom
    return min(count - 1, max(0, int(math.floor((lon + 180.0) / 360.0 * count))))


def lat_to_tile_y(lat: float, zoom: int) -> int:
    count = 1 << zoom
    latitude = math.radians(lat)
    value = (1.0 - math.asinh(math.tan(latitude)) / math.pi) / 2.0 * count
    return min(count - 1, max(0, int(math.floor(value))))


def tile_range(
    west: float, south: float, east: float, north: float, zoom: int
) -> tuple[int, int, int, int]:
    return (
        lon_to_tile_x(west, zoom),
        lat_to_tile_y(north, zoom),
        lon_to_tile_x(east, zoom),
        lat_to_tile_y(south, zoom),
    )


def qgc_hash(x: int, y: int, zoom: int) -> str:
    return f"{MAP_TYPE_ID:010d}{x:08d}{y:08d}{zoom:03d}"


class OfflineMapCollection:
    def __init__(self, directory: Path) -> None:
        self.directory = directory
        self.sources: list[TileSource] = []
        for path in sorted(directory.glob("*.qgctiledb")):
            uri = path.resolve().as_uri() + "?mode=ro&immutable=1"
            connection = sqlite3.connect(uri, uri=True, check_same_thread=False)
            row = connection.execute(
                "SELECT name, topleftLon, bottomRightLat, bottomRightLon, "
                "topleftLat, minZoom, maxZoom FROM TileSets ORDER BY setID LIMIT 1"
            ).fetchone()
            if row is None:
                connection.close()
                continue
            self.sources.append(
                TileSource(
                    path=path,
                    name=str(row[0]),
                    west=float(row[1]),
                    south=float(row[2]),
                    east=float(row[3]),
                    north=float(row[4]),
                    min_zoom=int(row[5]),
                    max_zoom=int(row[6]),
                    connection=connection,
                    lock=threading.Lock(),
                )
            )
        if not self.sources:
            raise RuntimeError(f"no completed .qgctiledb files found in {directory}")

    def _exact_tile(self, zoom: int, x: int, y: int) -> tuple[bytes, str, str] | None:
        tile_hash = qgc_hash(x, y, zoom)
        for source in self.sources:
            if not source.contains(zoom, x, y):
                continue
            with source.lock:
                row = source.connection.execute(
                    "SELECT tile, format FROM Tiles WHERE hash = ?", (tile_hash,)
                ).fetchone()
            if row is not None:
                return bytes(row[0]), str(row[1]), source.path.name
        return None

    @functools.lru_cache(maxsize=2048)
    def tile(self, zoom: int, x: int, y: int) -> tuple[bytes, str, str] | None:
        exact = self._exact_tile(zoom, x, y)
        if exact is not None:
            return exact

        # Native-resolution V2 deliberately stores high zooms only over chart
        # footprints. Match QGC by magnifying the closest stored ancestor in
        # the gaps instead of displaying a blank tile.
        for ancestor_zoom in range(zoom - 1, -1, -1):
            delta = zoom - ancestor_zoom
            factor = 1 << delta
            ancestor = self._exact_tile(ancestor_zoom, x // factor, y // factor)
            if ancestor is None:
                continue
            payload, _, source_name = ancestor
            with Image.open(io.BytesIO(payload)) as image:
                sub_x = x % factor
                sub_y = y % factor
                left = sub_x * image.width // factor
                top = sub_y * image.height // factor
                right = max(left + 1, (sub_x + 1) * image.width // factor)
                bottom = max(top + 1, (sub_y + 1) * image.height // factor)
                crop = image.crop((left, top, right, bottom)).resize(
                    (256, 256), Image.Resampling.NEAREST
                )
                output = io.BytesIO()
                crop.save(output, "PNG")
            return output.getvalue(), "png", f"{source_name} (fallback z{ancestor_zoom})"
        return None

    def inspect(self, zoom: int, x: int, y: int) -> list[dict[str, object]]:
        tile_hash = qgc_hash(x, y, zoom)
        matches: list[dict[str, object]] = []
        first_digest: str | None = None
        for source in self.sources:
            if not source.contains(zoom, x, y):
                continue
            with source.lock:
                row = source.connection.execute(
                    "SELECT tile FROM Tiles WHERE hash = ?", (tile_hash,)
                ).fetchone()
            if row is None:
                continue
            digest = hashlib.sha256(bytes(row[0])).hexdigest()
            if first_digest is None:
                first_digest = digest
            matches.append(
                {
                    "file": source.path.name,
                    "name": source.name,
                    "sha256": digest,
                    "same_as_first": digest == first_digest,
                }
            )
        if not matches:
            fallback = self.tile(zoom, x, y)
            if fallback is not None:
                payload, _, source_name = fallback
                matches.append(
                    {
                        "file": source_name,
                        "name": source_name,
                        "sha256": hashlib.sha256(payload).hexdigest(),
                        "same_as_first": True,
                    }
                )
        return matches

    def metadata(self) -> list[dict[str, object]]:
        return [
            {
                "file": source.path.name,
                "name": source.name,
                "west": source.west,
                "south": source.south,
                "east": source.east,
                "north": source.north,
                "min_zoom": source.min_zoom,
                "max_zoom": source.max_zoom,
                "size_bytes": source.path.stat().st_size,
            }
            for source in self.sources
        ]

    def close(self) -> None:
        for source in self.sources:
            source.connection.close()


class PreviewHandler(BaseHTTPRequestHandler):
    collection: OfflineMapCollection

    def do_GET(self) -> None:
        path, _, query = self.path.partition("?")
        if path == "/":
            self.send_payload(HTML_PAGE.encode("utf-8"), "text/html; charset=utf-8")
            return
        if path == "/health":
            self.send_payload(b"qgc-offline-map-preview\n", "text/plain")
            return
        if path == "/sources.json":
            payload = json.dumps({"sources": self.collection.metadata()}).encode("utf-8")
            self.send_payload(payload, "application/json")
            return
        if path == "/inspect":
            from urllib.parse import parse_qs

            values = parse_qs(query)
            try:
                zoom = int(values["z"][0])
                x = int(values["x"][0])
                y = int(values["y"][0])
            except (KeyError, ValueError):
                self.send_error(HTTPStatus.BAD_REQUEST)
                return
            payload = json.dumps(
                {"z": zoom, "x": x, "y": y, "matches": self.collection.inspect(zoom, x, y)}
            ).encode("utf-8")
            self.send_payload(payload, "application/json")
            return
        if path.startswith("/tiles/"):
            parts = path.removeprefix("/tiles/").removesuffix(".png").split("/")
            try:
                zoom, x, y = (int(value) for value in parts)
            except (ValueError, TypeError):
                self.send_error(HTTPStatus.BAD_REQUEST)
                return
            result = self.collection.tile(zoom, x, y)
            if result is None:
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            payload, image_format, _ = result
            self.send_payload(payload, f"image/{image_format}", cache=True)
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def send_payload(self, payload: bytes, content_type: str, cache: bool = False) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "public, max-age=3600" if cache else "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format: str, *args: object) -> None:
        return


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--directory",
        type=Path,
        default=Path(r"E:\GeoServer\QGCOfflineMaps\NewZealand-Native-V2"),
    )
    result.add_argument("--host", default="127.0.0.1")
    result.add_argument("--port", type=int, default=8765)
    result.add_argument("--no-browser", action="store_true")
    return result


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    url = f"http://{arguments.host}:{arguments.port}/"
    try:
        with urllib.request.urlopen(url + "health", timeout=1) as response:
            if response.read().startswith(b"qgc-offline-map-preview"):
                if not arguments.no_browser:
                    webbrowser.open(url)
                return 0
    except (OSError, urllib.error.URLError):
        pass

    collection = OfflineMapCollection(arguments.directory.resolve())
    PreviewHandler.collection = collection
    server = ThreadingHTTPServer((arguments.host, arguments.port), PreviewHandler)
    print(f"Previewing {len(collection.sources)} databases at {url}", flush=True)
    if not arguments.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        collection.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
