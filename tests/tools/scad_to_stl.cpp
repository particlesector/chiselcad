// scad_to_stl — runs ChiselCAD's *real* mesh pipeline (the same
// CsgEvaluator -> MeshEvaluator -> Manifold path MeshBuilder::buildOne()
// uses) on a .scad file and writes the unioned result as ASCII STL, in the
// same text format OpenSCAD's `--export-format asciistl` produces.
//
// Not part of the CMake build — see tests/tools/README.md for how to build
// and use this alongside stl_diff to compare ChiselCAD's actual geometry
// output against real OpenSCAD's, tessellation differences aside (see
// docs/roadmap.md v3.8/v3.9).
//
// Usage: scad_to_stl <file.scad> <out.stl>
#include "lang/SourceLoader.h"
#include "csg/CsgEvaluator.h"
#include "csg/MeshEvaluator.h"
#include "csg/MeshCache.h"
#include <cstdio>
#include <fstream>
#include <glm/glm.hpp>

using namespace chisel;

static void writeAsciiStl(const manifold::MeshGL& mesh, const std::string& path) {
    std::ofstream out(path);
    out << "solid ChiselCAD_Model\n";
    const auto& vp = mesh.vertProperties;
    const auto  np = mesh.numProp;
    for (std::size_t t = 0; t + 2 < mesh.triVerts.size(); t += 3) {
        auto vertAt = [&](std::size_t i) -> glm::dvec3 {
            std::size_t base = static_cast<std::size_t>(mesh.triVerts[i]) * np;
            return {vp[base], vp[base + 1], vp[base + 2]};
        };
        glm::dvec3 a = vertAt(t), b = vertAt(t + 1), c = vertAt(t + 2);
        glm::dvec3 n = glm::cross(b - a, c - a);
        double len = glm::length(n);
        if (len > 0) n /= len;
        out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
        out << "    outer loop\n";
        for (auto v : {a, b, c})
            out << "      vertex " << v.x << " " << v.y << " " << v.z << "\n";
        out << "    endloop\n  endfacet\n";
    }
    out << "endsolid ChiselCAD_Model\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <file.scad> <out.stl>\n", argv[0]);
        return 2;
    }
    std::filesystem::path path = argv[1];

    lang::LoadedSource loaded = lang::loadSource(path);
    auto& ast = loaded.result;
    for (const auto& d : loaded.diagnostics)
        if (d.level == lang::DiagLevel::Error) {
            std::fprintf(stderr, "ERROR: %s\n", d.message.c_str());
            return 1;
        }

    csg::CsgEvaluator csgEval;
    csgEval.baseDir = path.parent_path();
    csgEval.fileTable = &loaded.files;

    lang::Interpreter interp;
    interp.loadAssignments(ast);
    interp.loadFunctions(ast);
    auto scene = csgEval.evaluate(ast, interp);

    for (const auto& d : scene.evalDiags)
        if (d.level == lang::DiagLevel::Error) {
            std::fprintf(stderr, "ERROR: %s\n", d.message.c_str());
            return 1;
        }

    csg::MeshCache cache;
    csg::MeshEvaluator meshEval(cache);
    auto rootMeshes = meshEval.evaluate(scene);
    for (const auto& d : meshEval.diagnostics())
        std::fprintf(stderr, "WARNING: %s\n", d.message.c_str());

    // Only the "real" roots (scene.roots.size() of them) count as the
    // exportable model — background ('%') roots are appended after and
    // excluded, matching MeshBuilder's STL-export convention. A root with
    // invalid/degenerate geometry (e.g. cylinder(r1=0, r2=0) — already
    // reported above via meshEval.diagnostics()) is dropped before
    // unioning: BatchBoolean(Add) across a set that includes an
    // invalid-status Manifold produces a garbage/empty *combined* result,
    // silently discarding every valid root too — chiselcad_cli's per-root
    // (non-unioned) pipeline doesn't have this failure mode, so this only
    // affects this tool's true-union comparison, not the real product.
    std::vector<manifold::Manifold> realRoots;
    for (std::size_t i = 0; i < scene.roots.size(); ++i)
        if (rootMeshes[i].Status() == manifold::Manifold::Error::NoError)
            realRoots.push_back(rootMeshes[i]);

    manifold::Manifold combined = realRoots.empty()
        ? manifold::Manifold()
        : manifold::Manifold::BatchBoolean(realRoots, manifold::OpType::Add);

    writeAsciiStl(combined.GetMeshGL(), argv[2]);
    std::fprintf(stderr, "wrote %zu triangles, volume=%g\n",
                 combined.NumTri(), combined.Volume());
    return 0;
}
