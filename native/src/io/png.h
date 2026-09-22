#pragma once

#include "eval/image.h"

#include <string>

namespace sdf {

// 8-bit RGB PNG via zlib. Tooling only — the engine core does not depend on it.
bool write_png(const std::string &path, const Image &img);
// Reads 8-bit RGB or RGBA (alpha dropped), non-interlaced.
bool read_png(const std::string &path, Image &out);

} // namespace sdf
