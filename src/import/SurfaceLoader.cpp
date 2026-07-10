#include "SurfaceLoader.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace chisel::io {

namespace {

struct HeightGrid {
    std::vector<std::vector<double>> rows; // rows[r][c], uniform column count
    std::string error;
};

HeightGrid parseGrid(std::ifstream& f) {
    HeightGrid grid;
    std::string line;
    std::size_t expectedCols = 0;

    while (std::getline(f, line)) {
        auto hashPos = line.find('#');
        if (hashPos != std::string::npos) line.erase(hashPos);

        std::istringstream ss(line);
        std::vector<double> row;
        double v;
        while (ss >> v) row.push_back(v);
        if (row.empty()) continue; // blank or comment-only line

        if (grid.rows.empty()) {
            expectedCols = row.size();
        } else if (row.size() != expectedCols) {
            grid.error = "inconsistent row length at data row " + std::to_string(grid.rows.size() + 1) +
                         ": expected " + std::to_string(expectedCols) +
                         " values, got " + std::to_string(row.size());
            return grid;
        }
        grid.rows.push_back(std::move(row));
    }

    if (grid.rows.empty())
        grid.error = "no height data found";
    else if (grid.rows.size() < 2 || expectedCols < 2)
        grid.error = "surface() requires at least a 2x2 grid of height values";

    return grid;
}

} // namespace

RawSurfaceMesh loadSurfaceMesh(const std::filesystem::path& path, bool center, bool invert) {
    RawSurfaceMesh out;

    std::ifstream f(path);
    if (!f) {
        out.error = "Cannot open file: " + path.string();
        return out;
    }

    HeightGrid grid = parseGrid(f);
    if (!grid.error.empty()) {
        out.error = std::move(grid.error);
        return out;
    }

    const std::size_t numRows = grid.rows.size();
    const std::size_t numCols = grid.rows[0].size();

    double minH = std::numeric_limits<double>::infinity();
    double maxH = -std::numeric_limits<double>::infinity();
    for (const auto& row : grid.rows)
        for (double v : row) {
            minH = std::min(minH, v);
            maxH = std::max(maxH, v);
        }

    // After invert (h' = maxH - h), the minimum is always exactly 0 (at the
    // point that was the original maximum) — only the non-inverted case can
    // have a negative effective minimum.
    const double effMinH = invert ? 0.0 : minH;
    const double effMaxH = invert ? (maxH - minH) : maxH;
    // Base at 0 in the common case (all-non-negative heights); if the data
    // dips below 0, drop the base to match so the solid never folds back
    // through its own top surface.
    const double baseZ = std::min(0.0, effMinH);
    const double xOff  = center ? -static_cast<double>(numCols - 1) / 2.0 : 0.0;
    const double yOff  = center ? -static_cast<double>(numRows - 1) / 2.0 : 0.0;
    const double zOff  = center ? -(baseZ + effMaxH) / 2.0 : 0.0;
    const double bottomZ = baseZ + zOff;

    auto heightAt = [&](std::size_t r, std::size_t c) {
        double h = grid.rows[r][c];
        return (invert ? (maxH - h) : h) + zOff;
    };
    auto xAt = [&](std::size_t c) { return static_cast<double>(c) + xOff; };
    auto yAt = [&](std::size_t r) { return static_cast<double>(numRows - 1 - r) + yOff; };

    const std::size_t gridN          = numRows * numCols;
    const std::size_t numCells       = (numRows - 1) * (numCols - 1);
    const std::size_t numBoundaryEdges = 2 * (numRows + numCols) - 4;

    out.positions.reserve(gridN * 2);
    // 4 triangles (2 top + 2 bottom) per interior cell, 2 wall triangles per
    // boundary edge, 3 indices per triangle.
    out.indices.reserve(numCells * 4 * 3 + numBoundaryEdges * 2 * 3);
    // Top layer at index r*numCols+c, following the height data.
    for (std::size_t r = 0; r < numRows; ++r)
        for (std::size_t c = 0; c < numCols; ++c)
            out.positions.emplace_back(static_cast<float>(xAt(c)), static_cast<float>(yAt(r)),
                                        static_cast<float>(heightAt(r, c)));
    // Bottom layer at index gridN + r*numCols+c, flat at bottomZ — same X/Y
    // grid as the top layer (not just 4 corners) so the side walls below
    // meet it with no T-junctions.
    for (std::size_t r = 0; r < numRows; ++r)
        for (std::size_t c = 0; c < numCols; ++c)
            out.positions.emplace_back(static_cast<float>(xAt(c)), static_cast<float>(yAt(r)),
                                        static_cast<float>(bottomZ));

    auto topIdx    = [&](std::size_t r, std::size_t c) { return static_cast<uint32_t>(r * numCols + c); };
    auto bottomIdx = [&](std::size_t r, std::size_t c) { return static_cast<uint32_t>(gridN + r * numCols + c); };
    auto pushTri   = [&](uint32_t a, uint32_t b, uint32_t c) {
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
    };

    // Top surface (+Z-facing) and bottom (-Z-facing — reversed winding),
    // one quad (2 triangles) per grid cell. Winding derived so that, since
    // grid row r maps to y = numRows-1-r (row 0 = max Y), [topIdx(r,c),
    // topIdx(r+1,c), topIdx(r,c+1)] has an outward (+Z) normal.
    for (std::size_t r = 0; r + 1 < numRows; ++r) {
        for (std::size_t c = 0; c + 1 < numCols; ++c) {
            pushTri(topIdx(r, c),     topIdx(r + 1, c),     topIdx(r, c + 1));
            pushTri(topIdx(r + 1, c), topIdx(r + 1, c + 1), topIdx(r, c + 1));

            pushTri(bottomIdx(r, c),     bottomIdx(r, c + 1),     bottomIdx(r + 1, c));
            pushTri(bottomIdx(r + 1, c), bottomIdx(r, c + 1),     bottomIdx(r + 1, c + 1));
        }
    }

    // Side walls: walk the grid's outer boundary counter-clockwise (as seen
    // from +Z) — front edge (r=numRows-1) toward +X, right edge (c=numCols-1)
    // toward +Y, back edge (r=0) toward -X, left edge (c=0) toward -Y — and
    // connect each edge's top/bottom vertices with outward-facing winding.
    // Each corner is added exactly once (by whichever segment reaches it
    // first); the loop closes via `(i+1) % boundary.size()`.
    std::vector<std::pair<std::size_t, std::size_t>> boundary;
    boundary.reserve(numBoundaryEdges);
    for (std::size_t c = 0; c < numCols; ++c)
        boundary.emplace_back(numRows - 1, c);
    for (std::size_t r = numRows - 1; r-- > 0; )
        boundary.emplace_back(r, numCols - 1);
    for (std::size_t c = numCols - 1; c-- > 0; )
        boundary.emplace_back(0, c);
    for (std::size_t r = 1; r + 1 < numRows; ++r)
        boundary.emplace_back(r, 0);

    for (std::size_t i = 0; i < boundary.size(); ++i) {
        auto [ra, ca] = boundary[i];
        auto [rb, cb] = boundary[(i + 1) % boundary.size()];
        uint32_t topA = topIdx(ra, ca),    topB = topIdx(rb, cb);
        uint32_t botA = bottomIdx(ra, ca), botB = bottomIdx(rb, cb);
        pushTri(topA, botA, botB);
        pushTri(topA, botB, topB);
    }

    return out;
}

} // namespace chisel::io
