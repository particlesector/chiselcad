#include "StlExporter.h"
#include <cstring>
#include <fstream>

namespace chisel::io {

std::string exportBinaryStl(const std::filesystem::path& outPath,
                             const std::vector<render::Vertex>& verts,
                             const std::vector<uint32_t>& indices)
{
    if (indices.size() % 3 != 0)
        return "Index count is not a multiple of 3";

    std::ofstream f(outPath, std::ios::binary);
    if (!f)
        return "Cannot open file for writing: " + outPath.string();

    // 80-byte header
    char header[80] = "ChiselCAD binary STL";
    f.write(header, sizeof(header));

    // Triangle count
    auto triCount = static_cast<uint32_t>(indices.size() / 3);
    f.write(reinterpret_cast<const char*>(&triCount), 4);

    // Per-triangle: normal (12) + 3 vertices (36) + attribute (2) = 50 bytes
    const uint16_t attr = 0;
    for (uint32_t i = 0; i < triCount; ++i) {
        const auto& v0 = verts[indices[i * 3 + 0]];
        const auto& v1 = verts[indices[i * 3 + 1]];
        const auto& v2 = verts[indices[i * 3 + 2]];

        // Use the first vertex normal as the face normal (flat-shaded export)
        f.write(reinterpret_cast<const char*>(&v0.normal), 12);
        f.write(reinterpret_cast<const char*>(&v0.pos),    12);
        f.write(reinterpret_cast<const char*>(&v1.pos),    12);
        f.write(reinterpret_cast<const char*>(&v2.pos),    12);
        f.write(reinterpret_cast<const char*>(&attr),       2);
    }

    if (!f)
        return "Write error";
    return {};
}

} // namespace chisel::io
