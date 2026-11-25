#!/usr/bin/env python3
"""Read native chart resolution and footprints from an extracted GeoServer catalog."""

from __future__ import annotations

import hashlib
import math
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


WEB_MERCATOR_INITIAL_RESOLUTION = 156543.03392804097


@dataclass(frozen=True)
class Bounds:
    west: float
    south: float
    east: float
    north: float

    def intersects(self, other: "Bounds") -> bool:
        return not (
            self.east < other.west
            or self.west > other.east
            or self.north < other.south
            or self.south > other.north
        )

    def intersection(self, other: "Bounds") -> "Bounds | None":
        if not self.intersects(other):
            return None
        return Bounds(
            max(self.west, other.west),
            max(self.south, other.south),
            min(self.east, other.east),
            min(self.north, other.north),
        )

    def as_list(self) -> list[float]:
        return [self.west, self.south, self.east, self.north]


@dataclass(frozen=True)
class Coverage:
    resource_id: str
    name: str
    group: str
    native_metres_per_pixel: float
    useful_max_zoom: int
    bounds: Bounds


def _text(element: ET.Element, path: str) -> str:
    child = element.find(path)
    return (child.text or "").strip() if child is not None else ""


def useful_xyz_zoom(native_metres_per_pixel: float) -> int:
    """Return the XYZ zoom whose Web Mercator pixel size is closest to the source."""
    if native_metres_per_pixel <= 0:
        raise ValueError("native pixel size must be positive")
    return round(math.log2(WEB_MERCATOR_INITIAL_RESOLUTION / native_metres_per_pixel))


def catalog_fingerprint(paths: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(set(paths), key=lambda value: str(value).lower()):
        digest.update(str(path).encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def load_chart_coverages(workspace: Path) -> tuple[list[Coverage], str]:
    """Return unique coverages referenced by North/South Island zoom groups."""
    workspace = workspace.resolve()
    if not workspace.is_dir():
        raise FileNotFoundError(f"GeoServer workspace does not exist: {workspace}")

    layer_files = list(workspace.rglob("layer.xml"))
    coverage_files = list(workspace.rglob("coverage.xml"))
    group_files = sorted((workspace / "layergroups").glob("*Island ZOOM *.xml"))
    if not group_files:
        raise RuntimeError(f"no North/South Island zoom groups found in {workspace}")

    layers: dict[str, tuple[str, str]] = {}
    parsed_paths: list[Path] = list(group_files)
    for path in layer_files:
        try:
            root = ET.parse(path).getroot()
            layer_id = _text(root, "id")
            resource_id = _text(root, "resource/id")
            if layer_id and resource_id:
                layers[layer_id] = (_text(root, "name"), resource_id)
                parsed_paths.append(path)
        except (OSError, ET.ParseError):
            continue

    coverage_metadata: dict[str, tuple[str, float, Bounds, Path]] = {}
    for path in coverage_files:
        try:
            root = ET.parse(path).getroot()
            resource_id = _text(root, "id")
            scale_x = abs(float(_text(root, "grid/transform/scaleX")))
            scale_y = abs(float(_text(root, "grid/transform/scaleY")))
            native_resolution = max(scale_x, scale_y)
            box = root.find("latLonBoundingBox")
            if not resource_id or box is None or native_resolution <= 0:
                continue
            bounds = Bounds(
                float(_text(box, "minx")),
                float(_text(box, "miny")),
                float(_text(box, "maxx")),
                float(_text(box, "maxy")),
            )
            coverage_metadata[resource_id] = (
                _text(root, "name"),
                native_resolution,
                bounds,
                path,
            )
        except (OSError, ET.ParseError, ValueError):
            continue

    coverages: dict[str, Coverage] = {}
    for group_path in group_files:
        root = ET.parse(group_path).getroot()
        group_name = _text(root, "name")
        for published in root.findall("publishables/published"):
            if published.attrib.get("type") != "layer":
                continue
            layer = layers.get(_text(published, "id"))
            if layer is None:
                continue
            layer_name, resource_id = layer
            metadata = coverage_metadata.get(resource_id)
            if metadata is None:
                continue
            coverage_name, native_resolution, bounds, coverage_path = metadata
            parsed_paths.append(coverage_path)
            coverages.setdefault(
                resource_id,
                Coverage(
                    resource_id=resource_id,
                    name=layer_name or coverage_name,
                    group=group_name,
                    native_metres_per_pixel=native_resolution,
                    useful_max_zoom=useful_xyz_zoom(native_resolution),
                    bounds=bounds,
                ),
            )

    if not coverages:
        raise RuntimeError("GeoServer catalog contained no chart coverages")
    return sorted(coverages.values(), key=lambda value: value.name.lower()), catalog_fingerprint(parsed_paths)


def package_zoom_areas(
    coverages: Iterable[Coverage],
    package_bounds: Bounds,
    first_sparse_zoom: int = 17,
    maximum_zoom: int = 19,
) -> dict[int, list[list[float]]]:
    """Return clipped source footprints for useful sparse high-zoom rendering."""
    result: dict[int, list[list[float]]] = {}
    coverage_list = list(coverages)
    for zoom in range(first_sparse_zoom, maximum_zoom + 1):
        areas: list[list[float]] = []
        for coverage in coverage_list:
            if coverage.useful_max_zoom < zoom:
                continue
            intersection = coverage.bounds.intersection(package_bounds)
            if intersection is not None:
                areas.append(intersection.as_list())
        if areas:
            result[zoom] = areas
    return result

