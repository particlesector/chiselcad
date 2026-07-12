#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "import/StlLoader.h"
#include <filesystem>

using namespace chisel::io;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#  error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

TEST_CASE("StlLoader:loads a valid ASCII triangle", "[stl-loader][tier-e]") {
    auto mesh = loadStlRaw(fixture("import/triangle.stl"));
    REQUIRE(mesh.error.empty());
    REQUIRE(mesh.positions.size() == 3);
    REQUIRE(mesh.normals.size() == 3);
    REQUIRE(mesh.indices.size() == 3);
    REQUIRE(mesh.positions[0].x == Approx(0.0));
    REQUIRE(mesh.positions[1].x == Approx(1.0));
    REQUIRE(mesh.positions[2].y == Approx(1.0));
    REQUIRE(mesh.normals[0].z == Approx(1.0));
}

TEST_CASE("StlLoader:missing file reports an error, not a crash", "[stl-loader][tier-e]") {
    auto mesh = loadStlRaw(fixture("import/does_not_exist.stl"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}

TEST_CASE("StlLoader:ASCII file with no facets reports an error", "[stl-loader][tier-e]") {
    auto mesh = loadStlRaw(fixture("import/empty.stl"));
    REQUIRE_FALSE(mesh.error.empty());
}

TEST_CASE("StlLoader:binary file with zero triangles reports an error", "[stl-loader][tier-e]") {
    // A legal-but-empty binary STL (80-byte header + triCount=0) must not
    // silently succeed with empty geometry — that would let import() build
    // an invisible Mesh leaf with no diagnostic at all.
    auto mesh = loadStlRaw(fixture("import/empty_binary.stl"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}

TEST_CASE("StlLoader:corrupt triangle-count header is rejected, not a DoS", "[stl-loader][bugfix]") {
    // corrupt_tricount.stl claims 0xFFFFFFFF triangles (~4 billion) in an 80-
    // byte header on a file that is only 184 bytes total. Before the fix,
    // loadBinary would attempt to loop triCount times and grow its vectors to
    // tens/hundreds of GB before ever observing the stream's fail state; the
    // fix rejects triCount against the actual file size up front, so this
    // must return immediately with an error instead of hanging or OOMing.
    auto mesh = loadStlRaw(fixture("import/corrupt_tricount.stl"));
    REQUIRE_FALSE(mesh.error.empty());
    REQUIRE(mesh.positions.empty());
}
