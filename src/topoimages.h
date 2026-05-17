#pragma once

#include <filesystem>

#include <rust-lib/lib.h>

namespace fs = std::filesystem;

void writeTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo);
void writeBakedTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo);
void writeTopoImageSet(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo, fs::path& basePathTopoDark);
void writeBakedTopoImageSet(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo, fs::path& basePathBakedTopoDark);
