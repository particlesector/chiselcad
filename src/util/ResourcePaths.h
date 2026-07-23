#pragma once
#include <filesystem>

namespace chisel::util {

// Directory containing the currently running executable, resolved via a
// platform API (never argv[0] or the current working directory, both of
// which are caller-controlled and unreliable).
std::filesystem::path executableDir();

// Resolves the directory holding compiled SPIR-V shaders. Prefers the
// relocatable installed layout (<exeDir>/../share/chiselcad/shaders, as
// produced by `cmake --install`) so a packaged release binary keeps working
// after being copied to another machine; falls back to the compile-time
// build-tree path (CHISELCAD_SHADER_DIR) for dev builds run in place from
// the build directory.
std::filesystem::path resolveShaderDir();

// Same idea as resolveShaderDir(), for the bundled resources/ directory
// (fonts, etc).
std::filesystem::path resolveResourceDir();

} // namespace chisel::util
