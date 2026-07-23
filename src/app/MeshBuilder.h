#pragma once
#include "HeadlessBuild.h"
#include "csg/MeshCache.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace chisel::app {

// ---------------------------------------------------------------------------
// MeshBuilder — runs runBuild() (HeadlessBuild.h) on a background thread.
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
