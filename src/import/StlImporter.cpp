#include "StlImporter.h"
#include <array>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <sstream>
#include <string>

namespace chisel::io {

// ---------------------------------------------------------------------------
// Binary STL
// ---------------------------------------------------------------------------
static StlMesh loadBinary(std::ifstream& f, uint32_t triCount) {
    StlMesh out;
    out.verts.reserve(triCount * 3);
    out.indices.reserve(triCount * 3);

    for (uint32_t i = 0; i < triCount; ++i) {
        glm::vec3 normal{};
        f.read(reinterpret_cast<char*>(&normal), 12);

        for (int v = 0; v < 3; ++v) {
            glm::vec3 pos{};
            f.read(reinterpret_cast<char*>(&pos), 12);
            out.indices.push_back(static_cast<uint32_t>(out.verts.size()));
            out.verts.push_back({pos, normal});
        }
        uint16_t attr = 0;
        f.read(reinterpret_cast<char*>(&attr), 2);
    }

    if (!f)
        out.error = "Unexpected end of file while reading binary STL";
    return out;
}

// ---------------------------------------------------------------------------
// ASCII STL
// ---------------------------------------------------------------------------
static StlMesh loadAscii(std::ifstream& f) {
    StlMesh out;
    std::string line;
    glm::vec3 normal{};
    int vertInFacet = 0;
    std::array<render::Vertex, 3> tri{};

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
                tri[vertInFacet] = {pos, normal};
                ++vertInFacet;
            }
        } else if (kw == "endfacet") {
            if (vertInFacet == 3) {
                for (int v = 0; v < 3; ++v) {
                    out.indices.push_back(static_cast<uint32_t>(out.verts.size()));
                    out.verts.push_back(tri[v]);
                }
            }
            vertInFacet = 0;
        }
    }

    if (out.verts.empty())
        out.error = "No geometry found in ASCII STL";
    return out;
}

// ---------------------------------------------------------------------------
// loadStl — detects binary vs ASCII automatically
// ---------------------------------------------------------------------------
StlMesh loadStl(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {{}, {}, "Cannot open file: " + path.string()};

    // Read the 80-byte header and 4-byte triangle count
    char header[80] = {};
    f.read(header, 80);
    uint32_t triCount = 0;
    f.read(reinterpret_cast<char*>(&triCount), 4);

    if (!f)
        return {{}, {}, "File too small to be a valid STL: " + path.string()};

    // ASCII STL starts with "solid"; binary may too, so also check expected size
    bool looksAscii = (std::strncmp(header, "solid", 5) == 0);
    if (looksAscii) {
        // Verify by checking if file size matches the binary formula
        auto curPos = f.tellg();
        f.seekg(0, std::ios::end);
        auto fileSize = f.tellg();
        auto expectedBinary = static_cast<std::streamoff>(80 + 4 + triCount * 50);
        looksAscii = (fileSize != expectedBinary);
    }

    if (looksAscii) {
        // Re-open as text for ASCII parsing
        f.close();
        std::ifstream tf(path);
        if (!tf)
            return {{}, {}, "Cannot re-open file for ASCII parsing: " + path.string()};
        return loadAscii(tf);
    }

    return loadBinary(f, triCount);
}

} // namespace chisel::io
