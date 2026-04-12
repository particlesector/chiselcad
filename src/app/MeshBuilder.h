#pragma once
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
    // discarded when poll() is next called.
    void requestBuild(std::filesystem::path path);

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
    void buildOne(std::filesystem::path path, int gen);

    std::thread             m_thread;
    std::mutex              m_workMutex;
    std::condition_variable m_workCv;

    // Protected by m_workMutex:
    bool                    m_stop    = false;
    bool                    m_hasWork = false;
    std::filesystem::path   m_workPath;
    int                     m_workGen = 0;

    // Incremented by requestBuild(); read by poll() to detect stale results.
    std::atomic<int>        m_currentGen{0};

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
