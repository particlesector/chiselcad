#include "SurfaceLoader.h"

#include "util/PathSuffix.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#include <stb/stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

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
        if (hashPos != std::string::npos)
            line.erase(hashPos);

        std::istringstream ss(line);
        std::vector<double> row;
        double v;
        while (ss >> v)
            row.push_back(v);
        // A failure that isn't plain end-of-stream means extraction stopped
        // on a non-numeric token partway through the row (e.g. "1 2 abc 4")
        // rather than having cleanly consumed every token — report it
        // instead of silently truncating the row to whatever was read so far.
        if (ss.fail() && !ss.eof()) {
            grid.error = "malformed value in data row " + std::to_string(grid.rows.size() + 1);
            return grid;
        }
        if (row.empty())
            continue; // blank or comment-only line

        if (grid.rows.empty()) {
            expectedCols = row.size();
        } else if (row.size() != expectedCols) {
            grid.error = "inconsistent row length at data row " +
                         std::to_string(grid.rows.size() + 1) + ": expected " +
                         std::to_string(expectedCols) + " values, got " +
                         std::to_string(row.size());
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

// PNG heightmap: matches OpenSCAD's own surface()-from-PNG behavior — each
// pixel's height is its linear sRGB luminance (Rec. 709 weights,
// Y = 0.2126 R + 0.7152 G + 0.0722 B, computed from 8-bit channel values
// forced to RGB by requesting STBI_rgb regardless of the source PNG's actual
// channel count, so grayscale/palette/RGBA inputs all take the same path),
// linearly scaled from [0, 255] to [0, 100] — i.e. black = 0, white = 100.
// Alpha, if present, is ignored (not requested). loadSurfaceMesh's row index
// maps directly to Y (row 0 -> min Y), matching real OpenSCAD's own .dat
// convention (confirmed against a live export — see the comment there), so
// to keep a PNG heightmap the same way up as the equivalent hand-written
// .dat, image row 0 (the top row of pixels) must land at *max* Y: flip
// during storage into grid row (height-1-r), the same
// `data[x + width*(height-1-y)]` flip SurfaceNode::convert_image() does.
HeightGrid pngToGrid(const std::filesystem::path& path) {
    HeightGrid grid;

    // Read the file via std::ifstream (which takes std::filesystem::path
    // directly and so opens it via the native wide-char API on Windows)
    // rather than handing stbi_load() a narrowed path.string() — the latter
    // mangles non-ASCII filenames on Windows (stbi_load's char* filename is
    // interpreted via the ANSI codepage, not UTF-8), unlike the .dat branch
    // above which already opens the same `path` correctly through ifstream.
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        grid.error = "Cannot open file: " + path.string();
        return grid;
    }
    std::vector<unsigned char> fileBytes((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
    f.close();

    int width = 0, height = 0, sourceChannels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &width, &height,
                              &sourceChannels, STBI_rgb);
    if (!pixels) {
        grid.error = "cannot decode PNG: " + path.string();
        return grid;
    }

    if (width < 2 || height < 2) {
        stbi_image_free(pixels);
        grid.error = "surface() requires at least a 2x2 grid of height values";
        return grid;
    }

    grid.rows.resize(static_cast<std::size_t>(height));
    for (int r = 0; r < height; ++r) {
        std::vector<double>& row = grid.rows[static_cast<std::size_t>(height - 1 - r)];
        row.resize(static_cast<std::size_t>(width));
        for (int c = 0; c < width; ++c) {
            const unsigned char* px = pixels + (static_cast<std::size_t>(r) * width + c) * 3;
            const double luminance = 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2];
            row[static_cast<std::size_t>(c)] = luminance / 255.0 * 100.0;
        }
    }
    stbi_image_free(pixels);
    return grid;
}

} // namespace

RawSurfaceMesh loadSurfaceMesh(const std::filesystem::path& path, bool center, bool invert) {
    RawSurfaceMesh out;

    HeightGrid grid;
    if (chisel::util::hasSuffixCI(path, ".png")) {
        grid = pngToGrid(path);
    } else {
        std::ifstream f(path);
        if (!f) {
            out.error = "Cannot open file: " + path.string();
            return out;
        }
        grid = parseGrid(f);
    }
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
    // Real OpenSCAD's base is always exactly one unit below the data's own
    // minimum (SurfaceNode::createGeometry(): `min_val = data.min_value() -
    // 1`), never clamped to 0 and never affected by `center` — confirmed
    // against a live OpenSCAD export: surface-simple.dat's heights span
    // [0,3], and the exported STL's flat bottom face sits at z=-1, not z=0.
    // `center` only ever offsets X/Y (`ox`/`oy` in that same function); Z is
    // left exactly as the raw data (plus this base), never centered either.
    const double bottomZ = effMinH - 1.0;
    const double xOff = center ? -static_cast<double>(numCols - 1) / 2.0 : 0.0;
    const double yOff = center ? -static_cast<double>(numRows - 1) / 2.0 : 0.0;

    auto heightAt = [&](std::size_t r, std::size_t c) {
        double h = grid.rows[r][c];
        return invert ? (maxH - h) : h;
    };
    auto xAt = [&](std::size_t c) {
        return static_cast<double>(c) + xOff;
    };
    auto yAt = [&](std::size_t r) {
        // Real OpenSCAD maps the data's row index directly to Y (row 0 ->
        // min Y, last row -> max Y) — confirmed against a live OpenSCAD
        // export of an asymmetric grid. Not flipped, despite this file's
        // PNG loader comment above claiming a "first row = far/max-Y edge"
        // convention for .dat too; that claim was itself wrong.
        return static_cast<double>(r) + yOff;
    };

    const std::size_t gridN = numRows * numCols;
    const std::size_t numCells = (numRows - 1) * (numCols - 1);
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

    auto topIdx = [&](std::size_t r, std::size_t c) {
        return static_cast<uint32_t>(r * numCols + c);
    };
    auto bottomIdx = [&](std::size_t r, std::size_t c) {
        return static_cast<uint32_t>(gridN + r * numCols + c);
    };
    auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
    };

    // Top surface (+Z-facing) and bottom (-Z-facing — reversed winding),
    // one quad (2 triangles) per grid cell. Winding derived so that, since
    // grid row r now maps directly to y = r (row 0 = min Y — see yAt()
    // above), [topIdx(r,c), topIdx(r,c+1), topIdx(r+1,c)] has an outward
    // (+Z) normal: (C-A)x(B-A) with A=(c,r), B=(c,r+1), C=(c+1,r) in (x,y)
    // is (1,0)x(0,1) = +1 (CCW from +Z).
    for (std::size_t r = 0; r + 1 < numRows; ++r) {
        for (std::size_t c = 0; c + 1 < numCols; ++c) {
            pushTri(topIdx(r, c), topIdx(r, c + 1), topIdx(r + 1, c));
            pushTri(topIdx(r + 1, c), topIdx(r, c + 1), topIdx(r + 1, c + 1));

            pushTri(bottomIdx(r, c), bottomIdx(r + 1, c), bottomIdx(r, c + 1));
            pushTri(bottomIdx(r + 1, c), bottomIdx(r + 1, c + 1), bottomIdx(r, c + 1));
        }
    }

    // Side walls: walk the grid's outer boundary counter-clockwise (as seen
    // from +Z) — near edge (r=0) toward +X, right edge (c=numCols-1)
    // toward +Y, far edge (r=numRows-1) toward -X, left edge (c=0) toward -Y
    // — and connect each edge's top/bottom vertices with outward-facing
    // winding. Each corner is added exactly once (by whichever segment
    // reaches it first); the loop closes via `(i+1) % boundary.size()`.
    std::vector<std::pair<std::size_t, std::size_t>> boundary;
    boundary.reserve(numBoundaryEdges);
    for (std::size_t c = 0; c < numCols; ++c)
        boundary.emplace_back(0, c);
    for (std::size_t r = 1; r < numRows; ++r)
        boundary.emplace_back(r, numCols - 1);
    for (std::size_t c = numCols - 1; c-- > 0;)
        boundary.emplace_back(numRows - 1, c);
    for (std::size_t r = numRows - 1; r-- > 1;)
        boundary.emplace_back(r, 0);

    for (std::size_t i = 0; i < boundary.size(); ++i) {
        auto [ra, ca] = boundary[i];
        auto [rb, cb] = boundary[(i + 1) % boundary.size()];
        uint32_t topA = topIdx(ra, ca), topB = topIdx(rb, cb);
        uint32_t botA = bottomIdx(ra, ca), botB = bottomIdx(rb, cb);
        pushTri(topA, botA, botB);
        pushTri(topA, botB, topB);
    }

    return out;
}

} // namespace chisel::io
