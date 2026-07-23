// chiselcad_cli — headless, GPU-free entry point for the .scad build
// pipeline (parse -> CSG evaluate -> Manifold mesh). No Vulkan/GLFW/ImGui
// dependency, so it builds and runs on CI/container hosts with no display
// or GPU driver — see docs/roadmap.md for why this exists (STL export
// scripting today; the OpenSCAD compat regression suite's mesh-invariant
// checks tomorrow).
//
// Usage:
//   chiselcad_cli <file.scad> [--out mesh.stl] [--stats out.json|-]
//
// With neither --out nor --stats given, stats are printed to stdout (a
// quick "does this file build" check). Exit code is 0 iff the build
// succeeded with no Error-level diagnostics (BuildResult::ok()), 1
// otherwise — so this composes directly into shell/CI conditionals.
#include "app/BuildStats.h"
#include "app/HeadlessBuild.h"
#include "csg/MeshCache.h"
#include "export/StlExporter.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

namespace {

void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <file.scad> [--out mesh.stl] [--stats out.json|-]\n";
}

} // namespace

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::warn); // keep stdout clean for --stats -

    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    std::filesystem::path scadPath = argv[1];
    std::filesystem::path outStlPath;
    std::string statsPath;
    bool wantStats = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            outStlPath = argv[++i];
        } else if (arg == "--stats" && i + 1 < argc) {
            statsPath = argv[++i];
            wantStats = true;
        } else {
            std::cerr << "Unrecognized argument: " << arg << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    // Default: nobody asked for an artifact, so at least report stats.
    if (outStlPath.empty() && !wantStats) {
        wantStats = true;
        statsPath = "-";
    }

    chisel::csg::MeshCache cache; // one-shot process — no reuse across builds needed
    chisel::app::BuildResult result =
        chisel::app::runBuild(scadPath, /*viewport=*/{}, /*options=*/{}, cache);

    // Tracked instead of returning immediately on failure so a --out error
    // (e.g. an unwritable path) doesn't skip an explicitly-requested
    // --stats — the build's diagnostics/stats are already fully known at
    // that point regardless of whether the STL write itself succeeded.
    bool outputFailed = false;

    if (!outStlPath.empty()) {
        if (!result.ok()) {
            std::cerr << "Not exporting STL: build did not succeed ("
                      << (result.errorMsg.empty() ? "see diagnostics" : result.errorMsg) << ")\n";
        } else {
            // Exclude any '%'-tagged background/reference geometry appended
            // after the real model's indices — see BuildResult::realIndexCount
            // and Application::exportStl(), which this mirrors.
            std::vector<uint32_t> exportIndices(
                result.indices.begin(),
                result.indices.begin() +
                    std::min<std::size_t>(result.realIndexCount, result.indices.size()));

            auto err = chisel::io::exportBinaryStl(outStlPath, result.verts, exportIndices);
            if (!err.empty()) {
                std::cerr << "STL export failed: " << err << "\n";
                outputFailed = true;
            }
        }
    }

    if (wantStats) {
        std::string json = chisel::app::buildResultToJson(result);
        if (statsPath == "-") {
            std::cout << json << "\n";
        } else {
            std::ofstream f(statsPath, std::ios::binary);
            if (!f) {
                std::cerr << "Could not open " << statsPath << " for writing\n";
                outputFailed = true;
            } else {
                f << json << "\n";
            }
        }
    }

    return (result.ok() && !outputFailed) ? 0 : 1;
}
