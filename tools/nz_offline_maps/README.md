# New Zealand GeoServer to QGC offline maps

This experiment converts the extracted GeoServer raster catalog into the
SQLite tile format already used by this QGroundControl tree (`.qgctiledb`). It
does not load the source TIFFs into memory. GeoServer composites and reprojects
aligned Web Mercator image blocks; the converter splits those blocks into XYZ
tiles and writes them directly into QGC's cache schema.

## Why this route

- The extracted catalog contains 211 TIFFs (about 65.1 GiB) and 210 coverage
  stores. GeoServer already knows their projections, ordering, styles, and
  layer groups.
- QGC does not read MBTiles in this branch. Its import/export path expects the
  `Tiles`, `TileSets`, and `SetTiles` SQLite tables implemented by
  `QGCTileCacheWorker`.
- The old `QGeoserverTileSet` importer is disabled and constructs every image
  in a `QList`; that cannot scale to this dataset.
- An 8x8 WMS block replaces 64 individual WMTS requests. Fully transparent
  256x256 results are omitted, and each completed block is recorded for resume.

## 1. Start GeoServer with the extracted catalog

Use the normal GeoServer instance on port 8080. On this workstation its
standard data directory remains `C:\ProgramData\GeoServer`: the extracted
catalog metadata has been copied there and `C:\ProgramData\GeoServer\data\New`
is a directory junction to `E:\GeoServer\GeoServer\data\New`. The 65 GiB of
TIFFs therefore stays on E: without changing normal GeoServer startup.

The original sample catalog is backed up at
`C:\ProgramData\GeoServer-default-backup-20260918-185347`. The standard Start
GeoServer shortcut now works as-is. The equivalent manual start is:

```powershell
$env:GEOSERVER_DATA_DIR = 'C:\ProgramData\GeoServer'
Push-Location 'C:\Program Files\GeoServer'
& 'C:\Program Files\Eclipse Adoptium\jdk-11.0.27.6-hotspot\bin\java.exe' `
  '-jar' 'C:\Program Files\GeoServer\start.jar'
Pop-Location
```

Before converting, open this URL and confirm it contains
`tiger:New Zealand merge`:

`http://127.0.0.1:8080/geoserver/wms?service=WMS&request=GetCapabilities`

## 2. Inspect the upper-bound request count

Use Python 3 with Pillow installed:

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/new_zealand.json `
  --output E:\GeoServer\new-zealand.qgctiledb `
  --plan
```

The candidate count covers a rectangular New Zealand extent. The stored tile
count should be lower because transparent high-zoom tiles are discarded. The
output size cannot be inferred from the 65.1 GiB TIFF size; it depends on
coverage, PNG compression, and selected zooms.

## 3. Run a small proof of concept

This creates only four WMS blocks at zoom 8 and can be opened or imported in
QGC before committing to the full render:

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/new_zealand.json `
  --output E:\GeoServer\nz-poc.qgctiledb `
  --min-zoom 8 --max-zoom 8 --max-blocks 4
```

Continue an interrupted or deliberately partial run with the identical options
plus `--resume`.

## 4. Full conversion

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/new_zealand.json `
  --output E:\GeoServer\new-zealand.qgctiledb
```

The sample policy renders the complete merged layer through zoom 12. At zooms
13-16 it excludes the global `Earth Map` layer and composes the New Zealand
chart groups cumulatively. This avoids filling millions of high-zoom tiles with
an upscaled global background while retaining lower-resolution charts beneath
the detailed charts.

Start with two workers. GeoTIFF rendering is disk- and memory-heavy; increasing
the worker count can make GeoServer slower rather than faster. The output is
safe to resume after a failure.

## 5. Load it into QGC

Select **Geoserver Map** as the map type. The generated database uses the
provider ID `685018728`, verified from this tree's Qt 5.15.2 build.

For a small proof of concept, use **Application Settings > Offline Maps >
Import** and choose **Replace existing set**. For the full database, QGC's
replace import performs another full file copy. On a constrained device it is
better to stop QGC and place the completed file directly at the `Map Cache in:`
path printed in QGC's log, renamed to `qgcMapCache.db`. This branch chooses an
Android removable app-specific directory when one exists.

Keep a backup: direct placement replaces QGC's current map cache. Allow enough
space for SQLite journal files during generation and for a second full copy if
using the UI importer.

## Production decision after the experiment

If the resulting single SQLite database performs well on the target hardware,
the direct QGC format is the shortest path. If database size, copy time, or
random-read performance is poor, the next step should be a read-only MBTiles
provider in QGC with regional packages. MBTiles is easier to build and move,
but this QGC version needs code changes before it can display it.
