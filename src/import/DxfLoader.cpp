#include "DxfLoader.h"

#include <cmath>
#include <fstream>
#include <optional>

namespace chisel::io {

namespace {

constexpr int kCircleSegments = 64;
constexpr double kPi = 3.14159265358979323846;

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// One DXF entity: its type ("LINE", "CIRCLE", "LWPOLYLINE", ...) plus every
// (group code, value) pair recorded between its own "0" line and the next.
struct Entity {
    std::string type;
    std::vector<std::pair<int, std::string>> fields;

    std::optional<double> getDouble(int code) const {
        for (const auto& [c, v] : fields)
            if (c == code) {
                try {
                    return std::stod(v);
                } catch (...) {
                    return std::nullopt;
                }
            }
        return std::nullopt;
    }
    int getInt(int code, int def = 0) const {
        for (const auto& [c, v] : fields)
            if (c == code) {
                try {
                    return std::stoi(v);
                } catch (...) {
                    return def;
                }
            }
        return def;
    }
    std::string getString(int code, const std::string& def = "") const {
        for (const auto& [c, v] : fields)
            if (c == code)
                return v;
        return def;
    }
};

} // namespace

RawPolygon2D loadDxfPaths(const std::filesystem::path& path, const std::string& layer) {
    RawPolygon2D out;

    std::ifstream f(path);
    if (!f) {
        out.error = "Cannot open file: " + path.string();
        return out;
    }

    // DXF ASCII is strictly two physical lines per (group code, value) pair.
    std::vector<std::pair<int, std::string>> pairs;
    std::string codeLine, valueLine;
    while (std::getline(f, codeLine)) {
        if (!std::getline(f, valueLine))
            break; // truncated trailing group code
        std::string codeStr = trim(codeLine);
        if (codeStr.empty())
            continue;
        int code = 0;
        try {
            code = std::stoi(codeStr);
        } catch (...) {
            continue;
        }
        pairs.emplace_back(code, trim(valueLine));
    }
    if (pairs.empty()) {
        out.error = "DXF file has no content";
        return out;
    }

    // Group pairs into entities, only within the ENTITIES section.
    bool inEntitiesSection = false;
    std::optional<Entity> current;
    std::vector<Entity> entities;

    auto flushCurrent = [&] {
        if (current && !current->type.empty())
            entities.push_back(std::move(*current));
        current.reset();
    };

    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const auto& [code, value] = pairs[i];
        if (code == 0) {
            if (value == "SECTION") {
                flushCurrent();
                inEntitiesSection = (i + 1 < pairs.size() && pairs[i + 1].first == 2 &&
                                     pairs[i + 1].second == "ENTITIES");
                continue;
            }
            if (value == "ENDSEC") {
                flushCurrent();
                inEntitiesSection = false;
                continue;
            }
            if (value == "EOF") {
                flushCurrent();
                break;
            }
            flushCurrent();
            if (inEntitiesSection) {
                current = Entity{};
                current->type = value;
            }
            continue;
        }
        if (current)
            current->fields.emplace_back(code, value);
    }
    flushCurrent();

    std::vector<std::vector<glm::vec2>> closedPaths;
    auto layerMatches = [&](const Entity& e) {
        return layer.empty() || e.getString(8) == layer;
    };

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Entity& e = entities[i];
        if (!layerMatches(e))
            continue;

        if (e.type == "CIRCLE") {
            double cx = e.getDouble(10).value_or(0.0);
            double cy = e.getDouble(20).value_or(0.0);
            double r = e.getDouble(40).value_or(0.0);
            if (r <= 0.0)
                continue;
            std::vector<glm::vec2> pts;
            pts.reserve(kCircleSegments);
            for (int s = 0; s < kCircleSegments; ++s) {
                double a = 2.0 * kPi * static_cast<double>(s) / kCircleSegments;
                pts.emplace_back(static_cast<float>(cx + r * std::cos(a)),
                                 static_cast<float>(cy + r * std::sin(a)));
            }
            closedPaths.push_back(std::move(pts));
        } else if (e.type == "LWPOLYLINE") {
            bool closed = (e.getInt(70) & 1) != 0;
            if (!closed)
                continue;
            // Vertices are repeated 10 (x) / 20 (y) group pairs, in file
            // order, with 20 always directly following its 10 per the DXF
            // spec (any bulge/width groups for that vertex come after).
            std::vector<glm::vec2> pts;
            bool havePendingX = false;
            double pendingX = 0.0;
            for (const auto& [code, value] : e.fields) {
                if (code == 10) {
                    try {
                        pendingX = std::stod(value);
                        havePendingX = true;
                    } catch (...) {
                    }
                } else if (code == 20 && havePendingX) {
                    try {
                        pts.emplace_back(static_cast<float>(pendingX),
                                         static_cast<float>(std::stod(value)));
                    } catch (...) {
                    }
                    havePendingX = false;
                }
            }
            if (pts.size() >= 3)
                closedPaths.push_back(std::move(pts));
        } else if (e.type == "POLYLINE") {
            bool closed = (e.getInt(70) & 1) != 0;
            if (!closed)
                continue;
            std::vector<glm::vec2> pts;
            std::size_t j = i + 1;
            for (; j < entities.size() && entities[j].type == "VERTEX"; ++j) {
                double vx = entities[j].getDouble(10).value_or(0.0);
                double vy = entities[j].getDouble(20).value_or(0.0);
                pts.emplace_back(static_cast<float>(vx), static_cast<float>(vy));
            }
            if (pts.size() >= 3)
                closedPaths.push_back(std::move(pts));
        }
        // LINE / ARC / bare VERTEX / SEQEND: not a closed loop on their
        // own — skipped, see DxfLoader.h's scope note.
    }

    if (closedPaths.empty()) {
        out.error = "DXF file contains no closed LWPOLYLINE/POLYLINE/CIRCLE entities" +
                    (layer.empty() ? std::string() : (" on layer '" + layer + "'"));
        return out;
    }

    for (auto& path : closedPaths) {
        std::vector<int> indices;
        indices.reserve(path.size());
        for (auto& pt : path) {
            indices.push_back(static_cast<int>(out.points.size()));
            out.points.push_back(pt);
        }
        out.paths.push_back(std::move(indices));
    }
    return out;
}

} // namespace chisel::io
