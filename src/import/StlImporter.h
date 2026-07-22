#pragma once
#include "StlLoader.h"
#include "render/Vertex.h"
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
// path (Application::loadStlFile), GUI-only despite render::Vertex itself
// being dependency-free. CsgEvaluator's import() builtin uses loadStlRaw()
// directly instead, since it belongs in chiselcad_core alongside the rest
// of the .scad build pipeline.
StlMesh loadStl(const std::filesystem::path& path);

} // namespace chisel::io
