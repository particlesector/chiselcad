#include "StlImporter.h"
#include <utility>

namespace chisel::io {

StlMesh loadStl(const std::filesystem::path& path) {
    RawStlMesh raw = loadStlRaw(path);

    StlMesh out;
    out.error = std::move(raw.error);
    if (!out.error.empty())
        return out;

    out.verts.reserve(raw.positions.size());
    for (std::size_t i = 0; i < raw.positions.size(); ++i)
        out.verts.push_back({raw.positions[i], raw.normals[i], render::kDefaultVertexColor});
    out.indices = std::move(raw.indices);
    return out;
}

} // namespace chisel::io
