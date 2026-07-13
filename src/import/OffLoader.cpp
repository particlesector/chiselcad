#include "OffLoader.h"

#include <fstream>
#include <optional>
#include <sstream>

namespace chisel::io {

namespace {

// Next physical line with a '#'-to-end-of-line comment stripped, skipping
// blank/comment-only lines; nullopt at EOF.
std::optional<std::string> nextContentLine(std::istream& f) {
    std::string line;
    while (std::getline(f, line)) {
        auto hashPos = line.find('#');
        if (hashPos != std::string::npos)
            line.erase(hashPos);
        if (line.find_first_not_of(" \t\r\n") != std::string::npos)
            return line;
    }
    return std::nullopt;
}

std::vector<std::string> splitTokens(const std::string& line) {
    std::istringstream ss(line);
    std::vector<std::string> toks;
    std::string t;
    while (ss >> t)
        toks.push_back(std::move(t));
    return toks;
}

} // namespace

RawOffMesh loadOffMesh(const std::filesystem::path& path) {
    RawOffMesh out;

    std::ifstream f(path);
    if (!f) {
        out.error = "Cannot open file: " + path.string();
        return out;
    }

    auto firstLine = nextContentLine(f);
    if (!firstLine) {
        out.error = "OFF file is empty";
        return out;
    }
    std::vector<std::string> headerToks = splitTokens(*firstLine);
    if (headerToks.empty() || headerToks[0].size() < 3 ||
        headerToks[0].compare(headerToks[0].size() - 3, 3, "OFF") != 0) {
        out.error = "not a recognized OFF file (missing 'OFF' header)";
        return out;
    }

    // Counts may follow the header on the same line ("OFF 8 6 12") or on
    // the next content line ("OFF" alone, then "8 6 12").
    std::vector<std::string> countToks(headerToks.begin() + 1, headerToks.end());
    if (countToks.size() < 2) {
        auto countLine = nextContentLine(f);
        if (!countLine) {
            out.error = "OFF file ended before vertex/face counts";
            return out;
        }
        countToks = splitTokens(*countLine);
    }
    if (countToks.size() < 2) {
        out.error = "OFF file: expected '<numVertices> <numFaces> <numEdges>'";
        return out;
    }

    long long numVerts = 0, numFaces = 0;
    try {
        numVerts = std::stoll(countToks[0]);
        numFaces = std::stoll(countToks[1]);
    } catch (...) {
        out.error = "OFF file: invalid vertex/face count";
        return out;
    }
    if (numVerts < 0 || numFaces < 0) {
        out.error = "OFF file: negative vertex/face count";
        return out;
    }

    out.positions.reserve(static_cast<std::size_t>(numVerts));
    for (long long i = 0; i < numVerts; ++i) {
        auto line = nextContentLine(f);
        if (!line) {
            out.error = "OFF file ended while reading vertex " + std::to_string(i);
            return out;
        }
        auto toks = splitTokens(*line);
        if (toks.size() < 3) {
            out.error = "OFF file: vertex " + std::to_string(i) + " has fewer than 3 coordinates";
            return out;
        }
        try {
            out.positions.emplace_back(static_cast<float>(std::stod(toks[0])),
                                       static_cast<float>(std::stod(toks[1])),
                                       static_cast<float>(std::stod(toks[2])));
        } catch (...) {
            out.error = "OFF file: invalid coordinate at vertex " + std::to_string(i);
            return out;
        }
    }

    const std::size_t numPoints = out.positions.size();
    for (long long i = 0; i < numFaces; ++i) {
        auto line = nextContentLine(f);
        if (!line) {
            out.error = "OFF file ended while reading face " + std::to_string(i);
            return out;
        }
        auto toks = splitTokens(*line);
        if (toks.empty()) {
            out.error = "OFF file: face " + std::to_string(i) + " is empty";
            return out;
        }
        long long n = 0;
        try {
            n = std::stoll(toks[0]);
        } catch (...) {
            out.error = "OFF file: invalid face vertex count at face " + std::to_string(i);
            return out;
        }
        if (n < 3) {
            out.error = "OFF file: face " + std::to_string(i) + " has fewer than 3 vertices";
            return out;
        }
        if (static_cast<long long>(toks.size()) < n + 1) {
            out.error =
                "OFF file: face " + std::to_string(i) + " lists fewer indices than declared";
            return out;
        }

        std::vector<uint32_t> faceIdx;
        faceIdx.reserve(static_cast<std::size_t>(n));
        for (long long k = 0; k < n; ++k) {
            long long vi = 0;
            try {
                vi = std::stoll(toks[static_cast<std::size_t>(1 + k)]);
            } catch (...) {
                out.error = "OFF file: invalid vertex index in face " + std::to_string(i);
                return out;
            }
            if (vi < 0 || static_cast<std::size_t>(vi) >= numPoints) {
                out.error = "OFF file: face " + std::to_string(i) +
                            " references an out-of-range vertex index";
                return out;
            }
            faceIdx.push_back(static_cast<uint32_t>(vi));
        }

        // Fan-triangulate from the face's first vertex (planar/convex
        // assumption), matching polyhedron()'s own triangulation. Any
        // tokens past index n (e.g. COFF per-face color) are left unread.
        for (std::size_t k = 1; k + 1 < faceIdx.size(); ++k) {
            out.indices.push_back(faceIdx[0]);
            out.indices.push_back(faceIdx[k]);
            out.indices.push_back(faceIdx[k + 1]);
        }
    }

    if (out.positions.empty())
        out.error = "OFF file has no vertices";

    return out;
}

} // namespace chisel::io
