#include "import/TextLoader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <limits>

using namespace chisel::io;
using Catch::Approx;

#ifndef CHISELCAD_TEST_FIXTURE_DIR
#error "CHISELCAD_TEST_FIXTURE_DIR must be defined by the build (see CMakeLists.txt)"
#endif
#ifndef CHISELCAD_RESOURCE_DIR
#error "CHISELCAD_RESOURCE_DIR must be defined by the build (see CMakeLists.txt)"
#endif

static std::filesystem::path fixture(const std::string& relPath) {
    return std::filesystem::path(CHISELCAD_TEST_FIXTURE_DIR) / relPath;
}

// The real bundled default font (resources/fonts/Roboto-Regular.ttf) — used
// directly rather than duplicated into tests/fixtures/, since it's already
// checked into the repo and this exercises the actual shipped asset.
static std::filesystem::path defaultFont() {
    return std::filesystem::path(CHISELCAD_RESOURCE_DIR) / "fonts" / "Roboto-Regular.ttf";
}

struct BBox {
    float minX = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
};

static BBox bounds(const RawTextOutline& o) {
    BBox b;
    for (const auto& p : o.points) {
        b.minX = std::min(b.minX, p.x);
        b.maxX = std::max(b.maxX, p.x);
        b.minY = std::min(b.minY, p.y);
        b.maxY = std::max(b.maxY, p.y);
    }
    return b;
}

TEST_CASE("TextLoader:empty string yields no geometry and no error", "[text-loader][tier-e]") {
    auto out = loadTextOutline(defaultFont(), "", 10.0, "left", "baseline", 1.0, 0.0);
    REQUIRE(out.error.empty());
    REQUIRE(out.points.empty());
    REQUIRE(out.paths.empty());
}

TEST_CASE("TextLoader:a single glyph produces at least one closed contour",
          "[text-loader][tier-e]") {
    auto out = loadTextOutline(defaultFont(), "A", 10.0, "left", "baseline", 1.0, 0.0);
    REQUIRE(out.error.empty());
    REQUIRE_FALSE(out.paths.empty());
    for (const auto& path : out.paths)
        REQUIRE(path.size() >= 3);
}

TEST_CASE("TextLoader:a glyph with a hole (O) produces multiple contours",
          "[text-loader][tier-e]") {
    // "O" has an outer contour and an inner counter — EvenOdd fill (see
    // PrimitiveGen.cpp) turns the nesting into a hole automatically.
    auto out = loadTextOutline(defaultFont(), "O", 10.0, "left", "baseline", 1.0, 0.0);
    REQUIRE(out.error.empty());
    REQUIRE(out.paths.size() >= 2);
}

TEST_CASE("TextLoader:missing font file reports an error, not a crash", "[text-loader][tier-e]") {
    auto out = loadTextOutline(fixture("text/does_not_exist.ttf"), "hi", 10.0, "left", "baseline",
                               1.0, 0.0);
    REQUIRE_FALSE(out.error.empty());
    REQUIRE(out.points.empty());
}

TEST_CASE("TextLoader:a non-font file reports an error, not a crash", "[text-loader][tier-e]") {
    auto out =
        loadTextOutline(fixture("text/not_a_font.ttf"), "hi", 10.0, "left", "baseline", 1.0, 0.0);
    REQUIRE_FALSE(out.error.empty());
    REQUIRE(out.points.empty());
}

TEST_CASE("TextLoader:size scales the outline proportionally", "[text-loader][tier-e]") {
    auto small = loadTextOutline(defaultFont(), "A", 10.0, "left", "baseline", 1.0, 0.0);
    auto big = loadTextOutline(defaultFont(), "A", 20.0, "left", "baseline", 1.0, 0.0);
    REQUIRE(small.error.empty());
    REQUIRE(big.error.empty());

    BBox bs = bounds(small), bb = bounds(big);
    REQUIRE((bb.maxY - bb.minY) == Approx((bs.maxY - bs.minY) * 2.0f).epsilon(0.01));
}

TEST_CASE("TextLoader:halign left/center/right shift by exact half/full advance width",
          "[text-loader][tier-e]") {
    auto left = loadTextOutline(defaultFont(), "AB", 10.0, "left", "baseline", 1.0, 0.0);
    auto center = loadTextOutline(defaultFont(), "AB", 10.0, "center", "baseline", 1.0, 0.0);
    auto right = loadTextOutline(defaultFont(), "AB", 10.0, "right", "baseline", 1.0, 0.0);
    REQUIRE(left.error.empty());
    REQUIRE(center.error.empty());
    REQUIRE(right.error.empty());

    BBox bl = bounds(left), bc = bounds(center), br = bounds(right);

    // halign shifts the whole run horizontally without changing its shape
    // (the offset is derived from the shaped run's total advance width,
    // not the glyphs' own tight ink bbox, so a fixed small margin against
    // ink-bbox width would be the wrong comparison — instead check shape
    // is preserved and that center's shift is exactly half of right's).
    REQUIRE((bc.maxX - bc.minX) == Approx(bl.maxX - bl.minX).margin(0.01f));
    REQUIRE((br.maxX - br.minX) == Approx(bl.maxX - bl.minX).margin(0.01f));
    float centerShift = bl.minX - bc.minX;
    float rightShift = bl.minX - br.minX;
    REQUIRE(centerShift > 0.0f);
    REQUIRE(centerShift == Approx(rightShift / 2.0f).margin(0.01f));
}

TEST_CASE("TextLoader:valign shifts vertically, ordered top < center < baseline < bottom",
          "[text-loader][tier-e]") {
    auto baseline = loadTextOutline(defaultFont(), "A", 10.0, "left", "baseline", 1.0, 0.0);
    auto top = loadTextOutline(defaultFont(), "A", 10.0, "left", "top", 1.0, 0.0);
    auto center = loadTextOutline(defaultFont(), "A", 10.0, "left", "center", 1.0, 0.0);
    auto bottom = loadTextOutline(defaultFont(), "A", 10.0, "left", "bottom", 1.0, 0.0);
    REQUIRE(baseline.error.empty());
    REQUIRE(top.error.empty());
    REQUIRE(center.error.empty());
    REQUIRE(bottom.error.empty());

    BBox bBase = bounds(baseline), bTop = bounds(top), bCenter = bounds(center),
         bBottom = bounds(bottom);
    float height = bBase.maxY - bBase.minY;

    // Each valign only translates the glyph (shape/height unchanged). The
    // font's ascent/descent (used for top/center/bottom) don't coincide
    // with "A"'s own ink extent, so this checks ordering/shape-preservation
    // rather than exact alignment to y=0.
    REQUIRE((bTop.maxY - bTop.minY) == Approx(height).margin(0.01f));
    REQUIRE((bCenter.maxY - bCenter.minY) == Approx(height).margin(0.01f));
    REQUIRE((bBottom.maxY - bBottom.minY) == Approx(height).margin(0.01f));

    REQUIRE(bTop.maxY < bCenter.maxY);
    REQUIRE(bCenter.maxY < bBase.maxY);
    REQUIRE(bBase.maxY < bBottom.maxY);
}

TEST_CASE("TextLoader:spacing widens the advance between glyphs", "[text-loader][tier-e]") {
    auto normal = loadTextOutline(defaultFont(), "AB", 10.0, "left", "baseline", 1.0, 0.0);
    auto wide = loadTextOutline(defaultFont(), "AB", 10.0, "left", "baseline", 2.0, 0.0);
    REQUIRE(normal.error.empty());
    REQUIRE(wide.error.empty());

    BBox bn = bounds(normal), bw = bounds(wide);
    REQUIRE((bw.maxX - bw.minX) > (bn.maxX - bn.minX));
}

TEST_CASE("TextLoader:$fn overrides curve tessellation to a fixed segment count",
          "[text-loader][tier-e]") {
    // "O" is all curves; a small fixed $fn should produce visibly fewer
    // points than the adaptive default for the same glyph.
    auto adaptive = loadTextOutline(defaultFont(), "O", 40.0, "left", "baseline", 1.0, 0.0);
    auto coarse = loadTextOutline(defaultFont(), "O", 40.0, "left", "baseline", 1.0, 3.0);
    REQUIRE(adaptive.error.empty());
    REQUIRE(coarse.error.empty());
    REQUIRE(coarse.points.size() < adaptive.points.size());
}
