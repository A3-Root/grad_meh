#include <intercept.hpp>

#include "util.h"
#include "geojsons.h"
#include "satimages.h"
#include "topoimages.h"

#include "version.h"

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/filesystem.h>

// String
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

// Gzip
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

// #include <rust-lib/foo/>
#include <rust-lib/lib.h>

#include <sstream>
#include <algorithm>
#include <array>
#include <array>
#include <fstream>
#include <iostream>
#include <list>
#include <vector>
#include <filesystem>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include <ogr_geometry.h>

#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

#include "findPbos.h"
#include "SimplePoint.h"

#include "../addons/main/status_codes.hpp"

using namespace intercept;
using namespace OIIO;

namespace fs = std::filesystem;
namespace nl = nlohmann;
namespace ba = boost::algorithm;
namespace bi = boost::iostreams;
using iet = types::game_state::game_evaluator::evaluator_error_type;

using SQFPar = game_value_parameter;

static bool gradMehIsRunning = false;

namespace {
std::string quoteCommandArg(const fs::path& path)
{
    auto value = path.string();
    ba::replace_all(value, "\"", "\\\"");
    return "\"" + value + "\"";
}

std::string quoteCommandArg(const std::string& value)
{
    auto escaped = value;
    ba::replace_all(escaped, "\"", "\\\"");
    return "\"" + escaped + "\"";
}

const char* armaTopoProcessorScript = R"PY(
import gzip
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SVG_NS = "{http://www.w3.org/2000/svg}"

def run(args):
    print("[grad_meh arma_topo]", " ".join(str(a) for a in args))
    subprocess.run([str(a) for a in args], check=True)

def find_tool(name):
    found = shutil.which(name)
    if found:
        return found
    raise RuntimeError(f"Required tool '{name}' was not found on PATH")

def get_items(root, category_id, item_type):
    group = root.find(f"./{SVG_NS}g[@id='{category_id}']")
    if group is None:
        return []
    return group.findall(f".//{SVG_NS}{item_type}")

def remove_groups(root, keep_terrain):
    for group in list(root.findall(f"./{SVG_NS}g")):
        is_terrain = group.get("id") == "terrain"
        if (keep_terrain and not is_terrain) or ((not keep_terrain) and is_terrain):
            root.remove(group)

def preprocess_svg(in_file, out_file):
    tree = ET.parse(in_file)
    root = tree.getroot()
    for polyline in get_items(root, "countLines", "polyline"):
        polyline.set("fill", "none")
    for polyline in get_items(root, "roads", "polyline"):
        polyline.set("fill", "none")
    for polyline in get_items(root, "airports", "polyline"):
        polyline.set("fill", "none")
    for ellipse in get_items(root, "objects", "ellipse"):
        ellipse.set("fill", "none")
        ellipse.set("stroke", "url(#colorForestBorder)")
        ellipse.set("rx", "6.00")
        ellipse.set("ry", "6.00")
    for polygon in get_items(root, "forests", "polygon"):
        polygon.set("fill-opacity", "0.2")
        polygon.set("fill", "#3e6e30")
    for text in get_items(root, "mountains", "text"):
        text.set("font-size", "10px")
        text.set("font-family", "Arial")
    for text in get_items(root, "townNames", "text"):
        text.set("font-size", "24px")
        text.set("font-family", "Arial")
    tree.write(out_file, encoding="utf-8", xml_declaration=True)

def make_dark_svg(in_file, out_file):
    tree = ET.parse(in_file)
    root = tree.getroot()
    for polyline in get_items(root, "countLines", "polyline"):
        polyline.set("opacity", "0.5")
    for land in get_items(root, "terrain", "polygon"):
        land.set("fill", "#1d2b20")
    for sea in get_items(root, "terrain", "rect"):
        sea.set("fill", "#303d6e")
    for text in get_items(root, "townNames", "text"):
        text.set("fill", "#EEEEEE")
        text.set("stroke", "#0f0f0f")
        text.set("stroke-width", "1px")
        text.set("font-weight", "bold")
    for text in get_items(root, "mountains", "text"):
        text.set("fill", "#EEEEEE")
    tree.write(out_file, encoding="utf-8", xml_declaration=True)

def make_layer_svg(in_file, out_file, keep_terrain):
    tree = ET.parse(in_file)
    remove_groups(tree.getroot(), keep_terrain)
    tree.write(out_file, encoding="utf-8", xml_declaration=True)

def main():
    if len(sys.argv) != 4:
        raise RuntimeError("usage: process_arma_topo.py <map_dir> <world_name> <world_size>")

    map_dir = Path(sys.argv[1])
    world_name = sys.argv[2].lower()
    world_size = int(float(sys.argv[3]))
    source_svg = map_dir / "arma_topo" / "source" / f"{world_name}.svg"
    source_dem = map_dir / "dem.asc.gz"
    if not source_svg.exists():
        raise RuntimeError(f"Missing SVG source: {source_svg}")
    if not source_dem.exists():
        raise RuntimeError(f"Missing DEM source: {source_dem}")

    inkscape = find_tool("inkscape")
    magick = find_tool("magick")
    gdaldem = find_tool("gdaldem")
    gdal2tiles = shutil.which("gdal2tiles.py") or shutil.which("gdal2tiles")
    if not gdal2tiles:
        raise RuntimeError("Required tool 'gdal2tiles.py' or 'gdal2tiles' was not found on PATH")

    image_size = min(max(world_size, 1024), 32768)
    zoom_level = 3
    if image_size >= 2560: zoom_level = 4
    if image_size >= 5120: zoom_level = 5
    if image_size >= 10240: zoom_level = 6
    if image_size >= 16400: zoom_level = 7
    if image_size >= 32768: zoom_level = 8

    temp_dir = map_dir / "arma_topo" / "temp"
    temp_dir.mkdir(parents=True, exist_ok=True)
    proc_svg = temp_dir / f"{world_name}.svg"
    dark_svg = temp_dir / f"{world_name}_dark.svg"
    land_svg = temp_dir / f"{world_name}_landonly.svg"
    noland_svg = temp_dir / f"{world_name}_noland.svg"
    topo_png = temp_dir / f"{world_name}_topo.png"
    dark_png = temp_dir / f"{world_name}_topo_dark.png"
    land_png = temp_dir / f"{world_name}_landonly.png"
    noland_png = temp_dir / f"{world_name}_noland.png"
    dem_asc = temp_dir / f"{world_name}.asc"
    hillshade = temp_dir / f"{world_name}_hillshade.png"
    hillshade_half = temp_dir / f"{world_name}_hillshade_half.png"
    topo_relief = temp_dir / f"{world_name}_topo_relief.png"
    color_relief = temp_dir / f"{world_name}_color_relief.png"
    palette = temp_dir / "color_relief.cpt"

    with gzip.open(source_dem, "rb") as src, open(dem_asc, "wb") as dst:
        shutil.copyfileobj(src, dst)

    palette.write_text("-450 35 60 92\n0 128 170 198\n1 184 208 173\n150 214 207 166\n350 221 198 156\n600 235 232 214\nnv 0 0 0 0\n", encoding="utf-8")

    preprocess_svg(source_svg, proc_svg)
    make_dark_svg(proc_svg, dark_svg)
    make_layer_svg(proc_svg, land_svg, True)
    make_layer_svg(proc_svg, noland_svg, False)

    for svg, png in [(proc_svg, topo_png), (dark_svg, dark_png), (land_svg, land_png), (noland_svg, noland_png)]:
        run([inkscape, "-o", png, f"--export-height={image_size}", f"--export-width={image_size}", svg])
        run([magick, png, "-alpha", "off", png])

    run([gdaldem, "hillshade", "-alg", "Horn", "-alt", "45", "-multidirectional", "-of", "PNG", dem_asc, hillshade])
    run([magick, hillshade, "-resize", f"{image_size}x{image_size}", hillshade])
    run([magick, hillshade, "-alpha", "set", "-channel", "a", "-evaluate", "set", "50%", hillshade_half])
    run([magick, land_png, hillshade_half, "-compose", "multiply", "-composite", topo_relief])
    run([magick, topo_relief, noland_png, "-composite", topo_relief])

    run([gdaldem, "color-relief", "-of", "PNG", dem_asc, palette, color_relief])
    run([magick, color_relief, "-resize", f"{image_size}x{image_size}", color_relief])
    run([magick, color_relief, hillshade_half, "-compose", "multiply", "-composite", color_relief])
    run([magick, color_relief, noland_png, "-composite", color_relief])

    layers = [
        (topo_png, map_dir / "arma_topo" / "tiles"),
        (dark_png, map_dir / "arma_topo_dark" / "tiles"),
        (topo_relief, map_dir / "arma_topo_relief" / "tiles"),
        (color_relief, map_dir / "arma_color_relief" / "tiles"),
    ]
    for image, target in layers:
        if target.exists():
            shutil.rmtree(target)
        target.mkdir(parents=True, exist_ok=True)
        run([gdal2tiles, "-p", "raster", "--xyz", "-z", f"0-{zoom_level}", "-w", "none", "-r", "lanczos", image, target])

    metadata = {
        "worldName": world_name,
        "worldSize": world_size,
        "imageSize": image_size,
        "maxZoom": zoom_level,
        "layers": ["arma_topo", "arma_topo_dark", "arma_topo_relief", "arma_color_relief"],
    }
    (map_dir / "arma_topo" / "process_meta.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
)PY";

bool processArmaTopo(const fs::path& basePath, const std::string& lowerWorldName, const int32_t worldSize)
{
    const auto toolsPath = fs::path("grad_meh") / "tools";
    if (!fs::exists(toolsPath)) {
        fs::create_directories(toolsPath);
    }

    const auto scriptPath = toolsPath / "process_arma_topo.py";
    {
        std::ofstream scriptOut(scriptPath, std::ios::binary);
        scriptOut << armaTopoProcessorScript;
    }

    const auto dockerCommand = "docker run --rm"
        " -v " + quoteCommandArg(fs::absolute(basePath).string() + ":/data/map")
        + " -v " + quoteCommandArg(fs::absolute(scriptPath).string() + ":/app/process_arma_topo.py:ro")
        + " grad-meh-arma-topo:latest"
        + " python3 /app/process_arma_topo.py /data/map"
        + " " + quoteCommandArg(lowerWorldName)
        + " " + quoteCommandArg(std::to_string(worldSize));

    const auto result = std::system(dockerCommand.c_str());
    if (result != 0) {
        PLOG_ERROR << fmt::format("Docker Arma topo processor failed with exit code {}", result);
        return false;
    }
    return true;
}
}

int intercept::api_version() { // This is required for the plugin to work.
    return INTERCEPT_SDK_API_VERSION;
}

void writeMeta(const std::string &worldName, const int32_t &worldSize, fs::path &basePath)
{
    client::invoker_lock threadLock;
    auto mapConfig = sqf::config_entry(sqf::config_file()) >> "CfgWorlds" >> worldName;
    nl::json meta;
    meta["version"] = GRAD_MEH_GIT_LAST_VERSION;
    meta["worldName"] = ba::to_lower_copy(worldName);
    meta["worldSize"] = worldSize;
    meta["author"] = sqf::get_text(mapConfig >> "author");
    meta["displayName"] = sqf::get_text(mapConfig >> "description");
    meta["elevationOffset"] = sqf::get_number(mapConfig >> "elevationOffset");
    meta["latitude"] = sqf::get_number(mapConfig >> "latitude");
    meta["longitude"] = sqf::get_number(mapConfig >> "longitude");
    meta["gridOffsetX"] = sqf::get_number(mapConfig >> "Grid" >> "offsetX");
    meta["gridOffsetY"] = sqf::get_number(mapConfig >> "Grid" >> "offsetY");

    auto colorArray = sqf::get_array(mapConfig >> "OutsideTerrain" >> "colorOutside").to_array();
    if (!colorArray.empty())
    {
        meta["colorOutside"] = std::vector<float_t>(colorArray.begin(), colorArray.end());
    }

    auto gridArray = nl::json::array();

    for (auto &grid : sqf::config_classes("true", (mapConfig >> "Grid")))
    {
        auto entry = sqf::config_entry(grid);

        nl::json entryGrid;
        entryGrid["zoomMax"] = sqf::get_number(entry >> "zoomMax");
        entryGrid["format"] = sqf::get_text(entry >> "format");
        entryGrid["formatX"] = sqf::get_text(entry >> "formatX");
        entryGrid["formatY"] = sqf::get_text(entry >> "formatY");
        entryGrid["stepX"] = (int32_t)sqf::get_number(entry >> "stepX");
        entryGrid["stepY"] = (int32_t)sqf::get_number(entry >> "stepY");

        gridArray.push_back(entryGrid);
    }

    meta["grids"] = gridArray;
    threadLock.unlock();

    std::ofstream out(basePath / "meta.json");
    out << std::setw(4) << meta << std::endl;
    out.close();
}

void writeDem(fs::path &basePath, arma_file_formats::cxx::OprwCxx &wrp, const int32_t &worldSize)
{
    auto cellsize = (float_t)worldSize / wrp.map_size_x;

    std::stringstream demStringStream;
    demStringStream << "ncols " << wrp.map_size_x << std::endl;
    demStringStream << "nrows " << wrp.map_size_y << std::endl;
    demStringStream << "xllcorner " << 0.0 << std::endl;
    demStringStream << "yllcorner " << -cellsize << std::endl;
    demStringStream << "cellsize " << cellsize << std::endl; // worldSize / mapsizex
    demStringStream << "NODATA_value " << -9999;
    demStringStream << std::endl;

    for (int64_t y = wrp.map_size_y - 1; y >= 0; y--)
    {
        for (size_t x = 0; x < wrp.map_size_x; x++)
        {
            demStringStream << wrp.elevation[x + wrp.map_size_x * y] << " ";
        }
        demStringStream << std::endl;
    }

    bi::filtering_istream fis;
    fis.push(bi::gzip_compressor(bi::gzip_params(bi::gzip::best_compression)));
    fis.push(demStringStream);

    std::ofstream demOut(basePath / "dem.asc.gz", std::ios::binary);
    bi::copy(fis, demOut);
    demOut.close();
}

bool writePreviewImage(const std::string &worldName, std::filesystem::path &basePath)
{
    client::invoker_lock threadLock;
    auto mapConfig = sqf::config_entry(sqf::config_file()) >> "CfgWorlds" >> worldName;
    std::string configPicturePath = sqf::get_text(mapConfig >> "pictureMap");
    threadLock.unlock();

    if (configPicturePath.empty()) {
        return true;
    }

    if (boost::starts_with(configPicturePath, "\\")) {
        configPicturePath = configPicturePath.substr(1);
    }

    auto pboPath = findPboPath(configPicturePath);
    if (pboPath.empty()) {
        return true;
    }
    try {
        auto previewPbo = arma_file_formats::cxx::create_pbo_reader_path(pboPath.string());
        auto previewData = previewPbo->get_entry_data(configPicturePath);
        auto previewFileName = (basePath / "preview.png").string();

        auto picturePath = fs::path(configPicturePath);
        auto ext = picturePath.extension().string();
        boost::algorithm::to_lower(ext);

        std::unique_ptr<ImageOutput> out = ImageOutput::create(previewFileName);
        if (!out) {
            auto msg = fmt::format("Could not create preview picture: {}", previewFileName);
            PLOG_ERROR << msg;
            prettyDiagLog(msg);
            return false;
        }

        if (ext == ".paa") {
            auto previewMipmap = arma_file_formats::cxx::get_mipmap_from_paa_vec(previewData, 0);

            ImageSpec spec(previewMipmap.width, previewMipmap.height, 4, TypeDesc::UINT8);
            out->open(previewFileName, spec);
            out->write_image(TypeDesc::UINT8, previewMipmap.data.data());
        } else {
			Filesystem::IOMemReader ioMemReader(previewData.data(), previewData.size());
            auto img = ImageInput::open(picturePath.filename().string(), nullptr, &ioMemReader);
            if (!img) {
                auto msg = fmt::format("Could not open non paa preview picture: {}", configPicturePath);
                PLOG_ERROR << msg;
                prettyDiagLog(msg);
                return false;
            }

            ImageSpec imgBufSpec = img->spec();

            std::vector<uint8_t> pixels(imgBufSpec.width * imgBufSpec.height * imgBufSpec.nchannels);
            img->read_image(TypeDesc::UINT8, pixels.data());
            img->close();

            ImageSpec spec(imgBufSpec.width, imgBufSpec.height, imgBufSpec.nchannels, TypeDesc::UINT8);
            out->open(previewFileName, spec);
            out->write_image(TypeDesc::UINT8, pixels.data());
        }
        out->close();
        return true;
    }
    catch (const rust::Error& ex) {
        auto msg = fmt::format("Failed to write preview image ({}): {}", configPicturePath, ex.what());
        PLOG_ERROR << msg;
        prettyDiagLog(msg);
        return false;
    }
    catch (const std::exception& ex) {
        auto msg = fmt::format("Failed to write preview image ({}): {}", configPicturePath, ex.what());
        PLOG_ERROR << msg;
        prettyDiagLog(msg);
        return false;
    }
    catch (...) {
        auto msg = fmt::format("Failed to write preview image ({}): unknown error", configPicturePath);
        PLOG_ERROR << msg;
        prettyDiagLog(msg);
        return false;
    }
}

void extractMap(const std::string &worldName, const std::string &worldPath, std::array<bool, 8> &steps)
{

    auto lowerWorldName = boost::algorithm::to_lower_copy(worldName);

    auto basePath = fs::path("grad_meh") / lowerWorldName;
    auto basePathGeojson = fs::path("grad_meh") / lowerWorldName / "geojson";
    auto basePathSat = fs::path("grad_meh") / lowerWorldName / "sat";
    auto basePathTopo = fs::path("grad_meh") / lowerWorldName / "topo";
    auto basePathTopoDark = fs::path("grad_meh") / lowerWorldName / "topo_dark";
    auto basePathBakedTopo = fs::path("grad_meh") / lowerWorldName / "baked_topo";
    auto basePathBakedTopoDark = fs::path("grad_meh") / lowerWorldName / "baked_topo_dark";

    std::stringstream startMsg;
    startMsg << "Starting export of " << worldName << " [";
    startMsg << std::boolalpha;

    for (auto i = 0; i < steps.size(); i++)
    {
        startMsg << steps[i];
        if (i < (steps.size() - 1))
        {
            startMsg << ", ";
        }
    }
    startMsg << "]";

    prettyDiagLog(startMsg.str());
    PLOG_INFO << startMsg.str();

    if (!fs::exists(basePath))
    {
        fs::create_directories(basePath);
    }
    if (!fs::exists(basePathGeojson))
    {
        fs::create_directories(basePathGeojson);
    }
    if (!fs::exists(basePathSat))
    {
        fs::create_directories(basePathSat);
    }
    if (!fs::exists(basePathTopo))
    {
        fs::create_directories(basePathTopo);
    }
    if (!fs::exists(basePathTopoDark))
    {
        fs::create_directories(basePathTopoDark);
    }
    if (!fs::exists(basePathBakedTopo))
    {
        fs::create_directories(basePathBakedTopo);
    }
    if (!fs::exists(basePathBakedTopoDark))
    {
        fs::create_directories(basePathBakedTopoDark);
    }

    std::string curWorldPath = "";
    try {
        // Find Wrp Path
        auto wrpPath = findPboPath(worldPath);
        curWorldPath = wrpPath.string();
        auto wrpPboReader = arma_file_formats::cxx::create_pbo_reader_path(wrpPath.string());

        auto wrp_data = wrpPboReader->get_entry_data(worldPath);

        auto wrp = arma_file_formats::cxx::OprwCxx{};
        // wrp.wrpName = worldName + ".wrp";

        if (steps[0] || steps[1] || steps[2] || steps[3] || steps[5] || steps[6] || steps[7])
        {
            reportStatus(worldName, "read_wrp", "running");
            wrp = arma_file_formats::cxx::create_wrp_from_vec(wrp_data);
            reportStatus(worldName, "read_wrp", "done");
        }
        else
        {
            reportStatus(worldName, "read_wrp", "canceled");
        }

        auto worldSize = (uint32_t)wrp.layer_cell_size * wrp.layer_size_x;

        if (steps[0])
        {
            reportStatus(worldName, "write_sat", "running");
            prettyDiagLog("Exporting sat images");
            writeSatImages(wrp, worldSize, basePathSat, worldName);
            reportStatus(worldName, "write_sat", "done");
        }
        if (steps[1])
        {
            reportStatus(worldName, "write_topo", "running");
            prettyDiagLog("Exporting topographic images");
            writeTopoImageSet(wrp, basePathTopo, basePathTopoDark);
            reportStatus(worldName, "write_topo", "done");
        }
        if (steps[2])
        {
            reportStatus(worldName, "write_baked_topo", "running");
            prettyDiagLog("Exporting baked topographic images");
            writeBakedTopoImageSet(wrp, basePathBakedTopo, basePathBakedTopoDark);
            reportStatus(worldName, "write_baked_topo", "done");
        }
        if (steps[3])
        {
            reportStatus(worldName, "write_houses", "running");
            prettyDiagLog("Exporting geojson");
            writeGeojsons(wrp, basePathGeojson, worldName);
            reportStatus(worldName, "write_houses", "done");
        }

        if (steps[4])
        {
            reportStatus(worldName, "write_preview", "running");
            prettyDiagLog("Exporting preview image");
            if (writePreviewImage(worldName, basePath)) {
                reportStatus(worldName, "write_preview", "done");
            } else {
                reportStatus(worldName, "write_preview", "canceled");
            }
        }

        if (steps[5])
        {
            reportStatus(worldName, "write_meta", "running");
            prettyDiagLog("Exporting meta json");
            writeMeta(worldName, worldSize, basePath);
            reportStatus(worldName, "write_meta", "done");
        }

        if (steps[6])
        {
            reportStatus(worldName, "write_dem", "running");
            prettyDiagLog("Exporting dem file");
            writeDem(basePath, wrp, worldSize);
            reportStatus(worldName, "write_dem", "done");
        }

        if (steps[7])
        {
            reportStatus(worldName, "write_arma_topo", "running");
            prettyDiagLog("Processing Arma diagnostic SVG topographic tiles");
            if (!fs::exists(basePath / "dem.asc.gz")) {
                prettyDiagLog("Writing dem file required by Arma topographic processor");
                writeDem(basePath, wrp, worldSize);
            }
            if (processArmaTopo(basePath, lowerWorldName, worldSize)) {
                reportStatus(worldName, "write_arma_topo", "done");
            } else {
                reportStatus(worldName, "write_arma_topo", "canceled");
            }
        }
    }
    catch (const rust::Error& ex) {
        PLOG_ERROR << "Exception in extract map command";
        PLOG_ERROR << fmt::format("WRP Path: {}", curWorldPath);
        PLOG_ERROR << ex.what();
        throw;
    }

    gradMehIsRunning = false;
    return;
}

game_value exportRunningCommand(game_state &gs) {
    return gradMehIsRunning;
}

game_value prepareArmaTopoSvgCommand(game_state &gs, SQFPar rightArg)
{
    if (rightArg.type_enum() != game_data_type::STRING)
    {
        gs.set_script_error(iet::assertion_failed, "Expected a world name string!"sv);
        return "";
    }

    auto worldName = ba::to_lower_copy(static_cast<std::string>(r_string(rightArg)));
    auto sourcePath = fs::path("grad_meh") / worldName / "arma_topo" / "source";
    if (!fs::exists(sourcePath))
    {
        fs::create_directories(sourcePath);
    }

    return (sourcePath / (worldName + ".svg")).string();
}

game_value exportMapCommand(game_state &gs, SQFPar rightArg)
{

    if (gradMehIsRunning)
        return GRAD_MEH_STATUS_ERR_ALREADY_RUNNING;

    if (isMapPopulating())
        return GRAD_MEH_STATUS_ERR_PBO_POPULATING;

    std::string worldName;

    // [sat image, topo image, baked topo image, houses, preview img, meta.json, dem.asc, Arma topo]
    std::array<bool, 8> steps = { true, true, true, true, true, true, true, true };

    if (rightArg.type_enum() == game_data_type::STRING)
    {
        worldName = r_string(rightArg).c_str();
    }
    else if (rightArg.type_enum() == game_data_type::ARRAY)
    {
        auto parArray = rightArg.to_array();

        if (parArray.size() <= 0 || parArray.size() >= 10)
        {
            gs.set_script_error(iet::assertion_failed, "Wrong amount of arguments!"sv);
            return GRAD_MEH_STATUS_ERR_ARGS;
        }

        if (parArray[0].type_enum() == game_data_type::STRING)
        {

            worldName = r_string(parArray[0]);
            for (int i = 1; i < parArray.size(); i++)
            {
                if (parArray[i].type_enum() == game_data_type::BOOL)
                {
                    auto b = (bool)parArray[i];
                    steps[i - 1] = b;
                }
                else
                {
                    gs.set_script_error(iet::assertion_failed, types::r_string("Expected bool at index ").append(std::to_string(i)).append("!"));
                    return GRAD_MEH_STATUS_ERR_ARGS;
                }
            }
        }
        else
        {
            gs.set_script_error(iet::assertion_failed, "First element in the parameter array has to be a string!"sv);
            return GRAD_MEH_STATUS_ERR_ARGS;
        }
    }
    else
    {
        gs.set_script_error(iet::assertion_failed, "Expected a string or an array!"sv);
        return GRAD_MEH_STATUS_ERR_ARGS;
    }

    auto configWorld = sqf::config_entry(sqf::config_file()) >> "CfgWorlds" >> worldName;
    if (!boost::iequals(sqf::config_name(configWorld), worldName))
    {
        gs.set_script_error(iet::assertion_failed, "Couldn't find the specified world!"sv);
        return GRAD_MEH_STATUS_ERR_NOT_FOUND;
    }

    // check for leading /
    std::string worldPath = sqf::get_text(configWorld >> "worldName");
    if (boost::starts_with(worldPath, "\\"))
    {
        worldPath = worldPath.substr(1);
    }

    // try to find pbo
    auto wrpPboPath = findPboPath(worldPath);
    if (wrpPboPath == "")
    {
        return GRAD_MEH_STATUS_ERR_PBO_NOT_FOUND;
    }
    else
    {
        try
        {
            auto wrpPboReader = arma_file_formats::cxx::create_pbo_reader_path(wrpPboPath.string());
            if (!wrpPboReader->has_entry(worldPath))
                return GRAD_MEH_STATUS_ERR_PBO_NOT_FOUND;
        }
        catch (std::exception &ex)
        {
            prettyDiagLog(std::string("Exception when opening PBO: ").append(ex.what()));
            return GRAD_MEH_STATUS_ERR_PBO_NOT_FOUND;
        }
    }

    if (!gradMehIsRunning)
    {
        gradMehIsRunning = true;
        std::thread readWrpThread(extractMap, worldName, worldPath, steps);
        readWrpThread.detach();
        return GRAD_MEH_STATUS_OK;
    }
    else
    {
        prettyDiagLog("gradMeh is already running! Aborting!");
        return GRAD_MEH_STATUS_ERR_ALREADY_RUNNING;
    }
}

types::registered_sqf_function grad_meh_export_map_string;
types::registered_sqf_function grad_meh_export_map_array;
types::registered_sqf_function grad_meh_export_running;
types::registered_sqf_function grad_meh_prepare_arma_topo_svg;

void intercept::pre_start()
{
    grad_meh_export_map_string =
        client::host::register_sqf_command("gradMehExportMap", "Exports the given map", exportMapCommand, game_data_type::SCALAR, game_data_type::STRING);
    grad_meh_export_map_array =
        client::host::register_sqf_command("gradMehExportMap", "Exports the given map", exportMapCommand, game_data_type::SCALAR, game_data_type::ARRAY);
    grad_meh_export_running =
        client::host::register_sqf_command("gradMehExportRunning", "Check if an export is currently running", exportRunningCommand, game_data_type::BOOL);
    grad_meh_prepare_arma_topo_svg =
        client::host::register_sqf_command("gradMehPrepareArmaTopoSvg", "Prepare the Arma diagnostic SVG output path for a map", prepareArmaTopoSvgCommand, game_data_type::STRING, game_data_type::STRING);

#if defined(_WIN32)
    std::filesystem::path a3_log_path;
    PWSTR path_tmp;

    auto get_folder_path_ret = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path_tmp);

    if (get_folder_path_ret == S_OK) {
        a3_log_path = path_tmp;
    }
    CoTaskMemFree(path_tmp);

    a3_log_path = a3_log_path / "Arma 3";

#endif

#if _DEBUG
    auto severity = plog::Severity::debug;
#else
    auto severity = plog::Severity::warning;
#endif

    plog::init(severity, fmt::format("{}/grad_meh_{:%Y-%m-%d_%H-%M-%S}.log", a3_log_path.string(), std::chrono::system_clock::now()).c_str());

    PLOG_INFO << "Starting PBO Mapping";

    std::thread mapPopulateThread(populateMap);
    mapPopulateThread.detach();
}

void intercept::pre_init()
{
    std::stringstream preInitMsg;

    preInitMsg << "The grad_meh plugin is running! (";

    if (GRAD_MEH_GIT_TAG.empty())
    {
        preInitMsg << GRAD_MEH_GIT_REV;
    }
    else
    {
        preInitMsg << GRAD_MEH_GIT_TAG;
    }

    preInitMsg << "@" << GRAD_MEH_GIT_BRANCH << ")";
    intercept::sqf::system_chat(preInitMsg.str());
    prettyDiagLog(preInitMsg.str());

    PLOG_INFO << preInitMsg.str();

}
