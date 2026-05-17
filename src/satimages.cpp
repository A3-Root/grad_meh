#include "satimages.h"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

using namespace OpenImageIO_v2_5;

// Range TODO: replace with C++20 Range
#include <boost/range/counting_range.hpp>

#include <boost/algorithm/string/join.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <cmath>

namespace {
struct PixelPoint {
    int32_t x = 0;
    int32_t y = 0;
};

PixelPoint worldToPixel(const arma_file_formats::cxx::OprwCxx& wrp, const int32_t width, const int32_t height, const float worldX, const float worldY)
{
    const auto worldSize = static_cast<float>(wrp.layer_cell_size * wrp.layer_size_x);

    return {
        std::clamp(static_cast<int32_t>((worldX / worldSize) * width), 0, width - 1),
        std::clamp(static_cast<int32_t>(height - ((worldY / worldSize) * height)), 0, height - 1)
    };
}

void blendPixel(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const int32_t x, const int32_t y, const std::array<uint8_t, 4>& color, const float alpha)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    const auto clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
    const auto offset = (static_cast<size_t>(y) * width + x) * 4;
    for (auto channel = 0; channel < 3; channel++) {
        pixels[offset + channel] = static_cast<uint8_t>(std::round(
            pixels[offset + channel] * (1.0f - clampedAlpha) + color[channel] * clampedAlpha));
    }
    pixels[offset + 3] = 255;
}

void drawLineBlended(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, PixelPoint a, PixelPoint b, const std::array<uint8_t, 4>& color, const int32_t thickness, const float alpha)
{
    auto dx = std::abs(b.x - a.x);
    auto sx = a.x < b.x ? 1 : -1;
    auto dy = -std::abs(b.y - a.y);
    auto sy = a.y < b.y ? 1 : -1;
    auto err = dx + dy;

    while (true) {
        for (int32_t ox = -thickness; ox <= thickness; ox++) {
            for (int32_t oy = -thickness; oy <= thickness; oy++) {
                blendPixel(pixels, width, height, a.x + ox, a.y + oy, color, alpha);
            }
        }

        if (a.x == b.x && a.y == b.y) {
            break;
        }

        const auto e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            a.x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            a.y += sy;
        }
    }
}

bool pointInPolygon(const std::vector<PixelPoint>& polygon, const int32_t x, const int32_t y)
{
    auto inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& pi = polygon[i];
        const auto& pj = polygon[j];
        if (pj.y == pi.y) {
            continue;
        }
        if (((pi.y > y) != (pj.y > y)) && (x < (pj.x - pi.x) * (y - pi.y) / static_cast<float>(pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

void fillPolygonBlended(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const std::vector<PixelPoint>& polygon, const std::array<uint8_t, 4>& fill, const float fillAlpha, const std::array<uint8_t, 4>& outline, const float outlineAlpha)
{
    if (polygon.size() < 3) {
        return;
    }

    auto minX = width - 1;
    auto minY = height - 1;
    auto maxX = 0;
    auto maxY = 0;
    for (const auto& point : polygon) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    for (auto y = minY; y <= maxY; y++) {
        for (auto x = minX; x <= maxX; x++) {
            if (pointInPolygon(polygon, x, y)) {
                blendPixel(pixels, width, height, x, y, fill, fillAlpha);
            }
        }
    }

    for (size_t i = 0; i < polygon.size(); i++) {
        drawLineBlended(pixels, width, height, polygon[i], polygon[(i + 1) % polygon.size()], outline, 1, outlineAlpha);
    }
}

std::vector<uint8_t> imageToPixels(const ImageBuf& image)
{
    const auto& spec = image.spec();
    std::vector<uint8_t> pixels(static_cast<size_t>(spec.width) * spec.height * spec.nchannels);
    image.get_pixels(ROI(0, spec.width, 0, spec.height, 0, 1, 0, spec.nchannels), TypeDesc::UINT8, pixels.data());
    return pixels;
}

ImageBuf pixelsToImage(const std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const int32_t channels)
{
    return ImageBuf(ImageSpec(width, height, channels, TypeDesc::UINT8), const_cast<uint8_t*>(pixels.data()));
}

std::vector<uint8_t> makeDarkSatellitePixels(const ImageBuf& image)
{
    auto pixels = imageToPixels(image);
    const auto channels = image.spec().nchannels;
    for (size_t offset = 0; offset + 2 < pixels.size(); offset += channels) {
        const auto luminance = static_cast<float>(pixels[offset + 0]) * 0.2126f
            + static_cast<float>(pixels[offset + 1]) * 0.7152f
            + static_cast<float>(pixels[offset + 2]) * 0.0722f;
        pixels[offset + 0] = static_cast<uint8_t>(std::clamp(luminance * 0.18f + 18.0f, 0.0f, 255.0f));
        pixels[offset + 1] = static_cast<uint8_t>(std::clamp(luminance * 0.20f + 22.0f, 0.0f, 255.0f));
        pixels[offset + 2] = static_cast<uint8_t>(std::clamp(luminance * 0.24f + 28.0f, 0.0f, 255.0f));
        if (channels > 3) {
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

void applyBakedSatelliteOverlays(arma_file_formats::cxx::OprwCxx& wrp, std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const bool dark)
{
    const auto buildingFill = dark ? std::array<uint8_t, 4>{ 160, 150, 128, 255 } : std::array<uint8_t, 4>{ 238, 226, 202, 255 };
    const auto buildingOutline = dark ? std::array<uint8_t, 4>{ 218, 204, 170, 255 } : std::array<uint8_t, 4>{ 116, 106, 92, 255 };
    const auto roadColor = dark ? std::array<uint8_t, 4>{ 210, 202, 184, 255 } : std::array<uint8_t, 4>{ 245, 236, 212, 255 };
    const auto powerlineColor = dark ? std::array<uint8_t, 4>{ 176, 182, 190, 255 } : std::array<uint8_t, 4>{ 60, 60, 60, 255 };
    const auto riverFill = dark ? std::array<uint8_t, 4>{ 62, 134, 178, 255 } : std::array<uint8_t, 4>{ 86, 158, 206, 255 };
    const auto riverOutline = dark ? std::array<uint8_t, 4>{ 96, 176, 218, 255 } : std::array<uint8_t, 4>{ 42, 110, 160, 255 };

    for (auto& mapInfo : wrp.map_infos_4) {
        const std::vector<PixelPoint> polygon = {
            worldToPixel(wrp, width, height, mapInfo.bounds.a.x, mapInfo.bounds.a.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.b.x, mapInfo.bounds.b.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.d.x, mapInfo.bounds.d.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.c.x, mapInfo.bounds.c.y)
        };
        fillPolygonBlended(pixels, width, height, polygon, buildingFill, dark ? 0.34f : 0.42f, buildingOutline, dark ? 0.52f : 0.56f);
    }

    for (auto& roadNet : wrp.road_net) {
        for (auto& roadPart : roadNet.road_parts) {
            for (size_t i = 1; i < roadPart.positions.size(); i++) {
                drawLineBlended(
                    pixels,
                    width,
                    height,
                    worldToPixel(wrp, width, height, roadPart.positions[i - 1].x, roadPart.positions[i - 1].z),
                    worldToPixel(wrp, width, height, roadPart.positions[i].x, roadPart.positions[i].z),
                    roadColor,
                    1,
                    dark ? 0.52f : 0.58f);
            }
        }
    }

    for (auto& mapInfo : wrp.map_infos_5) {
        drawLineBlended(
            pixels,
            width,
            height,
            worldToPixel(wrp, width, height, mapInfo.floats[0], mapInfo.floats[1]),
            worldToPixel(wrp, width, height, mapInfo.floats[2], mapInfo.floats[3]),
            powerlineColor,
            1,
            0.48f);
    }

    for (auto& river : wrp.map_info_river) {
        std::vector<PixelPoint> polygon = {};
        for (auto& point : river.polygon) {
            polygon.push_back(worldToPixel(wrp, width, height, point.x, point.y));
        }
        fillPolygonBlended(pixels, width, height, polygon, riverFill, dark ? 0.46f : 0.52f, riverOutline, 0.46f);
    }
}
}

void writeImagePyramid(const ImageBuf& src, const fs::path& basePath, const int32_t tileSize)
{
    if (!fs::exists(basePath)) {
        fs::create_directories(basePath);
    }

    std::vector<ImageBuf> levels;
    auto levelImage = src.copy(TypeDesc::UINT8);

    while (levelImage.spec().width >= tileSize || levelImage.spec().height >= tileSize || levels.empty()) {
        levels.push_back(levelImage.copy(TypeDesc::UINT8));

        const auto width = levelImage.spec().width;
        const auto height = levelImage.spec().height;
        const auto nextWidth = std::max(1, width / 2);
        const auto nextHeight = std::max(1, height / 2);
        if (nextWidth == width && nextHeight == height) {
            break;
        }

        levelImage = ImageBufAlgo::resize(levelImage, "", 0, ROI(0, nextWidth, 0, nextHeight));
    }

    for (int32_t level = static_cast<int32_t>(levels.size()) - 1, z = 0; level >= 0; level--, z++) {
        const auto& image = levels[static_cast<size_t>(level)];
        const auto width = image.spec().width;
        const auto height = image.spec().height;
        const auto cols = static_cast<int32_t>(std::ceil(static_cast<float>(width) / tileSize));
        const auto rows = static_cast<int32_t>(std::ceil(static_cast<float>(height) / tileSize));

        auto zPath = basePath / std::to_string(z);
        if (!fs::exists(zPath)) {
            fs::create_directories(zPath);
        }

        for (int32_t x = 0; x < cols; x++) {
            auto xPath = zPath / std::to_string(x);
            if (!fs::exists(xPath)) {
                fs::create_directories(xPath);
            }

            for (int32_t y = 0; y < rows; y++) {
                ImageBuf tile(ImageSpec(tileSize, tileSize, image.spec().nchannels, TypeDesc::UINT8));
                auto roi = ROI(
                    x * tileSize,
                    std::min((x + 1) * tileSize, width),
                    y * tileSize,
                    std::min((y + 1) * tileSize, height));
                auto cut = ImageBufAlgo::cut(image, roi);
                ImageBufAlgo::paste(tile, 0, 0, 0, 0, cut);
                tile.write((xPath / std::to_string(y).append(".png")).string());
            }
        }
    }
}

struct SatMapTile {
    fs::path path = {};
    TileTransform tt = {};
    arma_file_formats::cxx::MipmapCxx mipmap = {};
};

struct FillerMapTile {
    fs::path path = {};
    std::set<TileTransform, CmpTileTransform> tt = {};
    arma_file_formats::cxx::MipmapCxx mipmap = {};
};

static const std::vector<std::string> texture_config_path    { "Stage0", "texture" };
static const std::vector<std::string> texgen_config_path     { "Stage0", "texGen" };

void writeSatImages(arma_file_formats::cxx::OprwCxx& wrp, const int32_t& worldSize, std::filesystem::path& basePathSat, const std::string& worldName)
{
    std::vector<std::string> rvmats = {};
    for (auto& rv : wrp.texures) {
        if (!rv.texture_filename.empty()) {
            rvmats.push_back(static_cast<std::string>(rv.texture_filename));
        }
    }

    std::sort(rvmats.begin(), rvmats.end());

    if (rvmats.size() > 1) {
        std::vector<SatMapTile> satMapTiles = {};
        satMapTiles.reserve(rvmats.size());

        std::string pboPath = "";
        try {
            pboPath = findPboPath(rvmats[0]).string();
            auto rvmatPbo = arma_file_formats::cxx::create_pbo_reader_path(pboPath);

            // Has to be not empty
            std::string lastValidRvMat = "1337";
            std::optional<FillerMapTile> fillerTile = std::nullopt;
            std::optional<TileTransform> firstPos = std::nullopt;
            std::string prefix = "s_";

            std::optional<rust::box<arma_file_formats::cxx::PboReaderCxx>> pbo = std::nullopt;

            for (auto& rvmatPath : rvmats) {
                if (!boost::istarts_with(((fs::path)rvmatPath).filename().string(), lastValidRvMat)) {
                    PLOG_INFO << fmt::format("Getting data for {}", rvmatPath);
                    auto rap_data = rvmatPbo->get_entry_data(rvmatPath);
                    if (rap_data.empty()) {
                        PLOG_ERROR << "Rvmat data was empty!";
                    }
                    PLOG_INFO << fmt::format("Parsing rvmat {}", rvmatPath);
                    auto rap = arma_file_formats::cxx::create_cfg_vec(rap_data);
                    auto textureStr = static_cast<std::string>(rap->get_entry_as_string(texture_config_path));

                    // Fix wrong file extensions (cup summer has png extension)
                    auto textureStrAsPath = fs::path(textureStr);
                    if (!boost::iequals(textureStrAsPath.extension().string(), ".paa")) {
                        textureStr = textureStrAsPath.replace_extension(".paa").string();
                    }

                    if (!textureStr.empty()) {
                        auto rvmatFilename = ((fs::path)textureStr).filename().string();
                        if (boost::istarts_with(rvmatFilename, prefix)) {
                            lastValidRvMat = rvmatFilename.substr(0, 9);

                            auto tt = getTileTransform(rap);

                            if (!firstPos.has_value()) {
                                firstPos = tt;
                            }

                            if (!pbo.has_value()) {
                                pbo = arma_file_formats::cxx::create_pbo_reader_path(findPboPath(textureStr).string());
                            }

                            auto data = (*pbo)->get_entry_data(textureStr);

                            if (data.empty()) {
                                pbo = arma_file_formats::cxx::create_pbo_reader_path(findPboPath(textureStr).string());
                                data = (*pbo)->get_entry_data(textureStr);
                            }

                            auto mipmap = arma_file_formats::cxx::get_mipmap_from_paa_vec(data, 0);

                            struct SatMapTile tile = { textureStr, tt, std::move(mipmap) };
                            satMapTiles.push_back(std::move(tile));
                        }
                        else {
                            if(!fillerTile.has_value()) {
                                auto fillerPbo = arma_file_formats::cxx::create_pbo_reader_path(findPboPath(textureStr).string());
                                auto fillerData = fillerPbo->get_entry_data(textureStr);
                                auto filler_mm = arma_file_formats::cxx::get_mipmap_from_paa_vec(fillerData, 0);

                                struct FillerMapTile tile = { textureStr, {}, filler_mm };
                                fillerTile = tile;
                            }

                            auto tt = getTileTransform(rap);

                            if (!firstPos.has_value()) {
                                firstPos = tt;
                            }
                            fillerTile->tt.emplace(tt);
                        }
                    }

                }
            }

            // Remove Duplicates
            std::sort(satMapTiles.begin(), satMapTiles.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.path.string() < rhs.path.string();
            });
            auto last = std::unique(satMapTiles.begin(), satMapTiles.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.path == rhs.path;
            });
            satMapTiles.erase(last, satMapTiles.end());

            auto tileSize = 0;

            // Fix wrong file extensions (cup summer has png extension)
            for (auto& smt : satMapTiles) {
                fs::path lcoPathAsPath = smt.path;
                if (!boost::iequals(lcoPathAsPath.extension().string(), ".paa")) {
                    smt.path = lcoPathAsPath.replace_extension(".paa").string();
                }

                auto mmSize = std::max(smt.mipmap.width, smt.mipmap.height);
                if (tileSize < mmSize) {
                    tileSize = mmSize;
                }
            }

            if (tileSize == 4 && ba::iequals(worldName, "vr")) {
                tileSize = 1024;
            }

            auto& firstSmt = satMapTiles[0];

            auto tileWidth = tileSize;
            auto tileHeight = tileSize;

            TileTransform firstTT = firstSmt.tt;
            if (firstPos.has_value()) {
                firstTT = *firstPos;
            }

            auto firstWorldPos = firstTT.pos;
            auto firstAside = firstTT.aside;

            auto scaleFactor = firstAside[0] * tileWidth;

            if (scaleFactor != 1) {
                scaleFactor += 0.5;

                auto newWidth = static_cast<int32_t>(scaleFactor * tileWidth);

                tileWidth = newWidth;
                tileHeight = newWidth;
            }

            auto firstXPos = static_cast<int32_t>(firstWorldPos[0] * tileWidth);
            auto firstYPos = static_cast<int32_t>(firstWorldPos[1] * tileHeight);

            int32_t finalSatMapSize = firstYPos - firstXPos;

            ImageBuf dst(ImageSpec(finalSatMapSize, finalSatMapSize, 4, TypeDesc::UINT8));
            auto& dstSpec = dst.spec();

            if (fillerTile.has_value()) {
                auto filler_mm = fillerTile->mipmap;

                auto fillerWidth = filler_mm.width;
                auto fillerHeight = filler_mm.height;

                ImageBuf src(ImageSpec(fillerWidth, fillerHeight, 4, TypeDesc::UINT8), filler_mm.data.data());

                int32_t numOfSubTiles = tileWidth / filler_mm.width;

                ImageBuf fullFillerTile(ImageSpec(tileWidth, tileHeight, 4, TypeDesc::UINT8));

                for (int32_t i = 0; i < numOfSubTiles; i++) {
                    for (int32_t j = 0; j < numOfSubTiles; j++) {
                        ImageBufAlgo::paste(fullFillerTile, i * fillerWidth, j * fillerHeight, 0, 0, src);
                    }
                }

                #ifdef _DEBUG
                auto copy_filler = fullFillerTile.copy(TypeDesc::UINT8);
                copy_filler.write((basePathSat / "debug_filler_tile.exr").string());
                #endif

                auto& fillterTTs = fillerTile->tt;

                for (auto& tt : fillterTTs) {
                    auto pos = tt.pos;
                    auto xPos = static_cast<int32_t>(pos[0] * tileWidth * -1);
                    auto yPos = static_cast<int32_t>(((pos[1] * tileHeight) - dstSpec.width) * -1);

                    ImageBufAlgo::paste(dst, xPos, yPos, 0, 0, fullFillerTile);
                }

                //#ifdef _DEBUG
                /*auto copy_full = dst.copy(TypeDesc::UINT8);
                copy_full.write((basePathSat / "debug_sat_map_only_filler.exr").string());*/
                //#endif
            }

            //auto dstOG = dst.copy(dst.spec().format);

            int c = 0;
            for (auto& smt : satMapTiles) {
                auto mipmap = smt.mipmap;

                ImageBuf src(ImageSpec(mipmap.width, mipmap.height, 4, TypeDesc::UINT8), mipmap.data.data());

                if (mipmap.width == 4 && mipmap.height == 4) {
                    src = ImageBufAlgo::resize(src, "", 0, ROI(0, tileWidth, 0, tileHeight));
                    PLOG_INFO << fmt::format("4x4 filler method detected, resizing 4x4 tiles to {}x{} !", tileWidth, tileHeight);
                }

                auto srcWidth = src.spec().width;

                auto asideX = smt.tt.aside[0];
                auto scaleFactor = asideX * srcWidth;

                if (scaleFactor != 1) {
                    scaleFactor += 0.5;

                    auto newWidth = static_cast<int32_t>(scaleFactor * srcWidth);

                    src = ImageBufAlgo::resize(src, "", 0, ROI(0, newWidth, 0, newWidth));
                }

                auto srcSpec = src.spec();

                auto width = srcSpec.width;
                auto height = srcSpec.height;

                auto pos = smt.tt.pos;

                auto xPos = static_cast<int32_t>(pos[0] * width * -1);
                auto yPos = static_cast<int32_t>(((pos[1] * height) - dstSpec.width) * -1);

                /*float red[4] = { 1, 0, 0, 1 };
                ImageBufAlgo::render_box(src, 0, 0, width-1, height-1, red);*/

                /*auto debugText = fmt::format("rvmat: {}\nxPos: {}\nyPos: {}\nwidth: {}\nheight: {}\nscaleFactor: {}\npos[0] {}\npos[1] {}\naside[0] {}", 
                    smt.path.filename().string(), xPos, yPos, width, height, scaleFactor, pos[0], pos[1], asideX);
                ImageBufAlgo::render_text(src, 50, 50, debugText, 20, "Arial", red, ImageBufAlgo::TextAlignX::Left);*/

                ImageBufAlgo::paste(dst, xPos, yPos, 0, 0, src);

                /*auto copy = dstOG.copy(dstOG.spec().format);
                ImageBufAlgo::paste(copy, xPos, yPos, 0, 0, src);
                copy.write((basePathSat / fmt::format("debug_part_{}.exr", c++)).string());*/

                /*auto copy_party = dst.copy(TypeDesc::UINT8);
                copy_party.write((basePathSat / fmt::format("debug_part_{}.exr", c++)).string());*/
            }

            //#ifdef _DEBUG
            /*auto copy = dst.copy(TypeDesc::UINT8);
            copy.write((basePathSat / "debug_full_streched.exr").string());*/
            //#endif

            int32_t finalTileSize = dst.spec().width / 4;

            auto cr = boost::counting_range(0, 4);
            std::for_each(std::execution::par_unseq, cr.begin(), cr.end(), [basePathSat, dst, finalTileSize](int i) {
                auto curWritePath = basePathSat / std::to_string(i);
                if (!fs::exists(curWritePath)) {
                    fs::create_directories(curWritePath);
                }

                for (int32_t j = 0; j < 4; j++) {
                    ImageBuf out = ImageBufAlgo::cut(dst, ROI(i * finalTileSize, (i + 1) * finalTileSize, j * finalTileSize, (j + 1) * finalTileSize));
                    out.write((curWritePath / std::to_string(j).append(".png")).string());
                }
            });

            writeImagePyramid(dst, basePathSat / "tiles");

            auto basePathSatDark = basePathSat.parent_path() / "sat_dark";
            auto basePathBakedSat = basePathSat.parent_path() / "baked_sat";
            auto basePathBakedSatDark = basePathSat.parent_path() / "baked_sat_dark";
            fs::create_directories(basePathSatDark);
            fs::create_directories(basePathBakedSat);
            fs::create_directories(basePathBakedSatDark);

            auto width = dst.spec().width;
            auto height = dst.spec().height;
            auto channels = dst.spec().nchannels;

            auto darkPixels = makeDarkSatellitePixels(dst);
            auto darkSat = pixelsToImage(darkPixels, width, height, channels);
            writeImagePyramid(darkSat, basePathSatDark / "tiles");

            auto bakedPixels = imageToPixels(dst);
            applyBakedSatelliteOverlays(wrp, bakedPixels, width, height, false);
            auto bakedSat = pixelsToImage(bakedPixels, width, height, channels);
            writeImagePyramid(bakedSat, basePathBakedSat / "tiles");

            auto bakedDarkPixels = darkPixels;
            applyBakedSatelliteOverlays(wrp, bakedDarkPixels, width, height, true);
            auto bakedSatDark = pixelsToImage(bakedDarkPixels, width, height, channels);
            writeImagePyramid(bakedSatDark, basePathBakedSatDark / "tiles");
        }
        catch (const rust::Error& ex) {
            PLOG_ERROR << fmt::format("Exception in writeSatImages PBO: {}", pboPath);
            PLOG_ERROR << ex.what();
            throw;
        }
    }
}

TileTransform getTileTransform(rust::Box<arma_file_formats::cxx::CfgCxx>& rap) {
    auto texGenValue = rap->get_entry_as_number(texgen_config_path);

    auto tex_gen_class = fmt::format("TexGen{}", texGenValue);
    std::vector<std::string> texgenDirConfigPath { tex_gen_class, "uvTransform", "pos" };
    std::vector<std::string> texgenAsideConfigPath { tex_gen_class, "uvTransform", "aside" };

    auto posArr = rap->get_entry_as_array_float(texgenDirConfigPath);
    std::vector<float_t> posArrStd(posArr.begin(), posArr.end());

    if (posArrStd.size() < 2) {
        auto errMsg = fmt::format("arr size for '{}' was < 2", ba::join(texgenDirConfigPath, " >> "));
        PLOG_ERROR << errMsg;
        throw new std::runtime_error(errMsg);
    }

    auto asideArr = rap->get_entry_as_array_float(texgenAsideConfigPath);
    std::vector<float_t> asideArrStd(asideArr.begin(), asideArr.end());

    if (asideArrStd.size() < 2) {
        auto errMsg = fmt::format("arr size for '{}' was < 2", ba::join(texgenAsideConfigPath, " >> "));
        PLOG_ERROR << errMsg;
        throw new std::runtime_error(errMsg);
    }

    TileTransform tt {
        asideArrStd,
        posArrStd
    };

    return tt;
}
