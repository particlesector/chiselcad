#pragma once
#include "HeadlessBuild.h"
#include <string>

namespace chisel::app {

// Serializes a BuildResult to the JSON shape used by `chiselcad_cli --stats`
// and (eventually) by Catch2-based compat-suite tests comparing against a
// recorded golden.json — see docs/roadmap.md's compat-suite discussion.
// Deliberately lives in chiselcad_core rather than the CLI itself so both
// callers can produce/consume this shape in-process without shelling out.
//
// Shape:
//   {
//     "ok": bool,               // BuildResult::ok()
//     "errorMsg": string,
//     "volume": number,
//     "surfaceArea": number,
//     "triCount": number,
//     "vertCount": number,
//     "elapsedMs": number,
//     "diagnostics": [
//       { "level": "info"|"warning"|"error", "message": string,
//         "file": string, "line": number, "col": number }
//     ]
//   }
std::string buildResultToJson(const BuildResult& result, int indent = 2);

} // namespace chisel::app
