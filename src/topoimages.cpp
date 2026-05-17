#include "topoimages.h"

#include "satimages.h"

#include <OpenImageIO/imagebuf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace OpenImageIO_v2_5;

namespace {
constexpr float kTopoPixelsPerMeter = 1.0f;

struct PixelPoint {
    int32_t x = 0;
    int32_t y = 0;
};

float elevationAt(const arma_file_formats::cxx::OprwCxx& wrp, const int32_t x, const int32_t y)
{
    const auto clampedX = std::clamp<int32_t>(x, 0, wrp.map_size_x - 1);
    const auto clampedY = std::clamp<int32_t>(y, 0, wrp.map_size_y - 1);
    return wrp.elevation[clampedX + wrp.map_size_x * clampedY];
}

float elevationAtWorld(const arma_file_formats::cxx::OprwCxx& wrp, const float worldSize, const float worldX, const float worldY)
{
    const auto sourceX = static_cast<int32_t>(std::round((std::clamp(worldX, 0.0f, worldSize) / worldSize) * (wrp.map_size_x - 1)));
    const auto sourceY = static_cast<int32_t>(std::round((std::clamp(worldY, 0.0f, worldSize) / worldSize) * (wrp.map_size_y - 1)));
    return elevationAt(wrp, sourceX, sourceY);
}

std::array<uint8_t, 4> topoColor(const float elevation, const float shade, const bool contour, const bool dark)
{
    const auto normalized = std::clamp((elevation + 20.0f) / 520.0f, 0.0f, 1.0f);

    std::array<float, 3> low = dark
        ? std::array<float, 3>{ 42.0f, 58.0f, 46.0f }
        : std::array<float, 3>{ 190.0f, 212.0f, 179.0f };
    std::array<float, 3> high = dark
        ? std::array<float, 3>{ 92.0f, 86.0f, 70.0f }
        : std::array<float, 3>{ 238.0f, 231.0f, 208.0f };

    if (elevation < 0.5f) {
        low = dark ? std::array<float, 3>{ 19.0f, 42.0f, 58.0f } : std::array<float, 3>{ 132.0f, 176.0f, 199.0f };
        high = dark ? std::array<float, 3>{ 32.0f, 66.0f, 88.0f } : std::array<float, 3>{ 168.0f, 202.0f, 218.0f };
    }

    const auto contourFactor = contour ? (dark ? 1.34f : 0.62f) : 1.0f;
    const auto shadeFactor = std::clamp((dark ? 0.86f : 0.72f) + shade * 0.42f, dark ? 0.68f : 0.55f, dark ? 1.38f : 1.18f) * contourFactor;

    return {
        static_cast<uint8_t>(std::clamp((low[0] + (high[0] - low[0]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((low[1] + (high[1] - low[1]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((low[2] + (high[2] - low[2]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        255
    };
}

PixelPoint worldToPixel(const arma_file_formats::cxx::OprwCxx& wrp, const int32_t width, const int32_t height, const float worldX, const float worldY)
{
    const auto worldSize = static_cast<float>(wrp.layer_cell_size * wrp.layer_size_x);

    return {
        std::clamp(static_cast<int32_t>((worldX / worldSize) * width), 0, width - 1),
        std::clamp(static_cast<int32_t>(height - ((worldY / worldSize) * height)), 0, height - 1)
    };
}

void setPixel(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const int32_t x, const int32_t y, const std::array<uint8_t, 4>& color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }

    const auto offset = (static_cast<size_t>(y) * width + x) * 4;
    pixels[offset + 0] = color[0];
    pixels[offset + 1] = color[1];
    pixels[offset + 2] = color[2];
    pixels[offset + 3] = color[3];
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

void drawLine(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, PixelPoint a, PixelPoint b, const std::array<uint8_t, 4>& color, const int32_t thickness)
{
    auto dx = std::abs(b.x - a.x);
    auto sx = a.x < b.x ? 1 : -1;
    auto dy = -std::abs(b.y - a.y);
    auto sy = a.y < b.y ? 1 : -1;
    auto err = dx + dy;

    while (true) {
        for (int32_t ox = -thickness; ox <= thickness; ox++) {
            for (int32_t oy = -thickness; oy <= thickness; oy++) {
                setPixel(pixels, width, height, a.x + ox, a.y + oy, color);
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

void fillPolygon(std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const std::vector<PixelPoint>& polygon, const std::array<uint8_t, 4>& fill, const std::array<uint8_t, 4>& outline)
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
                setPixel(pixels, width, height, x, y, fill);
            }
        }
    }

    for (size_t i = 0; i < polygon.size(); i++) {
        drawLine(pixels, width, height, polygon[i], polygon[(i + 1) % polygon.size()], outline, 1);
    }
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

std::vector<uint8_t> buildTopoPixels(arma_file_formats::cxx::OprwCxx& wrp, const bool dark)
{
    const auto worldSize = static_cast<float>(wrp.layer_cell_size * wrp.layer_size_x);
    const auto width = static_cast<int32_t>(std::ceil(worldSize * kTopoPixelsPerMeter));
    const auto height = width;
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) {
            const auto worldX = (static_cast<float>(x) + 0.5f) / kTopoPixelsPerMeter;
            const auto worldY = worldSize - ((static_cast<float>(y) + 0.5f) / kTopoPixelsPerMeter);
            const auto sampleStep = std::max(1.0f, 1.0f / kTopoPixelsPerMeter);
            const auto elevation = elevationAtWorld(wrp, worldSize, worldX, worldY);
            const auto dx = elevationAtWorld(wrp, worldSize, worldX + sampleStep, worldY) - elevationAtWorld(wrp, worldSize, worldX - sampleStep, worldY);
            const auto dy = elevationAtWorld(wrp, worldSize, worldX, worldY + sampleStep) - elevationAtWorld(wrp, worldSize, worldX, worldY - sampleStep);
            const auto shade = std::clamp(((-dx * 0.55f) + (dy * 0.35f)) / 32.0f, -0.55f, 0.55f);

            const auto contourInterval = 50.0f;
            const auto contourWidth = 1.4f;
            const auto contourRemainder = std::fabs(std::fmod(elevation, contourInterval));
            const auto contour = elevation > 1.0f && (contourRemainder < contourWidth || contourRemainder > contourInterval - contourWidth);
            const auto color = topoColor(elevation, shade, contour, dark);

            const auto offset = (static_cast<size_t>(y) * width + x) * 4;
            pixels[offset + 0] = color[0];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[2];
            pixels[offset + 3] = color[3];
        }
    }

    return pixels;
}

void applyBakedOverlays(arma_file_formats::cxx::OprwCxx& wrp, std::vector<uint8_t>& pixels, const int32_t width, const int32_t height, const bool dark)
{
    const auto buildingFill = dark ? std::array<uint8_t, 4>{ 126, 118, 98, 255 } : std::array<uint8_t, 4>{ 130, 124, 112, 255 };
    const auto buildingOutline = dark ? std::array<uint8_t, 4>{ 178, 168, 140, 255 } : std::array<uint8_t, 4>{ 86, 82, 76, 255 };
    const auto roadColor = dark ? std::array<uint8_t, 4>{ 190, 180, 158, 255 } : std::array<uint8_t, 4>{ 216, 205, 184, 255 };
    const auto powerlineColor = dark ? std::array<uint8_t, 4>{ 154, 158, 164, 255 } : std::array<uint8_t, 4>{ 94, 94, 94, 255 };
    const auto riverFill = dark ? std::array<uint8_t, 4>{ 58, 122, 156, 255 } : std::array<uint8_t, 4>{ 96, 158, 190, 255 };
    const auto riverOutline = dark ? std::array<uint8_t, 4>{ 84, 154, 190, 255 } : std::array<uint8_t, 4>{ 65, 124, 158, 255 };

    for (auto& mapInfo : wrp.map_infos_4) {
        const std::vector<PixelPoint> polygon = {
            worldToPixel(wrp, width, height, mapInfo.bounds.a.x, mapInfo.bounds.a.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.b.x, mapInfo.bounds.b.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.d.x, mapInfo.bounds.d.y),
            worldToPixel(wrp, width, height, mapInfo.bounds.c.x, mapInfo.bounds.c.y)
        };
        fillPolygonBlended(pixels, width, height, polygon, buildingFill, dark ? 0.28f : 0.32f, buildingOutline, dark ? 0.38f : 0.45f);
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
                    dark ? 0.42f : 0.48f);
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
            0.42f);
    }

    for (auto& river : wrp.map_info_river) {
        std::vector<PixelPoint> polygon = {};
        for (auto& point : river.polygon) {
            polygon.push_back(worldToPixel(wrp, width, height, point.x, point.y));
        }
        fillPolygonBlended(pixels, width, height, polygon, riverFill, dark ? 0.62f : 0.68f, riverOutline, 0.45f);
    }
}

void writeRasterLayer(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePath, const bool dark, const bool baked)
{
    if (!fs::exists(basePath)) {
        fs::create_directories(basePath);
    }

    const auto worldSize = static_cast<float>(wrp.layer_cell_size * wrp.layer_size_x);
    const auto width = static_cast<int32_t>(std::ceil(worldSize * kTopoPixelsPerMeter));
    const auto height = width;
    auto pixels = buildTopoPixels(wrp, dark);

    if (baked) {
        applyBakedOverlays(wrp, pixels, width, height, dark);
    }

    ImageBuf raster(ImageSpec(width, height, 4, TypeDesc::UINT8), pixels.data());
    writeImagePyramid(raster, basePath / "tiles");
}
}

void writeTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo)
{
    writeRasterLayer(wrp, basePathTopo, false, false);
}

void writeBakedTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo)
{
    writeRasterLayer(wrp, basePathBakedTopo, false, true);
}

void writeTopoImageSet(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo, fs::path& basePathTopoDark)
{
    writeRasterLayer(wrp, basePathTopo, false, false);
    writeRasterLayer(wrp, basePathTopoDark, true, false);
}

void writeBakedTopoImageSet(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo, fs::path& basePathBakedTopoDark)
{
    writeRasterLayer(wrp, basePathBakedTopo, false, true);
    writeRasterLayer(wrp, basePathBakedTopoDark, true, true);
}
