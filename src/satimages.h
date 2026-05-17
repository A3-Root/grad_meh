#pragma once

#include "util.h"

#include <vector>
#include <filesystem>
#include <execution>
#include <tuple>

#include <rust-lib/lib.h>
#include <OpenImageIO/imagebuf.h>

namespace fs = std::filesystem;

struct TileTransform {
    std::vector<float_t> aside = {};
    std::vector<float_t> pos = {};

};

struct CmpTileTransform
{
    bool operator()(const TileTransform& lhs, const TileTransform& rhs) const
    {
        return (lhs.aside < rhs.aside) || (rhs.pos < lhs.pos);
    }
};

void writeSatImages(arma_file_formats::cxx::OprwCxx& wrp, const int32_t& worldSize, std::filesystem::path& basePathSat, const std::string& worldName);
void writeImagePyramid(const OpenImageIO_v2_5::ImageBuf& src, const std::filesystem::path& basePath, const int32_t tileSize = 256);
TileTransform getTileTransform(rust::Box<arma_file_formats::cxx::CfgCxx>& rap);
