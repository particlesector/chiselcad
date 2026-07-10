#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

// ---------------------------------------------------------------------------
// RawStlMesh — triangle-soup STL data with no GPU/render dependency, safe to
// include from anywhere (including the lightweight `chiselcad_tests` target,
// which has no Vulkan/VMA deps). Positions/normals are duplicated per
// triangle exactly as stored in the file (STL has no shared-vertex
// indexing); callers that need welded/manifold geometry (e.g. Manifold's
// MeshGL constructor) handle that themselves.
// ---------------------------------------------------------------------------
struct RawStlMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals; // one per position, from each triangle's stored facet normal
    std::vector<uint32_t>  indices;
    std::string            error;   // empty = success
};

RawStlMesh loadStlRaw(const std::filesystem::path& path);

} // namespace chisel::io
