#pragma once
#include "AST.h"
#include "Diagnostic.h"

#include <filesystem>
#include <string>
#include <vector>

namespace chisel::lang {

// ---------------------------------------------------------------------------
// LoadedSource — the result of recursively resolving a root .scad file's
// include<>/use<> directives into a single merged ParseResult.
// ---------------------------------------------------------------------------
struct LoadedSource {
    ParseResult result;
    DiagList diagnostics; // lex/parse errors from every file visited, plus
                          // "cannot open" / "circular include" errors

    // fileId -> path table (SourceLoc::fileId indexes into this), in the
    // order each file was first opened. Index 0 is always rootPath, since
    // it's the first file loadSource() opens — matching SourceLoc's own
    // "0 is the sensible default" convention. Callers that produce
    // diagnostics from AST nodes after loadSource() returns (CsgEvaluator's
    // eval-time errors — assert()/import()/surface()/text()/polyhedron(),
    // which can't run until parsing succeeds) use this to resolve
    // SourceLoc::fileId back to a path, the same way lex/parse diagnostics
    // already carry filePath directly.
    std::vector<std::string> files;
};

// Reads, lexes, and parses `rootPath`, then recursively resolves every
// include<>/use<> directive it (transitively) contains — each target path is
// resolved relative to the directory of the file that references it.
//
// `include` splices the target's roots/assignments/moduleDefs/functionDefs
// into the merged result at the include directive's own textual position
// (OpenSCAD's "as if pasted here" semantics — e.g. a reassignment after the
// include still overrides a value the included file set); `use` splices only
// its moduleDefs/functionDefs. This positional fidelity is per-vector (roots,
// assignments, moduleDefs, functionDefs, and $fn/$fs/$fa each resolve
// independently) since the Parser already flattens a file's statements into
// those separate vectors before SourceLoader ever sees them — the fully
// interleaved statement order isn't reconstructed. A file that (transitively)
// includes/uses itself while still being loaded is reported as a circular
// include, not infinite-looped; the same file reached twice via different,
// non-overlapping branches (a "diamond") is simply loaded twice, matching
// OpenSCAD (there is no include-guard/dedup behavior).
LoadedSource loadSource(const std::filesystem::path& rootPath);

} // namespace chisel::lang
