// scad_dump — headless corpus-comparison runner (not part of the CMake
// build; compiled standalone for comparing ChiselCAD's language/echo output
// against real OpenSCAD's, see docs/roadmap.md v3.7 audit follow-up).
//
// Usage: scad_dump <file.scad>
//
// Parses/evaluates the file exactly the way MeshBuilder::buildOne() does
// (loadSource -> CsgEvaluator::evaluate), then prints diagnostics and echo
// messages in a format matching OpenSCAD's own CLI output closely enough to
// diff line-by-line against `openscad -o /dev/null <file>`'s stderr:
//   WARNING: <message>
//   ERROR: <message>
//   ECHO: <message>
#include "lang/SourceLoader.h"
#include "csg/CsgEvaluator.h"
#include <cstdio>
#include <string>

using namespace chisel;

static const char* levelWord(lang::DiagLevel lvl) {
    switch (lvl) {
    case lang::DiagLevel::Error:   return "ERROR";
    case lang::DiagLevel::Warning: return "WARNING";
    default:                       return "INFO";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file.scad>\n", argv[0]);
        return 2;
    }

    std::filesystem::path path = argv[1];
    lang::LoadedSource loaded = lang::loadSource(path);
    auto& ast = loaded.result;

    for (const auto& d : loaded.diagnostics)
        std::printf("%s: %s\n", levelWord(d.level), d.message.c_str());

    bool hasError = false;
    for (const auto& d : loaded.diagnostics)
        if (d.level == lang::DiagLevel::Error) hasError = true;
    if (hasError) return 1;

    csg::CsgEvaluator csgEval;
    csgEval.baseDir = path.parent_path();
    csgEval.fileTable = &loaded.files;

    lang::Interpreter interp;
    interp.loadAssignments(ast);
    interp.loadFunctions(ast);
    auto scene = csgEval.evaluate(ast, interp);

    for (const auto& d : scene.evalDiags)
        std::printf("%s: %s\n", levelWord(d.level), d.message.c_str());
    for (const auto& msg : scene.echoMessages)
        std::printf("%s\n", msg.c_str());

    return 0;
}
