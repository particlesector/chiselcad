#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

struct RawThreeMfMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    std::string error; // empty = success
};

// Reads a 3MF (3D Manufacturing Format) file's mesh geometry. 3MF is a ZIP
// archive containing an XML model part — conventionally "3D/3dmodel.model",
// located by filename suffix (see ZipReader.h) — whose <object><mesh>
// elements each carry a <vertices> list of <vertex x= y= z=/> and a
// <triangles> list of <triangle v1= v2= v3=/> (indices local to that
// object's own vertex list, per the 3MF core spec). Every <object>'s mesh
// is merged into one triangle soup, offsetting each object's triangle
// indices by the vertex count accumulated before it.
//
// Scope limits (this is not a full 3MF implementation):
//   - <components> (an object referencing other objects, optionally with
//     its own transform) are not resolved — only literal <mesh> geometry
//     directly inside each <object> is read.
//   - The <build><item .../> transform attribute (placing an object's mesh
//     into the scene) is not applied — every object's mesh merges in at its
//     own local coordinates.
// This covers a single-part 3MF export (the overwhelming majority of 3MF
// files produced by slicers/CAD tools for one printable model), not
// multi-part assemblies with per-part placement.
RawThreeMfMesh loadThreeMfMesh(const std::filesystem::path& path);

} // namespace chisel::io
