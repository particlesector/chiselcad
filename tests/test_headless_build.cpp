#include "app/BuildStats.h"
#include "app/HeadlessBuild.h"
#include "csg/MeshCache.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>

using namespace chisel::app;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

// ---------------------------------------------------------------------------
// runBuild() — the same synchronous pipeline both MeshBuilder (GUI, async)
// and chiselcad_cli (headless) call. These exercise it directly against a
// real Manifold build, which chiselcad_tests couldn't do before chiselcad_
// core existed (MeshEvaluator.cpp wasn't part of the old test target).
// ---------------------------------------------------------------------------
TEST_CASE("runBuild succeeds on a simple solid", "[headless]") {
    chisel::csg::MeshCache cache;
    BuildResult result = runBuild(fixture("headless/cube.scad"), {}, {}, cache);

    REQUIRE(result.ok());
    CHECK(result.errorMsg.empty());
    CHECK(result.volume == Approx(2.0 * 3.0 * 4.0).margin(1e-6));
    CHECK(result.triCount > 0);
    CHECK(result.vertCount > 0);
    CHECK(result.realIndexCount == result.indices.size());
}

TEST_CASE("runBuild reports parse errors instead of throwing", "[headless]") {
    chisel::csg::MeshCache cache;
    BuildResult result = runBuild(fixture("headless/broken.scad"), {}, {}, cache);

    REQUIRE_FALSE(result.ok());
    CHECK(result.errorMsg == "Parse errors");
    CHECK(result.volume == 0.0);
    CHECK_FALSE(result.diags.empty());

    bool hasError = false;
    for (const auto& d : result.diags)
        if (d.level == chisel::lang::DiagLevel::Error)
            hasError = true;
    CHECK(hasError);
}

// ---------------------------------------------------------------------------
// v3.9 geometry bugfixes — found via volumetric corpus comparison against
// real OpenSCAD (docs/roadmap.md v3.9). These need a real Manifold build to
// verify (mesh/volume output, not just CsgLeaf params), which chiselcad_core
// now makes possible for chiselcad_tests.
// ---------------------------------------------------------------------------
TEST_CASE("runBuild: per-node $fa/$fs override actually changes tessellation",
          "[headless][v39][bugfix]") {
    // Before this fix, PrimitiveGen::resolveSegments only read a per-node
    // $fn override; $fa/$fs overrides were silently ignored in favor of the
    // *global* $fa/$fs, so sphere(r=5, $fa=40, $fs=0.3) rendered at the
    // global default resolution regardless of its own $fa/$fs. Both fixtures
    // use the same radius; a coarse $fa/$fs must produce far fewer
    // triangles than a fine one, or this override isn't taking effect.
    chisel::csg::MeshCache cache;
    BuildResult coarse = runBuild(fixture("headless/sphere_fa_coarse.scad"), {}, {}, cache);
    BuildResult fine   = runBuild(fixture("headless/sphere_fa_fine.scad"), {}, {}, cache);

    REQUIRE(coarse.ok());
    REQUIRE(fine.ok());
    CHECK(coarse.triCount < fine.triCount / 2);
    // Both approximate the same sphere (r=5, volume 4/3*pi*r^3 ≈ 523.6) from
    // inside, so more segments should mean a volume closer to that ideal.
    constexpr double kIdealVolume = (4.0 / 3.0) * 3.14159265358979323846 * 5.0 * 5.0 * 5.0;
    CHECK(std::abs(fine.volume - kIdealVolume) < std::abs(coarse.volume - kIdealVolume));
}

TEST_CASE("runBuild: cylinder r2 defaults to 1.0 independently of r1, not mirroring it",
          "[headless][v39][bugfix]") {
    // Confirmed against real OpenSCAD: cylinder(h=5, r1=5) (r2 unset) tapers
    // from r1=5 down to r2=1, not a uniform r=5 cylinder. Before this fix,
    // PrimitiveGen defaulted an unset r2 to r1's value instead of 1.0.
    chisel::csg::MeshCache cache;
    BuildResult result = runBuild(fixture("headless/cylinder_r1_only.scad"), {}, {}, cache);
    REQUIRE(result.ok());

    // Frustum volume = (pi*h/3)*(r1^2 + r1*r2 + r2^2) with r1=5, r2=1, h=5.
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kExpectedFrustumVolume = (kPi * 5.0 / 3.0) * (25.0 + 5.0 * 1.0 + 1.0);
    // A uniform r=5 cylinder (the pre-fix bug) would give pi*25*5 ≈ 392.7 —
    // clearly distinct from the ~162.3 frustum volume expected here.
    CHECK(result.volume == Approx(kExpectedFrustumVolume).margin(1.0));
}

TEST_CASE("runBuild honors AbortFn by returning early", "[headless]") {
    chisel::csg::MeshCache cache;
    auto alwaysAbort = [] { return true; };
    BuildResult result = runBuild(fixture("headless/cube.scad"), {}, {}, cache, {}, alwaysAbort);

    // Aborted before meshing ever ran — no geometry, no error either (it's
    // a cancellation, not a failure). See MeshBuilder::buildOne()'s comment
    // on why storing this kind of partial result is still safe.
    CHECK(result.triCount == 0);
    CHECK(result.errorMsg.empty());
}

// ---------------------------------------------------------------------------
// buildResultToJson() — the --stats serialization shared by chiselcad_cli
// and, eventually, compat-suite golden-file comparisons.
// ---------------------------------------------------------------------------
TEST_CASE("buildResultToJson round-trips a successful build", "[headless]") {
    chisel::csg::MeshCache cache;
    BuildResult result = runBuild(fixture("headless/cube.scad"), {}, {}, cache);

    auto j = nlohmann::json::parse(buildResultToJson(result));
    CHECK(j["ok"].get<bool>() == true);
    CHECK(j["errorMsg"].get<std::string>().empty());
    CHECK(j["volume"].get<double>() == Approx(24.0).margin(1e-6));
    CHECK(j["triCount"].get<unsigned>() == result.triCount);
    CHECK(j["diagnostics"].is_array());
    CHECK(j["diagnostics"].empty());
}

TEST_CASE("buildResultToJson surfaces diagnostics for a failed build", "[headless]") {
    chisel::csg::MeshCache cache;
    BuildResult result = runBuild(fixture("headless/broken.scad"), {}, {}, cache);

    auto j = nlohmann::json::parse(buildResultToJson(result));
    CHECK(j["ok"].get<bool>() == false);
    CHECK_FALSE(j["diagnostics"].empty());
    const auto& firstDiag = j["diagnostics"][0];
    CHECK(firstDiag["level"].get<std::string>() == "error");
    CHECK(firstDiag.contains("message"));
    CHECK(firstDiag.contains("line"));
}
