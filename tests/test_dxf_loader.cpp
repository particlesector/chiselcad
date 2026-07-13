#include "import/DxfLoader.h"

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

TEST_CASE("DxfLoader:loads a closed LWPOLYLINE as one contour", "[dxf-loader][tier-e]") {
    auto poly = loadDxfPaths(fixture("import/square.dxf"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    REQUIRE(poly.paths[0].size() == 4);
    REQUIRE(poly.points.size() == 4);
    REQUIRE(poly.points[0].x == Approx(0.0));
    REQUIRE(poly.points[2].x == Approx(1.0));
    REQUIRE(poly.points[2].y == Approx(1.0));
}

TEST_CASE("DxfLoader:closed POLYLINE/VERTEX/SEQEND group is one contour", "[dxf-loader][tier-e]") {
    auto poly = loadDxfPaths(fixture("import/polyline_vertex.dxf"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    REQUIRE(poly.paths[0].size() == 3);
    REQUIRE(poly.points[1].x == Approx(2.0));
}

TEST_CASE("DxfLoader:CIRCLE is tessellated into a closed polygon", "[dxf-loader][tier-e]") {
    auto poly = loadDxfPaths(fixture("import/circle.dxf"));
    REQUIRE(poly.error.empty());
    REQUIRE(poly.paths.size() == 1);
    REQUIRE(poly.paths[0].size() >=
            16); // fixed tessellation resolution, just check "many segments"
    // Every point should sit at radius ~2 from (5,5).
    for (int idx : poly.paths[0]) {
        const auto& p = poly.points[static_cast<std::size_t>(idx)];
        double dist = std::hypot(p.x - 5.0, p.y - 5.0);
        REQUIRE(dist == Approx(2.0).margin(0.01));
    }
}

TEST_CASE("DxfLoader:a file with only open entities (LINE) reports a diagnostic",
          "[dxf-loader][tier-e]") {
    auto poly = loadDxfPaths(fixture("import/open_only.dxf"));
    REQUIRE_FALSE(poly.error.empty());
    REQUIRE(poly.paths.empty());
}

TEST_CASE("DxfLoader:layer= filters to entities on that layer only", "[dxf-loader][tier-e]") {
    auto all = loadDxfPaths(fixture("import/layered.dxf"));
    REQUIRE(all.error.empty());
    REQUIRE(all.paths.size() == 2);

    auto onlyOutline = loadDxfPaths(fixture("import/layered.dxf"), "outline");
    REQUIRE(onlyOutline.error.empty());
    REQUIRE(onlyOutline.paths.size() == 1);
    REQUIRE(onlyOutline.points[0].x == Approx(0.0));

    auto onlyHoles = loadDxfPaths(fixture("import/layered.dxf"), "holes");
    REQUIRE(onlyHoles.error.empty());
    REQUIRE(onlyHoles.paths.size() == 1);
    REQUIRE(onlyHoles.points[0].x == Approx(5.0));
}

TEST_CASE("DxfLoader:missing file reports an error, not a crash", "[dxf-loader][tier-e]") {
    auto poly = loadDxfPaths(fixture("import/does_not_exist.dxf"));
    REQUIRE_FALSE(poly.error.empty());
}
