#include "StlLoader.h"
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace chisel::io {

// ---------------------------------------------------------------------------
// Binary STL
// ---------------------------------------------------------------------------
static RawStlMesh loadBinary(std::ifstream& f, uint32_t triCount) {
    RawStlMesh out;
    out.positions.reserve(static_cast<std::size_t>(triCount) * 3);
    out.normals.reserve(static_cast<std::size_t>(triCount) * 3);
    out.indices.reserve(static_cast<std::size_t>(triCount) * 3);

    for (uint32_t i = 0; i < triCount; ++i) {
        glm::vec3 normal{};
        f.read(reinterpret_cast<char*>(&normal), 12);

        std::array<glm::vec3, 3> verts{};
        for (auto& pos : verts)
            f.read(reinterpret_cast<char*>(&pos), 12);

        uint16_t attr = 0;
        f.read(reinterpret_cast<char*>(&attr), 2);

        // Caller already bounds triCount against the file's actual size, but
        // stop as soon as the stream fails regardless, rather than spinning
        // through the remaining iterations appending zeroed geometry. Nothing
        // is pushed to `out` until the full 50-byte triangle record has read
        // successfully, so a truncated final triangle never leaves a partial
        // (1- or 2-vertex) zeroed entry behind.
        if (!f) break;

        for (const auto& pos : verts) {
            out.indices.push_back(static_cast<uint32_t>(out.positions.size()));
            out.positions.push_back(pos);
            out.normals.push_back(normal);
        }
    }

    if (!f)
        out.error = "Unexpected end of file while reading binary STL";
    else if (out.positions.empty())
        out.error = "No geometry found in binary STL"; // triCount == 0 — legal but empty
    return out;
}

// ---------------------------------------------------------------------------
// ASCII STL
// ---------------------------------------------------------------------------
static RawStlMesh loadAscii(std::ifstream& f) {
    RawStlMesh out;
    std::string line;
    glm::vec3 normal{};
    int vertInFacet = 0;
    std::array<glm::vec3, 3> tri{};

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string kw;
        ss >> kw;

        if (kw == "facet") {
            std::string normalKw;
            ss >> normalKw >> normal.x >> normal.y >> normal.z;
            vertInFacet = 0;
        } else if (kw == "vertex") {
            if (vertInFacet < 3) {
                glm::vec3 pos{};
                ss >> pos.x >> pos.y >> pos.z;
                tri[vertInFacet] = pos;
                ++vertInFacet;
            }
        } else if (kw == "endfacet") {
            if (vertInFacet == 3) {
                for (int v = 0; v < 3; ++v) {
                    out.indices.push_back(static_cast<uint32_t>(out.positions.size()));
                    out.positions.push_back(tri[v]);
                    out.normals.push_back(normal);
                }
            }
            vertInFacet = 0;
        }
    }

    if (out.positions.empty())
        out.error = "No geometry found in ASCII STL";
    return out;
}

// ---------------------------------------------------------------------------
// loadStlRaw — detects binary vs ASCII automatically
// ---------------------------------------------------------------------------
RawStlMesh loadStlRaw(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {{}, {}, {}, "Cannot open file: " + path.string()};

    // Read the 80-byte header and 4-byte triangle count
    char header[80] = {};
    f.read(header, 80);
    uint32_t triCount = 0;
    f.read(reinterpret_cast<char*>(&triCount), 4);

    if (!f)
        return {{}, {}, {}, "File too small to be a valid STL: " + path.string()};

    // Each binary triangle record is 12 (normal) + 3*12 (vertices) + 2 (attr)
    // bytes. Compute in a 64-bit type throughout — triCount is attacker/
    // corruption-controlled and 32-bit `triCount * 50` overflows above
    // ~85.9M triangles, which would otherwise misclassify a corrupt file's
    // true size (used below both for the ASCII/binary heuristic and to
    // reject a bogus triangle count before ever looping over it).
    constexpr std::streamoff kHeaderSize   = 84;
    constexpr std::streamoff kTriRecordSize = 50;
    std::streamoff expectedBinary =
        kHeaderSize + static_cast<std::streamoff>(triCount) * kTriRecordSize;

    auto fileSize = [&] {
        auto curPos = f.tellg();
        f.seekg(0, std::ios::end);
        auto size = f.tellg();
        f.seekg(curPos);
        return size;
    }();

    // ASCII STL starts with "solid"; binary may too, so also check expected size
    bool looksAscii = (std::strncmp(header, "solid", 5) == 0);
    if (looksAscii)
        looksAscii = (fileSize != expectedBinary);

    if (looksAscii) {
        // Re-open as text for ASCII parsing
        f.close();
        std::ifstream tf(path);
        if (!tf)
            return {{}, {}, {}, "Cannot re-open file for ASCII parsing: " + path.string()};
        return loadAscii(tf);
    }

    // Reject a triangle count the file can't actually hold (corrupted/
    // truncated file, or a deliberately crafted header) before loadBinary
    // ever allocates or reads based on it — otherwise a bogus triCount near
    // UINT32_MAX drives ~4 billion loop iterations and tens of GB of vector
    // growth before the stream's fail state is ever observed.
    if (expectedBinary > fileSize)
        return {{}, {}, {}, "Binary STL triangle count exceeds file size: " + path.string()};

    return loadBinary(f, triCount);
}

} // namespace chisel::io
