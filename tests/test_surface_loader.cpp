#include "import/SurfaceLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace chisel::io;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

// simple.dat is a 3x3 grid:
//   0 0 0
//   0 5 0
//   0 0 0
// Row 0 -> y=2 (far edge), row 2 -> y=0 (near edge); the peak sits at
// grid(r=1,c=1) -> (x=1, y=1, z=5).

TEST_CASE("SurfaceLoader:loads a valid grid and builds a closed solid",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/simple.dat"), /*center=*/false, /*invert=*/false);
    REQUIRE(mesh.error.empty());

    // 3x3 grid -> 9 top + 9 bottom vertices.
    REQUIRE(mesh.positions.size() == 18);
    // 4 interior cells * 4 tris (2 top + 2 bottom) + 8 boundary segments * 2
    // wall tris = 16 + 16 = 32 triangles.
    REQUIRE(mesh.indices.size() == 32 * 3);

    // Top-layer index for grid(1,1) is 1*3+1 = 4.
    const auto& peak = mesh.positions[4];
    REQUIRE(peak.x == Approx(1.0));
    REQUIRE(peak.y == Approx(1.0));
    REQUIRE(peak.z == Approx(5.0));
}

TEST_CASE("SurfaceLoader:center shifts the footprint and Z extent onto the origin",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/simple.dat"), /*center=*/true, /*invert=*/false);
    REQUIRE(mesh.error.empty());

    const auto& peak = mesh.positions[4]; // grid(1,1), the center cell
    REQUIRE(peak.x == Approx(0.0));
    REQUIRE(peak.y == Approx(0.0));
    REQUIRE(peak.z == Approx(2.5)); // (0 + 5) span, centered -> [-2.5, 2.5]
}

TEST_CASE("SurfaceLoader:invert flips heights about the grid's own maximum",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/simple.dat"), /*center=*/false, /*invert=*/true);
    REQUIRE(mesh.error.empty());

    REQUIRE(mesh.positions[4].z == Approx(0.0)); // was the peak (5) -> now the low point
    REQUIRE(mesh.positions[0].z == Approx(5.0)); // was 0 (a corner) -> now the high point
}

TEST_CASE("SurfaceLoader:non-square grid produces a watertight-count solid",
          "[surface-loader][tier-e]") {
    // wide.dat is a 2x4 grid — exercises the boundary-loop traversal (front/
    // right/back/left edges of very different lengths, and a degenerate
    // "left edge" of zero interior points since numRows-2 == 0) beyond the
    // single square 3x3 fixture every other test in this file uses.
    auto mesh = loadSurfaceMesh(fixture("surface/wide.dat"), false, false);
    REQUIRE(mesh.error.empty());

    // 2x4 grid -> 8 top + 8 bottom vertices.
    REQUIRE(mesh.positions.size() == 16);
    // (2-1)*(4-1)=3 interior cells * 4 tris, + (2*(2+4)-4)=8 boundary edges
    // * 2 wall tris = 12 + 16 = 28 triangles.
    REQUIRE(mesh.indices.size() == 28 * 3);
}

TEST_CASE("SurfaceLoader:comments and blank lines are ignored", "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/comments.dat"), false, false);
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 18); // same 3x3 shape as simple.dat
    REQUIRE(mesh.positions[4].z == Approx(5.0));
}

// ---------------------------------------------------------------------------
// PNG heightmaps (v3 Phase 4) — simple.png is a 3x3 8-bit grayscale image,
// same peak-at-center shape as simple.dat:
//   0   0   0
//   0  255   0
//   0   0   0
// Per OpenSCAD's own PNG-heightmap behavior, pixel value v maps to height
// v/255*100, so the center pixel (255) is height 100 and the rest are 0.
// ---------------------------------------------------------------------------
TEST_CASE("SurfaceLoader:PNG heightmap is dispatched by extension and scaled 0-255 to 0-100",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/simple.png"), /*center=*/false, /*invert=*/false);
    REQUIRE(mesh.error.empty());

    REQUIRE(mesh.positions.size() == 18); // 3x3 grid, same topology as simple.dat

    const auto& peak = mesh.positions[4]; // grid(1,1), top-layer index 1*3+1=4
    REQUIRE(peak.x == Approx(1.0));
    REQUIRE(peak.y == Approx(1.0));
    REQUIRE(peak.z == Approx(100.0));

    const auto& corner = mesh.positions[0]; // grid(0,0), a black (0) pixel
    REQUIRE(corner.z == Approx(0.0));
}

TEST_CASE("SurfaceLoader:PNG heightmap honors center and invert like .dat",
          "[surface-loader][tier-e]") {
    auto centered =
        loadSurfaceMesh(fixture("surface/simple.png"), /*center=*/true, /*invert=*/false);
    REQUIRE(centered.error.empty());
    REQUIRE(centered.positions[4].z == Approx(50.0)); // [0,100] span, centered -> peak at +50

    auto inverted =
        loadSurfaceMesh(fixture("surface/simple.png"), /*center=*/false, /*invert=*/true);
    REQUIRE(inverted.error.empty());
    REQUIRE(inverted.positions[4].z == Approx(0.0));   // was the peak -> now the low point
    REQUIRE(inverted.positions[0].z == Approx(100.0)); // was 0 -> now the high point
}

TEST_CASE("SurfaceLoader:PNG smaller than 2x2 reports a diagnostic, not a crash",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/too_small.png"), false, false);
    REQUIRE(mesh.error.empty() == false);
}

TEST_CASE("SurfaceLoader:missing PNG file reports a diagnostic, not a crash",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/does_not_exist.png"), false, false);
    REQUIRE(mesh.error.empty() == false);
}

TEST_CASE("SurfaceLoader:missing file reports an error, not a crash", "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/does_not_exist.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}

TEST_CASE("SurfaceLoader:inconsistent row length reports an error", "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/inconsistent.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
}

TEST_CASE("SurfaceLoader:a single-row grid reports an error", "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/tiny.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
}

TEST_CASE("SurfaceLoader:a comments-only file reports an error", "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/empty.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
}

TEST_CASE("SurfaceLoader:a non-numeric token mid-row reports an error instead of truncating",
          "[surface-loader][tier-e]") {
    auto mesh = loadSurfaceMesh(fixture("surface/malformed_token.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
}

TEST_CASE("SurfaceLoader:a non-numeric token in the very first data row still reports an error",
          "[surface-loader][tier-e]") {
    // Before the fix, a malformed first row silently set expectedCols from
    // the truncated read count and the file "loaded" with corrupted data.
    auto mesh = loadSurfaceMesh(fixture("surface/malformed_first_row.dat"), false, false);
    REQUIRE_FALSE(mesh.error.empty());
}
