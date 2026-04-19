#pragma once
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

StlMesh loadStl(const std::filesystem::path& path);

} // namespace chisel::io
