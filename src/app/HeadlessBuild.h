#pragma once
#include "csg/MeshCache.h"
#include "lang/Diagnostic.h"
#include "render/Vertex.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace chisel::app {

// Snapshot of render-camera state to plumb into $vpr/$vpt/$vpd for a build.
// Defaults match OpenSCAD's own defaults (no camera() statement) so a caller
// with no live camera (tests, CLI use) can pass ViewportState{} and still
// get sane values instead of undef — see Interpreter::setViewport. Kept at
// namespace scope rather than nested in a class: a nested aggregate used as
// a default argument of its own enclosing class's member function hits a
// "default member initializer required before the end of its enclosing
// class" error under GCC, since default-member-initializer resolution and
// default-function-argument resolution both defer to end-of-class.
struct ViewportState {
    double vpr[3] = {55.0, 0.0, 25.0}; // [x,y,z] rotation degrees
    double vpt[3] = {0.0, 0.0, 0.0};   // [x,y,z] translation (orbit target)
    double vpd    = 140.0;             // viewport distance
    double vpf    = 22.5;              // viewport field-of-view degrees
};

enum class BuildPhase : int {
    Idle,
    Parsing,
    Evaluating,   // CsgEvaluator
    Meshing,      // Manifold boolean ops (usually the slow part)
    Converting,   // manifold mesh → vertex/index buffers
    Done,
    Error
};

// Toggles that affect meshing but aren't part of the .scad source itself —
// pulled out of MeshBuilder's atomics so runBuild() below doesn't need a
// MeshBuilder instance to call.
struct BuildOptions {
    bool useManifoldSphere    = false; // Manifold's built-in sphere vs. OpenSCAD-compatible UV sphere
    bool warnOverlappingRoots = false; // pairwise AABB+boolean overlap check across roots
};

struct BuildResult {
    std::vector<render::Vertex>  verts;
    std::vector<uint32_t>        indices;
    lang::DiagList                 diags;
    std::string                  errorMsg;    // empty = success
    uint32_t                     triCount   = 0;
    uint32_t                     vertCount  = 0;  // unique verts from Manifold mesh
    double                       volume     = 0.0;
    double                       surfaceArea = 0.0;
    double                       elapsedMs  = 0.0;
    // Prefix length of `indices` (and, transitively, the verts it refers to)
    // that belongs to "real" model geometry, as opposed to '%'-tagged
    // background/reference roots appended after it — see CsgScene::
    // backgroundRoots. STL export uses only this prefix; the rest is
    // rendered on screen but was never part of the model. Equal to
    // indices.size() when the scene has no background roots.
    uint32_t                     realIndexCount = 0;

    // True iff errorMsg is empty and no diagnostic is DiagLevel::Error —
    // the single check both the CLI's exit code and a future compat-suite
    // "did this build succeed" assertion should use.
    bool ok() const {
        if (!errorMsg.empty())
            return false;
        for (const auto& d : diags)
            if (d.level == lang::DiagLevel::Error)
                return false;
        return true;
    }
};

// Called at each phase transition; MeshBuilder uses this to publish
// progress for the UI. Optional — pass {} to ignore.
using ProgressFn = std::function<void(BuildPhase)>;

// Polled at phase boundaries; returning true abandons the build early
// (e.g. a newer file save superseded this one) without finishing meshing
// it'll just be discarded. Optional — pass {} for a build that always runs
// to completion, which is what headless/CLI/test callers want.
using AbortFn = std::function<bool()>;

// ---------------------------------------------------------------------------
// runBuild — the parse → CSG-evaluate → mesh → vertex-buffer pipeline, run
// synchronously to completion (or early-abort) on the calling thread.
//
// This is the one place that pipeline lives; MeshBuilder wraps it on a
// worker thread for the GUI's async/poll() usage, and the headless CLI
// (cli_main.cpp) calls it directly and blocks. `cache` is caller-owned so
// MeshBuilder can persist one across rebuilds (see MeshBuilder::m_meshCache)
// while a one-shot caller can just pass a fresh, local MeshCache.
// ---------------------------------------------------------------------------
BuildResult runBuild(const std::filesystem::path& path,
                      const ViewportState& viewport,
                      const BuildOptions& options,
                      csg::MeshCache& cache,
                      const ProgressFn& onProgress = {},
                      const AbortFn& shouldAbort = {});

} // namespace chisel::app
