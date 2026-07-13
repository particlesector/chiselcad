#include "MeshBuilder.h"
#include "csg/CsgEvaluator.h"
#include "csg/MeshCache.h"
#include "csg/MeshEvaluator.h"
#include "lang/SourceLoader.h"
#include <manifold/manifold.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <chrono>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Helper: Manifold mesh → flat-shaded vertex/index buffers
//
// `tint` is baked into every vertex so a single draw call can render roots
// with different color() tints (color() is a whole-root attribute — see
// nodeColor() in CsgNode.h — since Manifold's boolean ops merge geometry
// from a subtree's children into one mesh with no per-part identity left).
// ---------------------------------------------------------------------------
static void manifoldToMesh(const manifold::Manifold& m,
                            const glm::vec3& tint,
                            std::vector<render::Vertex>& verts,
                            std::vector<uint32_t>& indices)
{
    verts.clear();
    indices.clear();

    auto mesh = m.GetMeshGL();
    size_t triCount = mesh.triVerts.size() / 3;
    verts.reserve(triCount * 3);
    indices.reserve(triCount * 3);

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t i0 = mesh.triVerts[t * 3 + 0];
        uint32_t i1 = mesh.triVerts[t * 3 + 1];
        uint32_t i2 = mesh.triVerts[t * 3 + 2];

        auto vp = [&](uint32_t i) {
            return glm::vec3(
                mesh.vertProperties[i * mesh.numProp + 0],
                mesh.vertProperties[i * mesh.numProp + 1],
                mesh.vertProperties[i * mesh.numProp + 2]);
        };

        glm::vec3 p0 = vp(i0), p1 = vp(i1), p2 = vp(i2);
        glm::vec3 n  = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({p0, n, tint});
        verts.push_back({p1, n, tint});
        verts.push_back({p2, n, tint});
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }
}

// ---------------------------------------------------------------------------
// MeshBuilder
// ---------------------------------------------------------------------------
MeshBuilder::MeshBuilder()
    : m_thread([this] { workerLoop(); })
{}

MeshBuilder::~MeshBuilder() {
    {
        std::lock_guard<std::mutex> lk(m_workMutex);
        m_stop = true;
    }
    m_workCv.notify_one();
    m_thread.join();
}

void MeshBuilder::requestBuild(std::filesystem::path path) {
    int gen = ++m_currentGen;
    {
        std::lock_guard<std::mutex> lk(m_workMutex);
        m_hasWork  = true;
        m_workPath = std::move(path);
        m_workGen  = gen;
    }
    m_workCv.notify_one();
}

std::unique_ptr<BuildResult> MeshBuilder::poll() {
    std::lock_guard<std::mutex> lk(m_resultMutex);
    if (!m_pendingResult) return nullptr;
    if (m_pendingGen != m_currentGen.load()) {
        m_pendingResult = nullptr;  // superseded — discard silently
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
        {
            std::unique_lock<std::mutex> lk(m_workMutex);
            m_workCv.wait(lk, [this] { return m_stop || m_hasWork; });
            if (m_stop) return;
            m_hasWork = false;
            path = m_workPath;
            gen  = m_workGen;
        }
        buildOne(std::move(path), gen);
    }
}

void MeshBuilder::buildOne(std::filesystem::path path, int gen) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsedMs = [&] {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    };

    auto storeError = [&](std::unique_ptr<BuildResult> r) {
        m_phase = BuildPhase::Error;
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_pendingResult = std::move(r);
        m_pendingGen    = gen;
    };

    // ---- Phase: Parsing ----
    m_phase = BuildPhase::Parsing;
    spdlog::info("[mesh] Parsing {}", path.string());

    auto result = std::make_unique<BuildResult>();

    // Reads the root file, lexes/parses it, and recursively resolves any
    // include<>/use<> directives (in it or its includes) into one merged
    // ParseResult — see SourceLoader.h. Diagnostics from every file visited
    // already carry their own filePath, so DiagnosticsPanel's jump-to-file
    // works across files without further changes here.
    lang::LoadedSource loaded = lang::loadSource(path);
    auto& ast = loaded.result;

    bool hasError = false;
    for (const auto& d : loaded.diagnostics)
        if (d.level == lang::DiagLevel::Error) { hasError = true; break; }

    if (hasError) {
        result->diags    = loaded.diagnostics;
        result->errorMsg = "Parse errors";
        storeError(std::move(result));
        return;
    }

    // Bail early if a newer request arrived
    if (gen != m_currentGen.load()) return;

    // ---- Phase: Evaluating CSG ----
    m_phase = BuildPhase::Evaluating;
    csg::CsgEvaluator csgEval;
    csgEval.baseDir = path.parent_path(); // for import()'s relative paths
    auto scene = csgEval.evaluate(ast);

    // Forward echo() output as Info diagnostics
    for (const auto& msg : scene.echoMessages) {
        lang::Diagnostic d;
        d.level   = lang::DiagLevel::Info;
        d.message = msg;
        result->diags.push_back(std::move(d));
    }
    // Forward assert() failures as Error diagnostics
    for (auto& d : scene.evalDiags)
        result->diags.push_back(std::move(d));

    if (gen != m_currentGen.load()) return;

    // ---- Phase: Meshing (Manifold — the slow part) ----
    m_phase = BuildPhase::Meshing;
    // m_meshCache persists across buildOne() calls (see MeshBuilder.h) so
    // that unchanged subtrees reuse their previously-tessellated mesh across
    // file saves instead of every leaf being recomputed on every edit.
    csg::MeshEvaluator meshEval(m_meshCache);
    meshEval.useManifoldSphere = m_useManifoldSphere.load();
    std::vector<manifold::Manifold> rootMeshes;
    try {
        rootMeshes = meshEval.evaluate(scene);
    } catch (const std::exception& e) {
        result->errorMsg = std::string("Mesh error: ") + e.what();
        storeError(std::move(result));
        return;
    }
    for (auto& d : meshEval.diagnostics())
        result->diags.push_back(d);

    if (gen != m_currentGen.load()) return;

    // ---- Phase: Converting to vertex buffers ----
    m_phase = BuildPhase::Converting;

    // Each root is converted independently and appended — this keeps objects
    // that are spatially inside other objects visible (no boolean union across roots).
    std::vector<uint32_t> rootVertStart; // per-root start index into result->verts
    rootVertStart.reserve(rootMeshes.size());

    for (std::size_t ri = 0; ri < rootMeshes.size(); ++ri) {
        const auto& m = rootMeshes[ri];
        rootVertStart.push_back(static_cast<uint32_t>(result->verts.size()));

        result->volume      += m.Volume();
        result->surfaceArea += m.SurfaceArea();

        auto rawMesh = m.GetMeshGL();
        result->triCount  += static_cast<uint32_t>(rawMesh.triVerts.size() / 3);
        result->vertCount += static_cast<uint32_t>(
            rawMesh.numProp > 0 ? rawMesh.vertProperties.size() / rawMesh.numProp : 0);

        const csg::ColorAttr& rootColor = csg::nodeColor(*scene.roots[ri]);
        const glm::vec3 tint = rootColor.has ? glm::vec3(rootColor.value) : render::kDefaultVertexColor;

        std::vector<render::Vertex>  verts;
        std::vector<uint32_t>        indices;
        manifoldToMesh(m, tint, verts, indices);

        // Offset indices by the current vertex count before appending
        const auto base = static_cast<uint32_t>(result->verts.size());
        for (auto& idx : indices) idx += base;

        result->verts.insert(result->verts.end(), verts.begin(), verts.end());
        result->indices.insert(result->indices.end(), indices.begin(), indices.end());
    }

    // ---- Optional: pairwise overlap detection ----
    if (m_warnOverlappingRoots.load() && rootMeshes.size() > 1) {
        // Compute per-root AABB from the already-converted vertex data
        const auto totalVerts = static_cast<uint32_t>(result->verts.size());
        auto rootAABB = [&](std::size_t ri) -> std::pair<glm::vec3, glm::vec3> {
            uint32_t start = rootVertStart[ri];
            uint32_t end   = (ri + 1 < rootVertStart.size())
                             ? rootVertStart[ri + 1] : totalVerts;
            glm::vec3 bmin{ 1e30f,  1e30f,  1e30f};
            glm::vec3 bmax{-1e30f, -1e30f, -1e30f};
            for (uint32_t vi = start; vi < end; ++vi) {
                bmin = glm::min(bmin, result->verts[vi].pos);
                bmax = glm::max(bmax, result->verts[vi].pos);
            }
            return {bmin, bmax};
        };

        auto aabbOverlap = [](glm::vec3 mn1, glm::vec3 mx1,
                               glm::vec3 mn2, glm::vec3 mx2) {
            return (mn1.x <= mx2.x && mx1.x >= mn2.x) &&
                   (mn1.y <= mx2.y && mx1.y >= mn2.y) &&
                   (mn1.z <= mx2.z && mx1.z >= mn2.z);
        };

        for (std::size_t i = 0; i < rootMeshes.size(); ++i) {
            if (gen != m_currentGen.load()) return; // newer build queued — abort
            auto [mn1, mx1] = rootAABB(i);
            for (std::size_t j = i + 1; j < rootMeshes.size(); ++j) {
                auto [mn2, mx2] = rootAABB(j);
                if (!aabbOverlap(mn1, mx1, mn2, mx2)) continue;

                // AABBs overlap — run exact Manifold intersection
                manifold::Manifold sect = rootMeshes[i] ^ rootMeshes[j];
                if (std::abs(sect.Volume()) > 1e-6) {
                    lang::Diagnostic warn;
                    warn.level   = lang::DiagLevel::Warning;
                    warn.message = "Objects " + std::to_string(i + 1) +
                                   " and " + std::to_string(j + 1) +
                                   " overlap — wrap in union() or difference() if intentional";
                    result->diags.push_back(std::move(warn));
                }
            }
        }
    }
    result->elapsedMs = elapsedMs();

    // A newer generation may have been requested while the above ran (no gen
    // check gates this final block) — skip publishing stats/phase for a
    // superseded generation so the on-screen overlay never shows generation
    // N's numbers after generation N+1 has already been queued. The mesh
    // geometry itself doesn't need a check here: poll() already discards
    // m_pendingResult when m_pendingGen != m_currentGen.
    if (gen == m_currentGen.load()) {
        m_elapsedMs        = result->elapsedMs;
        m_lastVolume       = result->volume;
        m_lastSurfaceArea  = result->surfaceArea;
        m_lastTriCount     = result->triCount;
        m_lastVertCount    = result->vertCount;
        m_phase            = BuildPhase::Done;
    }

    auto fmtN = [](uint32_t n) {
        std::string s = std::to_string(n);
        int i = static_cast<int>(s.size()) - 3;
        while (i > 0) { s.insert(static_cast<size_t>(i), ","); i -= 3; }
        return s;
    };
    spdlog::info("[mesh] Render: {:.3f}s  |  {} facets  |  {} vertices  |  volume: {:.4f}  |  area: {:.4f}",
        result->elapsedMs / 1000.0, fmtN(result->triCount), fmtN(result->vertCount),
        result->volume, result->surfaceArea);

    {
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_pendingResult = std::move(result);
        m_pendingGen    = gen;
    }
}

} // namespace chisel::app
