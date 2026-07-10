#pragma once
#include "StlLoader.h"
#include "render/GpuMesh.h"
#include <filesystem>
#include <string>
#include <vector>

namespace chisel::io {

struct StlMesh {
    std::vector<render::Vertex> verts;
    std::vector<uint32_t>       indices;
    std::string                 error; // empty = success
};

// render::Vertex convenience wrapper around loadStlRaw() (StlLoader.h) for
// direct GPU/UI consumption — the standalone "open an .stl file" viewer
// path (Application::loadStlFile). This is the one function in src/import/
// that depends on render/GpuMesh.h; CsgEvaluator's import() builtin uses
// loadStlRaw() directly instead, since it must stay link-compatible with
// the lightweight chiselcad_tests target (no Vulkan/VMA deps).
StlMesh loadStl(const std::filesystem::path& path);

} // namespace chisel::io
