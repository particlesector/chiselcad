#include "MeshBuilder.h"
#include "csg/CsgEvaluator.h"
#include "csg/MeshCache.h"
#include "csg/MeshEvaluator.h"
#include "lang/Lexer.h"
#include "lang/Parser.h"
#include <manifold/manifold.h>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <chrono>
#include <fstream>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Helper: Manifold mesh → flat-shaded vertex/index buffers
// ---------------------------------------------------------------------------
static void manifoldToMesh(const manifold::Manifold& m,
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
        verts.push_back({p0, n});
        verts.push_back({p1, n});
        verts.push_back({p2, n});
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

    std::ifstream f(path);
    if (!f.is_open()) {
        result->errorMsg = "Cannot open " + path.string();
        storeError(std::move(result));
        return;
    }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    lang::Lexer lexer(src);
    auto tokens = lexer.tokenize();
    if (lexer.hasErrors()) {
        result->diags    = lexer.diagnostics();
        result->errorMsg = "Lex errors";
        storeError(std::move(result));
        return;
    }

    lang::Parser parser(std::move(tokens));
    auto ast = parser.parse();
    if (parser.hasErrors()) {
        result->diags    = parser.diagnostics();
        result->errorMsg = "Parse errors";
        storeError(std::move(result));
        return;
    }

    // Bail early if a newer request arrived
    if (gen != m_currentGen.load()) return;

    // ---- Phase: Evaluating CSG ----
    m_phase = BuildPhase::Evaluating;
    csg::CsgEvaluator csgEval;
    auto scene = csgEval.evaluate(ast);

    if (gen != m_currentGen.load()) return;

    // ---- Phase: Meshing (Manifold — the slow part) ----
    m_phase = BuildPhase::Meshing;
    csg::MeshCache     cache;
    csg::MeshEvaluator meshEval(cache);
    manifold::Manifold manifoldMesh;
    try {
        manifoldMesh = meshEval.evaluate(scene);
    } catch (const std::exception& e) {
        result->errorMsg = std::string("Mesh error: ") + e.what();
        storeError(std::move(result));
        return;
    }

    if (gen != m_currentGen.load()) return;

    // ---- Phase: Converting to vertex buffers ----
    m_phase = BuildPhase::Converting;
    manifoldToMesh(manifoldMesh, result->verts, result->indices);
    result->triCount  = static_cast<uint32_t>(result->indices.size() / 3);
    result->elapsedMs = elapsedMs();

    m_elapsedMs    = result->elapsedMs;
    m_lastTriCount = result->triCount;

    spdlog::info("[mesh] Done: {} tris in {:.0f} ms", result->triCount, result->elapsedMs);
    m_phase = BuildPhase::Done;

    {
        std::lock_guard<std::mutex> lk(m_resultMutex);
        m_pendingResult = std::move(result);
        m_pendingGen    = gen;
    }
}

} // namespace chisel::app
