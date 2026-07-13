#include "import/OffLoader.h"

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

TEST_CASE("OffLoader:loads a valid triangle", "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/triangle.off"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 3);
    REQUIRE(mesh.indices.size() == 3);
    REQUIRE(mesh.positions[0].x == Approx(0.0));
    REQUIRE(mesh.positions[1].x == Approx(1.0));
    REQUIRE(mesh.positions[2].y == Approx(1.0));
}

TEST_CASE("OffLoader:fan-triangulates an n-gon face", "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/quad.off"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 4);
    // One quad -> 2 triangles -> 6 indices, fanned from vertex 0: (0,1,2),(0,2,3).
    REQUIRE(mesh.indices.size() == 6);
    REQUIRE(mesh.indices[0] == 0);
    REQUIRE(mesh.indices[1] == 1);
    REQUIRE(mesh.indices[2] == 2);
    REQUIRE(mesh.indices[3] == 0);
    REQUIRE(mesh.indices[4] == 2);
    REQUIRE(mesh.indices[5] == 3);
}

TEST_CASE("OffLoader:accepts header and counts sharing one line", "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/combined_header.off"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 3);
    REQUIRE(mesh.indices.size() == 3);
}

TEST_CASE("OffLoader:ignores trailing per-vertex/per-face color data (COFF)",
          "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/colored.off"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 3);
    // If trailing color tokens leaked into the position/index stream this
    // would either fail to parse or produce wrong coordinates/out-of-range
    // indices — asserting the exact same triangle as triangle.off proves
    // the color columns were skipped cleanly.
    REQUIRE(mesh.positions[0].x == Approx(0.0));
    REQUIRE(mesh.positions[1].x == Approx(1.0));
    REQUIRE(mesh.positions[2].y == Approx(1.0));
    REQUIRE(mesh.indices.size() == 3);
}

TEST_CASE("OffLoader:missing 'OFF' header is rejected", "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/malformed.off"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}

TEST_CASE("OffLoader:missing file reports an error, not a crash", "[off-loader][tier-e]") {
    auto mesh = loadOffMesh(fixture("import/does_not_exist.off"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}
