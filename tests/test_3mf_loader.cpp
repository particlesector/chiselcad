#include "import/ThreeMfLoader.h"

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

TEST_CASE("ThreeMfLoader:loads a DEFLATE-compressed tetrahedron", "[3mf-loader][tier-e]") {
    auto mesh = loadThreeMfMesh(fixture("import/tetra.3mf"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 4);
    REQUIRE(mesh.indices.size() == 12); // 4 triangles * 3
    REQUIRE(mesh.positions[1].x == Approx(1.0));
    REQUIRE(mesh.positions[3].z == Approx(1.0));
}

TEST_CASE("ThreeMfLoader:loads a STORED (uncompressed) archive identically",
          "[3mf-loader][tier-e]") {
    auto mesh = loadThreeMfMesh(fixture("import/tetra_stored.3mf"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 4);
    REQUIRE(mesh.indices.size() == 12);
}

TEST_CASE("ThreeMfLoader:not a ZIP archive reports a diagnostic, not a crash",
          "[3mf-loader][tier-e]") {
    auto mesh = loadThreeMfMesh(fixture("import/corrupt.3mf"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}

TEST_CASE("ThreeMfLoader:missing file reports an error, not a crash", "[3mf-loader][tier-e]") {
    auto mesh = loadThreeMfMesh(fixture("import/does_not_exist.3mf"));
    REQUIRE_FALSE(mesh.error.empty());
}
