// ChiselCAD — unit tests
// Tests for the language subsystem (lexer, parser, AST) and CSG evaluator
// will be added here as those subsystems are implemented.

#include <catch2/catch_test_macros.hpp>

TEST_CASE("sanity", "[meta]") {
    REQUIRE(1 + 1 == 2);
}
