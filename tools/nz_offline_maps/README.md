# New Zealand GeoServer to QGC offline maps

This experiment converts the extracted GeoServer raster catalog into the
SQLite tile format already used by this QGroundControl tree (`.qgctiledb`). It
does not load the source TIFFs into memory. GeoServer composites and reprojects
aligned Web Mercator image blocks; the converter splits those blocks into XYZ
tiles and writes them directly into QGC's cache schema.

## Why this route

- The extracted catalog contains 203 unique chart coverages referenced by the
  North/South Island groups. GeoServer already knows their projections,
  ordering, styles, and layer groups.
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

Incomplete output is deliberately named `.qgctiledb.partial`, so QGC cannot
mistake it for a release package. Continue it with the identical options plus
`--resume`.

## 4. Full conversion

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/new_zealand.json `
  --output E:\GeoServer\new-zealand.qgctiledb
```

The production V2 policy requests `tiger:New Zealand merge` at every level, so
the pixels and overlap ordering are exactly those served by GeoServer. The
national overview is complete through zoom 12. Each region is complete from
zoom 13 through zoom 16. Zooms 17-19 are sparse: they are rendered only over
the local TIFF footprints whose native pixel size justifies that zoom.

The words `ZOOM 8`, `ZOOM 13`, and so on in the GeoServer group names are
visibility thresholds, not proof that the source stops at that zoom. The
controller's **Source Audit** reads every coverage grid transform and computes
the closest Web Mercator zoom from its native metres per pixel. This prevents
both arbitrary upscaling and accidental loss of the detailed harbour insets.

Start with two workers. GeoTIFF rendering is disk- and memory-heavy; increasing
the worker count can make GeoServer slower rather than faster. The output is
safe to resume after a failure.

For an overnight or other bounded run, stop scheduling new work after five
hours. Already-running WMS blocks are committed before the process exits:

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/new_zealand.json `
  --output E:\GeoServer\new-zealand.qgctiledb `
  --max-hours 5
```

Resume with the same config, output, and `--resume`. The converter also pauses
if output-drive free space falls below 5 GiB. Change that safety reserve with
`--minimum-free-gib`.

## Release verification

A complete conversion is published only after all planned blocks are present,
SQLite `quick_check` passes, tile/link counts agree, every record has a PNG
signature, and 32 evenly distributed PNG samples decode. The converter then
renames the `.partial` file to `.qgctiledb` and writes a neighbouring
`.manifest.json` containing the bounds, zooms, layers, tile counts, GeoServer
capabilities fingerprint, file size, and full-database SHA-256.

Recheck an existing production package and regenerate its manifest with:

```powershell
python tools/nz_offline_maps/geoserver_to_qgc.py `
  --config tools/nz_offline_maps/northland.json `
  --output E:\GeoServer\QGCOfflineMaps\01-northland.qgctiledb `
  --verify
```

If GeoServer's advertised catalog changes after a partial conversion starts,
resume is rejected rather than combining imagery from two catalog versions.

## Regional production layout

Build `new_zealand_overview.json` once for zooms 0-12. Regional packages begin
at zoom 13 so installing neighbouring regions does not duplicate the national
overview. `northland.json` is the first detail-package candidate; its generous
marine bounds intentionally overlap Auckland and include offshore chart data.
Its boundary must be visually approved before using it as the template for the
remaining regions.

## Production control panel

Double-click `Start-NewZealandConversion.cmd` to open the Windows production
controller. It lists the national overview and all 16 regional detail
packages, writes V2 to
`E:\GeoServer\QGCOfflineMaps\NewZealand-Native-V2` by default, and runs one
package at a time so GeoServer is not overloaded. The earlier output in
`E:\GeoServer\QGCOfflineMaps\NewZealand` is not changed or overwritten.

The controller displays completed/planned WMS blocks, tile count, data size,
status, and the live converter log. **Pause Safely** creates a pause request;
the converter finishes the few WMS blocks already in flight, checkpoints the
SQLite database, and leaves a `.partial` file. **Start / Resume Selected**
continues that exact database. Closing the controller while work is active
offers the same safe-pause behavior.

The default five-hour auto-pause applies to the active package and also stops
the package queue, producing practical overnight milestones. Set it to `0` to
run continuously. Generated configuration snapshots are kept in the output
folder so a resumed package cannot silently pick up later boundary changes.
The **WMS workers** control changes concurrency without invalidating a partial
database. Thirty-two workers is the measured high-throughput default for this
16-core Ryzen 9 9950X and 94 GiB workstation. The benchmark used about 13 GiB
for GeoServer; reduce the worker count on machines with less memory or if block
throughput becomes worse.

## Check the completed maps

Double-click `Check-NewZealandMaps.cmd`, or use **Check Maps** in the production
controller. The local browser preview reads the completed SQLite databases
directly and does not contact GeoServer or the internet. At zooms 0-12 it shows
the overview; at zooms 13-16 it stitches the regional packages in the same
alphabetical-first order used by QGC. It can continue to zoom 19 over native
high-detail chart footprints. Where a sparse high-zoom tile is intentionally
absent, it magnifies the nearest stored ancestor tile, matching QGC. Orange
rectangles show the deliberate package cuts. Click any tile to see which
database or fallback level supplies it.

## 5. Load it into QGC

Select **Geoserver Map** as the map type. The generated database uses the
provider ID `685018728`, verified from this tree's Qt 5.15.2 build.

Use **Application Settings > Offline Maps > Import**. Imports now always remain
separate map entries and never merge tiles into the default cache. Choose one
of two explicit modes:

- **Keep file in place** mounts the selected `.qgctiledb` read-only, regardless
  of file size. This is the normal SD-card choice.
- **Copy into device memory** copies the whole database into QGC's private
  package directory, then mounts that copy as a separate entry.

On Android the Offline Maps page displays QGC's dedicated `FishMaps`
folder on the removable SD card. Files placed there are discovered at startup
or with **Scan SD-card folder** and are always kept in place. Mounted packages
are checked before the normal cache, so an older online tile cannot cover a
packaged tile. Import failures remain visible on the Offline Maps page until
dismissed.

Downloading new offline sets is disabled for the GeoServer marine map because
on-demand chart rendering is too slow. Aerial offline-map downloads remain
available. Use the prepared Aerokontiki marine packages in the SD-card folder
instead. Keep each database and its manifest together and retain a separate
backup of every production package.

Native-resolution V2 requires the current QGC source to be rebuilt before
device testing. Its tile-cache worker includes ancestor fallback for the sparse
Z17-Z19 levels; without that change, areas just outside a high-detail chart
footprint would appear blank while zoomed in. Fallback tiles are generated in
memory and are not copied into the cache or written back to the SD card.

The Android reader cancels obsolete cache work when the map moves, routes each
request only to regional packages whose bounds and zoom range can contain the
tile, limits ancestor searches to six zoom levels, and keeps generated fallback
tiles in a bounded memory cache. These safeguards are important with multiple
large SQLite packages on removable storage: rapid zooming must not leave old
requests scanning every regional database before the current view can load.

## Production decision after the experiment

If the resulting single SQLite database performs well on the target hardware,
the direct QGC format is the shortest path. If database size, copy time, or
random-read performance is poor, the next step should be a read-only MBTiles
provider in QGC with regional packages. MBTiles is easier to build and move,
but this QGC version needs code changes before it can display it.
