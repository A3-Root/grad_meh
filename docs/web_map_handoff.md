# MEH Web Map Handoff

This document describes how to build the stable-branch `grad_meh` addon, export map data from Arma 3, and consume the output in a web map server/viewer. OCAP/diagnostic SVG export now lives in the separate `ocap-renderterrain` repository and addon.

## Build Instructions

The native exporter is an Arma 3 Intercept plugin, so the release must be built on Windows with the Arma/Intercept toolchain available.

Required tools:

- Arma 3 installed
- Arma 3 Tools installed
- Visual Studio 2022 with MSVC C++ build tools
- CMake 3.28 or newer
- Ninja
- Conan compatible with the repo lockfile/profile
- Rust toolchain, used by `src/arma-file-formats-cxx`
- HEMTT

From the VS Code terminal:

```powershell
conan install . --output-folder=build --build=missing --profile=ci-conan-profile
cmake --preset ninja-release-win
cmake --build --preset ninja-msvc-release
hemtt release
```

Expected release artifact:

- HEMTT creates a release under `releases/`.
- The release must include the `@grad_meh` addon files and the built Intercept DLL under `@grad_meh/intercept/`.

If current HEMTT reports `.hemtt/project.toml not found`, either use the older HEMTT version this repo was created for, or migrate `hemtt.toml` to HEMTT v1. The source build still happens through CMake; HEMTT packages the Arma addon and copies the built plugin DLL via the `releasebuild` script in `hemtt.toml`.

## In-Game Export

The addon registers this SQF command:

```sqf
gradMehExportMap [mapId, sat, topo, bakedTopo, geojson, previewImg, meta, dem]
```

Arguments:

- `mapId`: CfgWorlds class name, for example `"Stratis"`.
- `sat`: export satellite image tiles.
- `topo`: export generated topographic raster tiles.
- `bakedTopo`: export generated topographic raster tiles with available map features baked in.
- `geojson`: export vector features.
- `previewImg`: export map preview image.
- `meta`: export `meta.json`.
- `dem`: export digital elevation model.
Example:

```sqf
gradMehExportMap ["Stratis", true, true, true, true, true, true, true];
```

The UI exposes the same options through checkboxes.

## Output Layout

Exports are written under the Arma 3 installation directory:

```text
grad_meh/{worldName}/
  meta.json
  preview.png
  dem.asc.gz
  sat/
    0/0.png
    ...
    3/3.png
    tiles/{z}/{x}/{y}.png
  sat_dark/
    tiles/{z}/{x}/{y}.png
  baked_sat/
    tiles/{z}/{x}/{y}.png
  baked_sat_dark/
    tiles/{z}/{x}/{y}.png
  topo/
    tiles/{z}/{x}/{y}.png
  topo_dark/
    tiles/{z}/{x}/{y}.png
  baked_topo/
    tiles/{z}/{x}/{y}.png
  baked_topo_dark/
    tiles/{z}/{x}/{y}.png
  geojson/
    roads/*.geojson.gz
    *.geojson.gz
```

`worldName` is lowercased.

## Raster Tiles

Satellite:

- Legacy 4 by 4 tiles remain in `sat/{x}/{y}.png`.
- Multi-zoom tiles are in `sat/tiles/{z}/{x}/{y}.png`.
- `sat_dark/tiles/{z}/{x}/{y}.png` is the dark-mode satellite equivalent.
- `baked_sat/tiles/{z}/{x}/{y}.png` and `baked_sat_dark/tiles/{z}/{x}/{y}.png` burn WRP-native map features into the satellite raster.
- Tiles are 256 by 256 pixels.
- `z = 0` is the most zoomed-out overview level.
- Each following zoom level doubles the image width and height, so larger `z` values are more zoomed in.
- Topographic layers are rendered at 1 pixel per meter at their maximum zoom level.

Topographic:

- `topo/tiles/{z}/{x}/{y}.png` is generated from elevation data.
- `topo/tiles/{z}/{x}/{y}.png` uses the same 256 pixel tile pyramid as satellite.
- `topo_dark/tiles/{z}/{x}/{y}.png` is the dark-mode topographic equivalent.
- The topo renderer currently uses elevation tinting, hillshade, and contour lines from the WRP elevation grid. It is not a capture of the in-game paper map.

Baked topography:

- `baked_topo/tiles/{z}/{x}/{y}.png` starts from the generated topo raster.
- `baked_topo/tiles/{z}/{x}/{y}.png` uses the same 256 pixel tile pyramid.
- `baked_topo_dark/tiles/{z}/{x}/{y}.png` is the dark-mode baked topographic equivalent.
- The current baked renderer draws available WRP-native map features into the raster, including house/building polygons, road network lines, powerlines, and river polygons.
- Detailed road classes are still exported as GeoJSON and can be overlaid by the web map from `geojson/roads/*.geojson.gz`.

Tile coordinates use image-space origin:

- `x = 0`, `y = 0` is the top-left tile.
- The exported rasters represent the full Arma world extent.
- Convert world coordinates to raster pixels with:

```text
pixelX = worldX / worldSize * imageWidth
pixelY = imageHeight - (worldY / worldSize * imageHeight)
```

## Metadata

`meta.json` is the main file a web map server should read first. Important fields:

- `worldName`: stable lowercase map id.
- `displayName`: human-readable name.
- `worldSize`: map size in meters.
- `latitude`, `longitude`: configured world location.
- `gridOffsetX`, `gridOffsetY`: Arma grid offset.
- `grids`: Arma grid definitions for zoom-dependent coordinate labels.
- `colorOutside`: optional outside-terrain color.

For an interactive map, treat the map as a simple square local CRS:

- Bounds: `[0, 0]` to `[worldSize, worldSize]`
- Units: meters
- Origin for world coordinates: bottom-left
- Origin for raster tiles: top-left

## Vector Data

`geojson/` contains gzipped GeoJSON feature collections. Exact availability depends on the source map.

Typical layers:

- roads under `geojson/roads/`
- buildings/houses
- forests
- rocks
- locations
- map objects such as churches, fuel stations, towers, ruins, and other icon types

Coordinates are exported in Arma world meters. For display over the raster tiles, use the same local CRS conversion described above.

Recommended web-server behavior:

- Serve raster tiles as static PNGs from `sat/tiles`, `topo/tiles`, and `baked_topo/tiles`.
- Serve GeoJSON either directly with gzip support or preprocess into vector tiles.
- Read `meta.json` to configure map bounds, display name, and coordinate/grid overlays.
- Offer satellite, topo, and baked topo as switchable base layers.
- Draw GeoJSON as optional overlays.

## DEM

`dem.asc.gz` is a gzipped ESRI ASCII raster. It can be used for:

- elevation lookup
- hillshade generation
- contour generation
- terrain profiles

The generated topo raster is intended as a quick built-in base layer. A web backend can create higher-quality topo styling later from `dem.asc.gz` plus GeoJSON if needed.
