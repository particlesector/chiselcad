#include "util/ResourcePaths.h"

#include <system_error>

namespace chisel::util {

namespace {

std::filesystem::path installedCandidate(const char* leaf) {
    return executableDir() / ".." / "share" / "chiselcad" / leaf;
}

} // namespace

std::filesystem::path resolveShaderDir() {
    std::filesystem::path installed = installedCandidate("shaders");
    std::error_code ec;
    if (std::filesystem::exists(installed, ec)) {
        return installed;
    }
#ifdef CHISELCAD_SHADER_DIR
    return CHISELCAD_SHADER_DIR;
#else
    return installed;
#endif
}

std::filesystem::path resolveResourceDir() {
    std::filesystem::path installed = installedCandidate("resources");
    std::error_code ec;
    if (std::filesystem::exists(installed, ec)) {
        return installed;
    }
#ifdef CHISELCAD_RESOURCE_DIR
    return CHISELCAD_RESOURCE_DIR;
#else
    return installed;
#endif
}

} // namespace chisel::util
