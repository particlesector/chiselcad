#include "MeshBuilder.h"

namespace chisel::app {

MeshBuilder::MeshBuilder() : m_thread([this] { workerLoop(); }) {}

MeshBuilder::~MeshBuilder() {
    {
        std::lock_guard<std::mutex> lk(m_workMutex);
        m_stop = true;
    }
    m_workCv.notify_one();
    m_thread.join();
}

void MeshBuilder::requestBuild(std::filesystem::path path, ViewportState viewport) {
    int gen = ++m_currentGen;
    {
        std::lock_guard<std::mutex> lk(m_workMutex);
        m_hasWork = true;
        m_workPath = std::move(path);
        m_workGen = gen;
        m_workViewport = viewport;
    }
    m_workCv.notify_one();
}

std::unique_ptr<BuildResult> MeshBuilder::poll() {
    std::lock_guard<std::mutex> lk(m_resultMutex);
    if (!m_pendingResult)
        return nullptr;
    if (m_pendingGen != m_currentGen.load()) {
        m_pendingResult = nullptr; // superseded — discard silently
        return nullptr;
    }
    return std::move(m_pendingResult);
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void MeshBuilder::workerLoop() {
    while (true) {
        std::filesystem::path path;
        int gen = 0;
        ViewportState viewport;
        {
            std::unique_lock<std::mutex> lk(m_workMutex);
            m_workCv.wait(lk, [this] { return m_stop || m_hasWork; });
            if (m_stop)
                return;
            m_hasWork = false;
            path = m_workPath;
            gen = m_workGen;
            viewport = m_workViewport;
        }
        buildOne(std::move(path), gen, viewport);
    }
}

// Thin wrapper around runBuild() (HeadlessBuild.h/.cpp): publishes phase
// transitions and the final result through the atomics/poll() the GUI
// reads, and cancels early when a newer build has been queued in the
// meantime. The pipeline itself — parse/evaluate/mesh/convert — lives in
// runBuild() so the headless CLI (cli_main.cpp) can call the exact same
// logic synchronously without any of this threading.
void MeshBuilder::buildOne(std::filesystem::path path, int gen, ViewportState viewport) {
    BuildOptions options;
    options.useManifoldSphere = m_useManifoldSphere.load();
    options.warnOverlappingRoots = m_warnOverlappingRoots.load();

    auto onProgress = [&](BuildPhase p) { m_phase = p; };
    auto shouldAbort = [&] { return gen != m_currentGen.load(); };

    BuildResult result = runBuild(path, viewport, options, m_meshCache, onProgress, shouldAbort);

    if (gen == m_currentGen.load()) {
        m_elapsedMs = result.elapsedMs;
        m_lastVolume = result.volume;
        m_lastSurfaceArea = result.surfaceArea;
        m_lastTriCount = result.triCount;
        m_lastVertCount = result.vertCount;
        // runBuild() already published Done/Error via onProgress; nothing
        // further to set here.
    }

    {
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_pendingResult = std::make_unique<BuildResult>(std::move(result));
        m_pendingGen = gen;
    }
}

} // namespace chisel::app
