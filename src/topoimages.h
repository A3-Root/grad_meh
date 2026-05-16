#pragma once

#include <filesystem>

#include <rust-lib/lib.h>

namespace fs = std::filesystem;

void writeTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathTopo);
void writeBakedTopoImages(arma_file_formats::cxx::OprwCxx& wrp, fs::path& basePathBakedTopo);
