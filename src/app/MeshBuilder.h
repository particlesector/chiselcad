#pragma once
#include "csg/MeshCache.h"
#include "lang/Diagnostic.h"
#include "render/GpuMesh.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chisel::app {

// Snapshot of render-camera state to plumb into $vpr/$vpt/$vpd for a build.
// Defaults match OpenSCAD's own defaults (no camera() statement) so a caller
// with no live camera (tests, CLI use) can pass ViewportState{} and still
// get sane values instead of undef — see Interpreter::setViewport. Kept at
// namespace scope rather than nested in MeshBuilder: a nested aggregate used
// as a default argument of its own enclosing class's member function hits a
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

struct BuildResult {
    std::vector<render::Vertex>  verts;
    std::vector<uint32_t>        indices;
    lang::DiagList               diags;
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
};

// ---------------------------------------------------------------------------
// MeshBuilder — runs the parse→CSG→mesh pipeline on a background thread.
//
// Usage (main thread):
//   builder.requestBuild(path);          // called on file change
//   if (auto r = builder.poll()) { ... } // called each frame; r != null when done
//
// Thread safety: requestBuild() and poll() are safe to call from the main
// thread at any time.  The worker thread writes only to internal state
// protected by mutexes.
// ---------------------------------------------------------------------------
class MeshBuilder {
public:
    MeshBuilder();
    ~MeshBuilder();  // stops and joins the worker thread

    // Queue a new build.  If a build is already running, its result will be
    // discarded when poll() is next called. `viewport` is snapshotted at
    // request time and used to set $vpr/$vpt/$vpd for this build only —
    // callers that don't care (or have no camera) can omit it.
    void requestBuild(std::filesystem::path path, ViewportState viewport = {});

    void setUseManifoldSphere(bool v)      noexcept { m_useManifoldSphere.store(v); }
    void setWarnOverlappingRoots(bool v)  noexcept { m_warnOverlappingRoots.store(v); }

    // Call once per frame from the main (Vulkan) thread.
    // Returns a finished BuildResult when one is ready, nullptr otherwise.
    // Stale results (superseded by a newer requestBuild) are automatically
    // discarded and nullptr is returned.
    std::unique_ptr<BuildResult> poll();

    BuildPhase phase()           const noexcept { return m_phase.load(); }
    double     elapsedMs()       const noexcept { return m_elapsedMs.load(); }
    double     lastVolume()      const noexcept { return m_lastVolume.load(); }
    double     lastSurfaceArea() const noexcept { return m_lastSurfaceArea.load(); }
    uint32_t   lastTriCount()    const noexcept { return m_lastTriCount.load(); }
    uint32_t   lastVertCount()   const noexcept { return m_lastVertCount.load(); }

private:
    void workerLoop();
    void buildOne(std::filesystem::path path, int gen, ViewportState viewport);

    // Owned by and only ever touched from the worker thread (workerLoop()/
    // buildOne() below) — never accessed from the main thread, so it needs
    // no locking of its own despite outliving any single build. Persisting
    // it here (rather than a local inside buildOne(), as before) is the
    // whole point: it lets unchanged subtrees across successive file saves
    // reuse their previously-tessellated mesh instead of every leaf being
    // recomputed on every edit.
    csg::MeshCache           m_meshCache;

    std::thread             m_thread;
    std::mutex              m_workMutex;
    std::condition_variable m_workCv;

    // Protected by m_workMutex:
    bool                    m_stop    = false;
    bool                    m_hasWork = false;
    std::filesystem::path   m_workPath;
    int                     m_workGen = 0;
    ViewportState           m_workViewport;

    // Incremented by requestBuild(); read by poll() to detect stale results.
    std::atomic<int>        m_currentGen{0};
    std::atomic<bool>       m_useManifoldSphere{false};
    std::atomic<bool>       m_warnOverlappingRoots{false};

    // Readable from main thread for UI without locks.
    std::atomic<BuildPhase> m_phase{BuildPhase::Idle};
    std::atomic<double>     m_elapsedMs{0.0};
    std::atomic<double>     m_lastVolume{0.0};
    std::atomic<double>     m_lastSurfaceArea{0.0};
    std::atomic<uint32_t>   m_lastTriCount{0};
    std::atomic<uint32_t>   m_lastVertCount{0};

    // Finished result — written by worker, consumed by poll().
    std::mutex                   m_resultMutex;
    std::unique_ptr<BuildResult> m_pendingResult;
    int                          m_pendingGen = -1;
};

} // namespace chisel::app
