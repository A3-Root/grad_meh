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

std::array<uint8_t, 4> topoColor(const float elevation, const float shade, const bool contour)
{
    const auto normalized = std::clamp((elevation + 20.0f) / 520.0f, 0.0f, 1.0f);

    std::array<float, 3> low = { 190.0f, 212.0f, 179.0f };
    std::array<float, 3> high = { 238.0f, 231.0f, 208.0f };

    if (elevation < 0.5f) {
        low = { 132.0f, 176.0f, 199.0f };
        high = { 168.0f, 202.0f, 218.0f };
    }

    const auto contourFactor = contour ? 0.62f : 1.0f;
    const auto shadeFactor = std::clamp(0.72f + shade * 0.42f, 0.55f, 1.18f) * contourFactor;

    return {
        static_cast<uint8_t>(std::clamp((low[0] + (high[0] - low[0]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((low[1] + (high[1] - low[1]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((low[2] + (high[2] - low[2]) * normalized) * shadeFactor, 0.0f, 255.0f)),
        255
    };
}

PixelPoint worldToPixel(const arma_file_formats::cxx::OprwCxx& wrp, const float worldX, const float worldY)
{
    const auto worldSize = static_cast<float>(wrp.layer_cell_size * wrp.layer_size_x);
    const auto width = static_cast<int32_t>(wrp.map_size_x);
    const auto height = static_cast<int32_t>(wrp.map_size_y);

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

std::vector<uint8_t> buildTopoPixels(arma_file_formats::cxx::OprwCxx& wrp)
{
    const auto width = static_cast<int32_t>(wrp.map_size_x);
    const auto height = static_cast<int32_t>(wrp.map_size_y);
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) {
            const auto elevation = elevationAt(wrp, x, y);
            const auto dx = elevationAt(wrp, x + 1, y) - elevationAt(wrp, x - 1, y);
            const auto dy = elevationAt(wrp, x, y + 1) - elevationAt(wrp, x, y - 1);
            const auto shade = std::clamp(((-dx * 0.55f) + (dy * 0.35f)) / 32.0f, -0.55f, 0.55f);

            const auto contourInterval = 50.0f;
            const auto contourWidth = 1.4f;
            const auto contourRemainder = std::fabs(std::fmod(elevation, contourInterval));
            const auto contour = elevation > 1.0f && (contourRemainder < contourWidth || contourRemainder > contourInterval - contourWidth);
            const auto color = topoColor(elevation, shade, contour);

            const auto outY = height - y - 1;
            const auto offset = (static_cast<size_t>(outY) * width + x) * 4;
            pixels[offset + 0] = color[0];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[2];
            pixels[offset + 3] = color[3];
        }
    }

    return pixels;
}
}

void writeTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo)
{
    if (!fs::exists(basePathTopo)) {
        fs::create_directories(basePathTopo);
    }

    const auto width = static_cast<int32_t>(wrp.map_size_x);
    const auto height = static_cast<int32_t>(wrp.map_size_y);
    auto pixels = buildTopoPixels(wrp);

    ImageBuf topo(ImageSpec(width, height, 4, TypeDesc::UINT8), pixels.data());
    topo.write((basePathTopo / "full.png").string());
    writeImagePyramid(topo, basePathTopo / "tiles");
}

void writeBakedTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo)
{
    if (!fs::exists(basePathBakedTopo)) {
        fs::create_directories(basePathBakedTopo);
    }

    const auto width = static_cast<int32_t>(wrp.map_size_x);
    const auto height = static_cast<int32_t>(wrp.map_size_y);
    auto pixels = buildTopoPixels(wrp);

    for (auto& mapInfo : wrp.map_infos_4) {
        const std::vector<PixelPoint> polygon = {
            worldToPixel(wrp, mapInfo.bounds.a.x, mapInfo.bounds.a.y),
            worldToPixel(wrp, mapInfo.bounds.b.x, mapInfo.bounds.b.y),
            worldToPixel(wrp, mapInfo.bounds.d.x, mapInfo.bounds.d.y),
            worldToPixel(wrp, mapInfo.bounds.c.x, mapInfo.bounds.c.y)
        };
        fillPolygon(pixels, width, height, polygon, { 92, 88, 82, 255 }, { 55, 52, 48, 255 });
    }

    for (auto& mapInfo : wrp.map_infos_5) {
        drawLine(
            pixels,
            width,
            height,
            worldToPixel(wrp, mapInfo.floats[0], mapInfo.floats[1]),
            worldToPixel(wrp, mapInfo.floats[2], mapInfo.floats[3]),
            { 92, 92, 92, 255 },
            1);
    }

    for (auto& river : wrp.map_info_river) {
        std::vector<PixelPoint> polygon = {};
        for (auto& point : river.polygon) {
            polygon.push_back(worldToPixel(wrp, point.x, point.y));
        }
        fillPolygon(pixels, width, height, polygon, { 96, 158, 190, 255 }, { 65, 124, 158, 255 });
    }

    ImageBuf bakedTopo(ImageSpec(width, height, 4, TypeDesc::UINT8), pixels.data());
    bakedTopo.write((basePathBakedTopo / "full.png").string());
    writeImagePyramid(bakedTopo, basePathBakedTopo / "tiles");
}
