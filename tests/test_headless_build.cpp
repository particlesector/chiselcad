#include "app/BuildStats.h"
#include "app/HeadlessBuild.h"
#include "csg/MeshCache.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
