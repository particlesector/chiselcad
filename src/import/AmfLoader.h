#pragma once
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace chisel::io {

struct RawAmfMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    std::string error; // empty = success
};

// Reads a plain-XML AMF (Additive Manufacturing File Format) file's mesh
// geometry: <object><mesh><vertices><vertex><coordinates><x/><y/><z/>
// </coordinates></vertex>...</vertices><volume><triangle><v1/><v2/><v3/>
// </triangle>...</volume></mesh></object>. Unlike 3MF, AMF puts its numbers
// in element *text content* rather than attributes — <x>1.5</x>, not
// x="1.5" — which is why this needs its own field-tracking pass over
// MiniXml's flat event stream rather than reusing ThreeMfLoader's
// attribute-based one. Every <object>'s mesh is merged into one triangle
// soup, offsetting each object's <v1>/<v2>/<v3> indices (local to that
// object's own <vertices> list, per the AMF spec) by the vertex count
// accumulated before it.
//
// Scope limits: only plain (uncompressed) XML .amf files are supported —
// gzip-compressed .amf (a valid AMF variant) is not. <constellation>
// (assembling multiple objects with per-instance transforms) and per-volume
// materials/colors are not read — only literal <mesh> geometry directly
// inside each <object>.
RawAmfMesh loadAmfMesh(const std::filesystem::path& path);

} // namespace chisel::io
