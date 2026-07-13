#include "import/SvgLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>

using namespace chisel::io;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

TEST_CASE("SvgLoader:rect becomes a 4-point closed contour", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/rect.svg"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    REQUIRE(poly.paths[0].size() == 4);
    REQUIRE(poly.points[0].x == Approx(1.0));
    REQUIRE(poly.points[0].y == Approx(2.0));
    REQUIRE(poly.points[2].x == Approx(4.0)); // x + width
    REQUIRE(poly.points[2].y == Approx(6.0)); // y + height
}

TEST_CASE("SvgLoader:circle is tessellated into a closed polygon", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/circle.svg"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    for (int idx : poly.paths[0]) {
        const auto& p = poly.points[static_cast<std::size_t>(idx)];
        double dist = std::hypot(p.x - 5.0, p.y - 5.0);
        REQUIRE(dist == Approx(2.0).margin(0.01));
    }
}

TEST_CASE("SvgLoader:polygon points= is a closed contour", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/polygon.svg"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    REQUIRE(poly.paths[0].size() == 4);
    REQUIRE(poly.points[2].x == Approx(4.0));
    REQUIRE(poly.points[2].y == Approx(4.0));
}

TEST_CASE("SvgLoader:path with M/L/Z becomes a closed contour", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/path.svg"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    // M0,0 L4,0 L4,4 L0,4 Z -> 4 distinct corners plus the Z-closing point
    // duplicating the start (matching DxfLoader/other closed-path callers'
    // "explicit closing vertex" shape is NOT assumed elsewhere, so just
    // check the corners are present).
    bool sawFarCorner = false;
    for (int idx : poly.paths[0]) {
        const auto& p = poly.points[static_cast<std::size_t>(idx)];
        if (std::abs(p.x - 4.0) < 0.001 && std::abs(p.y - 4.0) < 0.001)
            sawFarCorner = true;
    }
    REQUIRE(sawFarCorner);
}

TEST_CASE("SvgLoader:cubic Bezier curves are flattened into the contour", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/curve.svg"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    // A flattened curve segment contributes many points, not just the
    // Bezier's 2 endpoints — the path has 4 explicit anchor commands
    // (C, L, L, Z) but should produce well more than 4 points.
    REQUIRE(poly.paths[0].size() > 8);
}

TEST_CASE("SvgLoader:a file with only open shapes (line/polyline) reports a diagnostic",
          "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/open_only.svg"));
    REQUIRE_FALSE(poly.error.empty());
    REQUIRE(poly.paths.empty());
}

TEST_CASE("SvgLoader:missing file reports an error, not a crash", "[svg-loader][tier-e]") {
    auto poly = loadSvgPaths(fixture("import/does_not_exist.svg"));
    REQUIRE_FALSE(poly.error.empty());
}
