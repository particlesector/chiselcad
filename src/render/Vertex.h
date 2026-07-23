#pragma once
#include <glm/glm.hpp>

namespace chisel::render {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color; // per-root color() tint; defaults to kDefaultVertexColor
};

// Default per-vertex tint for geometry with no color() (or, for imported
// STL meshes, no color data at all). Must match mesh.frag's lighting
// formula, which reads the base color per-vertex instead of a constant.
inline constexpr glm::vec3 kDefaultVertexColor{0.74f, 0.74f, 0.78f};

} // namespace chisel::render
