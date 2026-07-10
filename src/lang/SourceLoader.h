#pragma once
#include "AST.h"
#include "Diagnostic.h"
#include <filesystem>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// LoadedSource — the result of recursively resolving a root .scad file's
// include<>/use<> directives into a single merged ParseResult.
// ---------------------------------------------------------------------------
struct LoadedSource {
    ParseResult result;
    DiagList    diagnostics; // lex/parse errors from every file visited, plus
                              // "cannot open" / "circular include" errors
};

// Reads, lexes, and parses `rootPath`, then recursively resolves every
// include<>/use<> directive it (transitively) contains — each target path is
// resolved relative to the directory of the file that references it.
//
// `include` splices the target's roots/assignments/moduleDefs/functionDefs
// into the merged result (OpenSCAD's "as if pasted here" semantics); `use`
// splices only its moduleDefs/functionDefs. A file that (transitively)
// includes/uses itself while still being loaded is reported as a circular
// include, not infinite-looped; the same file reached twice via different,
// non-overlapping branches (a "diamond") is simply loaded twice, matching
// OpenSCAD (there is no include-guard/dedup behavior).
LoadedSource loadSource(const std::filesystem::path& rootPath);

} // namespace chisel::lang
