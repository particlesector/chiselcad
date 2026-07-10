#include <catch2/catch_test_macros.hpp>
#include "lang/SourceLoader.h"
#include "lang/AST.h"
#include "lang/Interpreter.h"
#include <filesystem>

using namespace chisel::lang;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#  error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

static bool hasError(const DiagList& diags) {
    for (const auto& d : diags)
        if (d.level == DiagLevel::Error) return true;
    return false;
}

// ---------------------------------------------------------------------------
// include<> — splices roots/assignments/moduleDefs/functionDefs
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:include splices roots, assignments, and moduleDefs", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("include_basic/main.scad"));
    REQUIRE_FALSE(hasError(loaded.diagnostics));

    REQUIRE(loaded.result.assignments.size() == 1);
    REQUIRE(loaded.result.assignments[0].name == "size");

    REQUIRE(loaded.result.moduleDefs.size() == 1);
    REQUIRE(loaded.result.moduleDefs[0].name == "box");

    // box(); and translate(...) cube(1); from main.scad itself
    REQUIRE(loaded.result.roots.size() == 2);

    // No unresolved include directives should remain after loading.
    REQUIRE(loaded.result.includes.empty());
}

// ---------------------------------------------------------------------------
// use<> — only moduleDefs/functionDefs cross the boundary
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:use imports only moduleDefs/functionDefs, not roots or assignments", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("use_basic/main.scad"));
    REQUIRE_FALSE(hasError(loaded.diagnostics));

    REQUIRE(loaded.result.moduleDefs.size() == 1);
    REQUIRE(loaded.result.moduleDefs[0].name == "thing");

    // lib.scad's own `leftover_size = 1;` and `cube(leftover_size);` must
    // NOT leak into the using file.
    REQUIRE(loaded.result.assignments.empty());
    REQUIRE(loaded.result.roots.size() == 1); // just main.scad's `thing();`
}

// ---------------------------------------------------------------------------
// include<> splices at the directive's textual position, not at the end —
// a reassignment after the include must still see the included value.
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:include splices assignments at the directive's position, not the end", "[source-loader][tier-e]") {
    // main.scad: x = 1; include <order_lib.scad>  (order_lib.scad: x = 2;)
    //            y = x;
    // Textual paste would read as: x=1; x=2; y=x; -> x ends at 2, y sees 2.
    // Appending the include at the end of the vector instead would give
    // x=1; y=x; x=2; -> y would see x's *old* value (1), which is wrong.
    auto loaded = loadSource(fixture("order/main.scad"));
    REQUIRE_FALSE(hasError(loaded.diagnostics));
    REQUIRE(loaded.result.assignments.size() == 3);

    Interpreter interp;
    interp.loadAssignments(loaded.result);
    REQUIRE(interp.getVar("x").asNumber() == 2.0);
    REQUIRE(interp.getVar("y").asNumber() == 2.0);
}

// ---------------------------------------------------------------------------
// Circular include — must terminate and report an error, not hang/crash
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:circular include is reported and does not hang", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("cycle/a.scad"));
    REQUIRE(hasError(loaded.diagnostics));

    bool sawCircular = false;
    for (const auto& d : loaded.diagnostics)
        if (d.message.find("circular include") != std::string::npos) sawCircular = true;
    REQUIRE(sawCircular);
}

// ---------------------------------------------------------------------------
// Diamond include — same file reached via two different branches is not a
// cycle (no "currently being loaded" file is revisited).
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:diamond include is not treated as a cycle", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("diamond/main.scad"));
    REQUIRE_FALSE(hasError(loaded.diagnostics));

    // common.scad's module is merged once per include site (no dedup), so it
    // appears twice — that's expected, not a bug (see SourceLoader.h).
    int sharedCount = 0;
    for (const auto& m : loaded.result.moduleDefs)
        if (m.name == "shared") ++sharedCount;
    REQUIRE(sharedCount == 2);

    REQUIRE(loaded.result.roots.size() == 1); // main.scad's `shared();`
}

// ---------------------------------------------------------------------------
// Missing file — reported with the referencing file's path, not silently
// dropped.
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:missing included file is reported with a diagnostic", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("missing/main.scad"));
    REQUIRE(hasError(loaded.diagnostics));

    bool sawMissing = false;
    for (const auto& d : loaded.diagnostics) {
        if (d.message.find("cannot open included file") != std::string::npos) {
            sawMissing = true;
            REQUIRE(d.filePath == fixture("missing/main.scad").string());
        }
    }
    REQUIRE(sawMissing);
}

// ---------------------------------------------------------------------------
// Missing root file — reported, not thrown/crashed.
// ---------------------------------------------------------------------------
TEST_CASE("SourceLoader:missing root file is reported, not thrown", "[source-loader][tier-e]") {
    auto loaded = loadSource(fixture("does_not_exist/main.scad"));
    REQUIRE(hasError(loaded.diagnostics));
    REQUIRE(loaded.result.roots.empty());
}
