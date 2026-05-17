# Output

## 1. Introduction
This specification outlines the structure of the output directory for a single map.

## 2. Overview
```
├── dem.asc.gz
├── geojson
│   ├── bush.geojson.gz
│   ├── [...]
│   └── house.geojson.gz
├── meta.json
├── preview.png
└── sat
    ├── 0
    │   ├── [...]
    │   └── 0.png
    └── 3
        ├── [...]
        └── 0.png
```

## 3. `geojson/` directory
The `geojson/` directory holds the vector data of all objects and locations as multiple gzipped [geojson files](https://en.wikipedia.org/wiki/GeoJSON). Specifics can be found [here](./geojson_spec.md).


## 4. `meta.json`
The `meta.json` includes all important meta data of the map. The format of this file is further specified [here](./metajson_spec.md).  

## 5. `sat/` directory
The `sat/` directory includes the satellite image. Because the full image can easily get over 200MB in size (and large files are always a pain in the ass to deal with) it is always split in 16 tiles.  

Each tile within the `sat/` directory has the following nomenclature `{col}/{row}.png` with `{col}` being the column number and `{row}` the row number of the tile. Row/column numbers start at 0 and the origin is in the top left corner of the sat-image. So the image `sat/0/0.png` is the most top left tile of the satellite image.   

The `sat/tiles/` directory contains an additional zoom pyramid in the form `{zoom}/{col}/{row}.png`. Tiles are 256 by 256 pixels. Zoom `0` is the most zoomed-out overview level. Each following zoom level doubles the image dimensions, so higher zoom numbers are more zoomed in.

`sat_dark/tiles/` contains the dark-mode satellite equivalent. `baked_sat/tiles/` and `baked_sat_dark/tiles/` contain satellite rasters with available WRP-native map features burned into the image.

Take a look at the following image for a visual representation:  
![](./assets/sat_tiles.svg)  
  
## 6. `topo/` directory
The `topo/` directory includes a generated topographic raster image based on the map's elevation data.

The `topo/tiles/` directory contains generated topographic tiles with the same zoom pyramid layout as the satellite zoom tiles: `{zoom}/{col}/{row}.png`. The maximum zoom level is rendered at 1 pixel per meter. `topo_dark/tiles/` contains the dark-mode topographic equivalent.

## 7. `baked_topo/` directory
The `baked_topo/` directory includes a rendered topographic raster with available map features baked into the image.

The `baked_topo/tiles/` directory contains generated baked topographic tiles with the same zoom pyramid layout as the satellite zoom tiles: `{zoom}/{col}/{row}.png`. `baked_topo_dark/tiles/` contains the dark-mode baked topographic equivalent.

The baked image starts from the generated topographic raster and burns available WRP-native map features into it, currently including building polygons, road network lines, powerline segments, and river polygons.

## 8. `arma_topo/` directory
The `arma_topo/source/` directory contains the raw Arma diagnostic SVG map export when `Export Arma map SVG topo source` is enabled and the selected world is the currently loaded diagnostic world.

This SVG is the source artifact for OCAP-style rendering. It contains Arma's own paper-map layers such as roads, forests, contour lines, labels, and object symbols.

When Python, Inkscape, GDAL, and ImageMagick are available on PATH, the exporter processes that SVG and `dem.asc.gz` into:

- `arma_topo/tiles/{zoom}/{col}/{row}.png`
- `arma_topo_dark/tiles/{zoom}/{col}/{row}.png`
- `arma_topo_relief/tiles/{zoom}/{col}/{row}.png`
- `arma_color_relief/tiles/{zoom}/{col}/{row}.png`

## 9. `dem.asc.gz`
The `dem.asc.gz` includes the [digital elevation model](https://en.wikipedia.org/wiki/Digital_elevation_model) of the map. The file is a gzipped ascii file and in the [ESRI ASCII Raster Format](https://desktop.arcgis.com/de/arcmap/10.3/manage-data/raster-and-images/esri-ascii-raster-format.htm). 

## 10. `preview.png`
The `preview.png` is the map's preview image (shown in the in-game map selection screen of the editor).
