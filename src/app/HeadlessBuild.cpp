#include "HeadlessBuild.h"

#include "csg/CsgEvaluator.h"
#include "csg/MeshEvaluator.h"
#include "lang/Interpreter.h"
#include "lang/SourceLoader.h"

#include <chrono>
#include <cmath>
#include <glm/glm.hpp>
#include <manifold/manifold.h>
#include <spdlog/spdlog.h>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Helper: Manifold mesh → flat-shaded vertex/index buffers
//
// `tint` is baked into every vertex so a single draw call can render roots
// with different color() tints (color() is a whole-root attribute — see
// nodeColor() in CsgNode.h — since Manifold's boolean ops merge geometry
// from a subtree's children into one mesh with no per-part identity left).
// ---------------------------------------------------------------------------
static void manifoldToMesh(const manifold::Manifold& m, const glm::vec3& tint,
                           std::vector<render::Vertex>& verts, std::vector<uint32_t>& indices) {
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
            return glm::vec3(mesh.vertProperties[i * mesh.numProp + 0],
                             mesh.vertProperties[i * mesh.numProp + 1],
                             mesh.vertProperties[i * mesh.numProp + 2]);
        };

        glm::vec3 p0 = vp(i0), p1 = vp(i1), p2 = vp(i2);
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({p0, n, tint});
        verts.push_back({p1, n, tint});
        verts.push_back({p2, n, tint});
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }
}

BuildResult runBuild(const std::filesystem::path& path,
                      const ViewportState& viewport,
                      const BuildOptions& options,
                      csg::MeshCache& cache,
                      const ProgressFn& onProgress,
                      const AbortFn& shouldAbort) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    auto elapsedMs = [&] {
        return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    };
    auto aborted = [&] { return shouldAbort && shouldAbort(); };
    auto progress = [&](BuildPhase p) {
        if (onProgress)
            onProgress(p);
    };

    BuildResult result;

    // ---- Phase: Parsing ----
    progress(BuildPhase::Parsing);
    spdlog::info("[mesh] Parsing {}", path.string());

    // Reads the root file, lexes/parses it, and recursively resolves any
    // include<>/use<> directives (in it or its includes) into one merged
    // ParseResult — see SourceLoader.h. Lex/parse-time diagnostics from every
    // file visited already carry their own filePath directly; loaded.files
    // (below, threaded into csgEval.fileTable) additionally lets eval-time
    // diagnostics (assert()/import()/surface()/text() errors) resolve their
    // AST node's SourceLoc::fileId back to a filePath the same way, so
    // DiagnosticsPanel's jump-to-file works across files for both.
    lang::LoadedSource loaded = lang::loadSource(path);
    auto& ast = loaded.result;

    bool hasError = false;
    for (const auto& d : loaded.diagnostics)
        if (d.level == lang::DiagLevel::Error) {
            hasError = true;
            break;
        }

    if (hasError) {
        result.diags = loaded.diagnostics;
        result.errorMsg = "Parse errors";
        progress(BuildPhase::Error);
        return result;
    }

    if (aborted())
        return result;

    // ---- Phase: Evaluating CSG ----
    progress(BuildPhase::Evaluating);
    csg::CsgEvaluator csgEval;
    csgEval.baseDir = path.parent_path(); // for import()'s relative paths
    csgEval.fileTable =
        &loaded.files; // per-file diagnostics + relative-path resolution across include/use

    // Full evaluate() overload (rather than the convenience one) so $vpr/
    // $vpt/$vpd/$vpf reflect the caller-supplied viewport.
    lang::Interpreter interp;
    interp.setViewport(viewport.vpr[0], viewport.vpr[1], viewport.vpr[2], viewport.vpt[0],
                        viewport.vpt[1], viewport.vpt[2], viewport.vpd, viewport.vpf);
    interp.loadAssignments(ast);
    interp.loadFunctions(ast);
    auto scene = csgEval.evaluate(ast, interp);

    // Forward echo() output as Info diagnostics
    for (const auto& msg : scene.echoMessages) {
        lang::Diagnostic d;
        d.level = lang::DiagLevel::Info;
        d.message = msg;
        result.diags.push_back(std::move(d));
    }
    // Forward assert() failures as Error diagnostics
    for (auto& d : scene.evalDiags)
        result.diags.push_back(std::move(d));

    if (aborted())
        return result;

    // ---- Phase: Meshing (Manifold — the slow part) ----
    progress(BuildPhase::Meshing);
    csg::MeshEvaluator meshEval(cache);
    meshEval.useManifoldSphere = options.useManifoldSphere;
    std::vector<manifold::Manifold> rootMeshes;
    try {
        rootMeshes = meshEval.evaluate(scene);
    } catch (const std::exception& e) {
        result.errorMsg = std::string("Mesh error: ") + e.what();
        progress(BuildPhase::Error);
        return result;
    }
    for (auto& d : meshEval.diagnostics())
        result.diags.push_back(d);

    if (aborted())
        return result;

    // ---- Phase: Converting to vertex buffers ----
    progress(BuildPhase::Converting);

    // Each root is converted independently and appended — this keeps objects
    // that are spatially inside other objects visible (no boolean union across roots).
    std::vector<uint32_t> rootVertStart; // per-root start index into result.verts
    rootVertStart.reserve(rootMeshes.size());

    // Background ('%') roots were appended after the real ones by
    // MeshEvaluator::evaluate() — see CsgScene::backgroundRoots. They're
    // meshed and rendered like any other root, but excluded from volume/
    // surface-area/triangle stats and from the STL-exportable index range,
    // since OpenSCAD's '%' explicitly means "reference only, not the model."
    for (std::size_t ri = 0; ri < rootMeshes.size(); ++ri) {
        const bool isBackground = ri >= scene.roots.size();
        const auto& m = rootMeshes[ri];
        rootVertStart.push_back(static_cast<uint32_t>(result.verts.size()));

        if (!isBackground) {
            result.volume += m.Volume();
            result.surfaceArea += m.SurfaceArea();

            auto rawMesh = m.GetMeshGL();
            result.triCount += static_cast<uint32_t>(rawMesh.triVerts.size() / 3);
            result.vertCount += static_cast<uint32_t>(
                rawMesh.numProp > 0 ? rawMesh.vertProperties.size() / rawMesh.numProp : 0);
        }

        const csg::CsgNode& srcNode =
            isBackground ? *scene.backgroundRoots[ri - scene.roots.size()] : *scene.roots[ri];
        const csg::ColorAttr& rootColor = csg::nodeColor(srcNode);
        const glm::vec3 tint =
            rootColor.has ? glm::vec3(rootColor.value) : render::kDefaultVertexColor;

        std::vector<render::Vertex> verts;
        std::vector<uint32_t> indices;
        manifoldToMesh(m, tint, verts, indices);

        // Offset indices by the current vertex count before appending
        const auto base = static_cast<uint32_t>(result.verts.size());
        for (auto& idx : indices)
            idx += base;

        result.verts.insert(result.verts.end(), verts.begin(), verts.end());
        result.indices.insert(result.indices.end(), indices.begin(), indices.end());

        if (ri + 1 == scene.roots.size())
            result.realIndexCount = static_cast<uint32_t>(result.indices.size());
    }
    if (scene.backgroundRoots.empty())
        result.realIndexCount = static_cast<uint32_t>(result.indices.size());

    // ---- Optional: pairwise overlap detection ----
    // Background ('%') roots are deliberately excluded: overlapping a
    // reference/ghost object with the real model is the entire point of '%',
    // not a mistake worth warning about.
    if (options.warnOverlappingRoots && scene.roots.size() > 1) {
        // Compute per-root AABB from the already-converted vertex data
        const auto totalVerts = static_cast<uint32_t>(result.verts.size());
        auto rootAABB = [&](std::size_t ri) -> std::pair<glm::vec3, glm::vec3> {
            uint32_t start = rootVertStart[ri];
            uint32_t end = (ri + 1 < rootVertStart.size()) ? rootVertStart[ri + 1] : totalVerts;
            glm::vec3 bmin{1e30f, 1e30f, 1e30f};
            glm::vec3 bmax{-1e30f, -1e30f, -1e30f};
            for (uint32_t vi = start; vi < end; ++vi) {
                bmin = glm::min(bmin, result.verts[vi].pos);
                bmax = glm::max(bmax, result.verts[vi].pos);
            }
            return {bmin, bmax};
        };

        auto aabbOverlap = [](glm::vec3 mn1, glm::vec3 mx1, glm::vec3 mn2, glm::vec3 mx2) {
            return (mn1.x <= mx2.x && mx1.x >= mn2.x) && (mn1.y <= mx2.y && mx1.y >= mn2.y) &&
                   (mn1.z <= mx2.z && mx1.z >= mn2.z);
        };

        for (std::size_t i = 0; i < scene.roots.size(); ++i) {
            if (aborted())
                return result;
            auto [mn1, mx1] = rootAABB(i);
            for (std::size_t j = i + 1; j < scene.roots.size(); ++j) {
                auto [mn2, mx2] = rootAABB(j);
                if (!aabbOverlap(mn1, mx1, mn2, mx2))
                    continue;

                // AABBs overlap — run exact Manifold intersection
                manifold::Manifold sect = rootMeshes[i] ^ rootMeshes[j];
                if (std::abs(sect.Volume()) > 1e-6) {
                    lang::Diagnostic warn;
                    warn.level = lang::DiagLevel::Warning;
                    warn.message = "Objects " + std::to_string(i + 1) + " and " +
                                   std::to_string(j + 1) +
                                   " overlap — wrap in union() or difference() if intentional";
                    result.diags.push_back(std::move(warn));
                }
            }
        }
    }
    result.elapsedMs = elapsedMs();

    // A newer generation may have been requested while the above ran — skip
    // publishing the Done phase for a superseded generation so the on-screen
    // overlay never shows generation N's numbers after generation N+1 has
    // already been queued. The mesh/result itself is still returned either
    // way; it's up to the caller (MeshBuilder) to discard it.
    if (!aborted())
        progress(BuildPhase::Done);

    auto fmtN = [](uint32_t n) {
        std::string s = std::to_string(n);
        int i = static_cast<int>(s.size()) - 3;
        while (i > 0) {
            s.insert(static_cast<size_t>(i), ",");
            i -= 3;
        }
        return s;
    };
    spdlog::info(
        "[mesh] Render: {:.3f}s  |  {} facets  |  {} vertices  |  volume: {:.4f}  |  area: {:.4f}",
        result.elapsedMs / 1000.0, fmtN(result.triCount), fmtN(result.vertCount), result.volume,
        result.surfaceArea);

    return result;
}

} // namespace chisel::app
