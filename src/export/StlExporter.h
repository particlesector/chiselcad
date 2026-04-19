#pragma once
#include "render/GpuMesh.h"
#include <filesystem>
#include <string>
#include <vector>

namespace chisel::io {

// Write a binary STL from vertex/index buffers.
// Returns an empty string on success or an error message on failure.
std::string exportBinaryStl(const std::filesystem::path& outPath,
                             const std::vector<render::Vertex>& verts,
                             const std::vector<uint32_t>& indices);

} // namespace chisel::io
