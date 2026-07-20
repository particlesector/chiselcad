# scad_dump — corpus comparison against real OpenSCAD

`scad_dump.cpp` is a small standalone CLI (not part of the CMake build) that
parses and evaluates a `.scad` file the same way `MeshBuilder::buildOne()`
does (`loadSource()` → `CsgEvaluator::evaluate()`), then prints diagnostics
and `echo()` output in a format that lines up with real OpenSCAD's own CLI
stderr output. This makes it possible to diff ChiselCAD's language-level
behavior against a live-run OpenSCAD binary, file by file, instead of relying
on a manual read of the source against the OpenSCAD manual (which has missed
real bugs in every prior audit pass — see docs/roadmap.md v3–v3.7).

It deliberately avoids the full CMake build (which requires Vulkan/GLFW/
Manifold via vcpkg): every source file it pulls in is already GPU/Manifold-
free (the same set CMakeLists.txt calls out as "Sources under test" for
`chiselcad_tests`), so it builds with plain `g++` and only needs `zlib`.

## Building

```bash
g++ -std=c++20 -O1 -Isrc -Ithird_party -DCHISELCAD_RESOURCE_DIR='"resources/"' \
    src/lang/Lexer.cpp src/lang/Parser.cpp src/lang/Interpreter.cpp \
    src/lang/SourceLoader.cpp src/csg/CsgEvaluator.cpp \
    src/import/StlLoader.cpp src/import/OffLoader.cpp src/import/DxfLoader.cpp \
    src/import/SvgLoader.cpp src/import/MiniXml.cpp src/import/ZipReader.cpp \
    src/import/ThreeMfLoader.cpp src/import/AmfLoader.cpp \
    src/import/SurfaceLoader.cpp src/import/stb_truetype_impl.cpp \
    src/import/stb_image_impl.cpp src/import/StbFontBackend.cpp \
    src/import/NaiveLtrShaper.cpp src/import/TextLoader.cpp \
    tests/tools/scad_dump.cpp -lz -o scad_dump
```

## Running against OpenSCAD's own test corpus

OpenSCAD's own repo ships ~120 "echo regression" test files — `.scad`
programs whose entire purpose is to `echo()` computed values (no geometry
needed), each with an upstream-expected `.echo` output. Rather than trust
those checked-in expectations (which may be pinned to a different OpenSCAD
version than whatever's installed locally), generate ground truth live:

```bash
apt-get install -y openscad          # 2021.01 in Ubuntu noble; runs fully
                                      # headless for non-geometry output, no
                                      # Xvfb needed:
                                      #   openscad --export-format asciistl \
                                      #     -o /dev/null file.scad
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/openscad/openscad.git /tmp/openscad-src
git -C /tmp/openscad-src sparse-checkout set tests/data/scad tests/regression

for expected in /tmp/openscad-src/tests/regression/echo/*-expected.echo; do
    base=$(basename "$expected" -expected.echo)
    scad=$(find /tmp/openscad-src/tests/data/scad -iname "${base}.scad" | head -1)
    [ -z "$scad" ] && continue
    diff <(openscad --export-format asciistl -o /dev/null "$scad" 2>&1 >/dev/null \
             | grep -Ev '^(Geometries|Geometry cache|CGAL|Total rendering|Current top level)') \
         <(./scad_dump "$scad")
done
```

A byte-exact diff is a strict bar — OpenSCAD emits arity-mismatch and
"file not found" warnings with specific wording ChiselCAD doesn't replicate,
so plenty of "failures" are cosmetic wording differences, not wrong computed
values. Filter to just `ECHO:` lines to separate genuine value/behavior bugs
from diagnostic-wording noise.

## What this has already found (v3.7/v3.8, see docs/roadmap.md)

Two real bugs no source-level audit had caught, both fixed and covered by
unit tests in `tests/test_parser.cpp`/`test_interpreter.cpp`/
`test_csg_evaluator.cpp`:

- `$special = expr` (e.g. `f($fn=64, 1)`) wasn't recognized as a named
  argument in a generic function/module call — only in the dedicated
  primitive-param parsers — so it crashed the parser and cascaded into a
  wall of unrelated errors for the rest of the file.
- Once parseable, a `$`-prefixed named argument now binds into scope for
  named-function and function-literal calls too (module calls already did
  this), matching OpenSCAD's dynamic-scoping semantics for special
  variables passed as call overrides.

Confirmed but still open (see docs/roadmap.md v3.8): `.x`/`.y`/`.z`/`.w`
vector/range member access, calling the result of an arbitrary expression
directly (`(function(x) ...)(2)(5)` currying), the exact interleaving order
of named vs. positional arguments when both target the same parameter slot,
and a cluster of Unicode/scoping/builtin edge cases still being triaged.

## scad_to_stl / stl_diff — geometry validation (v3.9)

The `scad_dump` comparison above only covers `echo()`-based tests — computed
*values*, never actual mesh output. That's deliberate: ChiselCAD (Manifold)
and OpenSCAD (CGAL, or its own separate Manifold integration) tessellate
primitives completely differently, so a raw point/vertex diff between their
STL exports would "fail" even for a byte-perfect-correct geometry engine.

The tessellation-agnostic fix is a **volumetric symmetric-difference**
check: export the same `.scad` file to STL from both, reload each as a
Manifold, and measure the volume of `(A−B)∪(B−A)`. Two meshes representing
the same solid have a symmetric-difference volume of ~0 regardless of how
differently each one discretized it.

This needs the *real* C++ Manifold library, which isn't in `apt` — but it
builds standalone from source in ~20s with zero extra dependencies (just
pulls in Clipper2 via `FetchContent`). Pin to **v3.5.2**: Manifold's
`master` branch has since dropped `CrossSection::FillRule`, which
`src/csg/PrimitiveGen.cpp`/`MeshEvaluator.cpp` still use.

```bash
git clone --depth 1 --branch v3.5.2 https://github.com/elalish/manifold.git /tmp/manifold-src
cmake -B /tmp/manifold-src/build -S /tmp/manifold-src -DCMAKE_BUILD_TYPE=Release \
    -DMANIFOLD_TEST=OFF -DMANIFOLD_PAR=OFF -DMANIFOLD_PYBIND=OFF -DMANIFOLD_CBIND=OFF \
    -DMANIFOLD_USE_BUILTIN_TBB=OFF
cmake --build /tmp/manifold-src/build -j"$(nproc)"

MF_INC=/tmp/manifold-src/include
MF_BUILD_INC=/tmp/manifold-src/build/include
MF_LIB=/tmp/manifold-src/build/src

# ChiselCAD's real mesh pipeline (CsgEvaluator -> MeshEvaluator -> PrimitiveGen),
# same code MeshBuilder::buildOne() runs — not a reimplementation:
g++ -std=c++20 -O1 -Isrc -Ithird_party -I"$MF_INC" -I"$MF_BUILD_INC" \
    -DCHISELCAD_RESOURCE_DIR='"resources/"' \
    src/lang/Lexer.cpp src/lang/Parser.cpp src/lang/Interpreter.cpp \
    src/lang/SourceLoader.cpp src/csg/CsgEvaluator.cpp src/csg/MeshEvaluator.cpp \
    src/csg/PrimitiveGen.cpp src/csg/MeshCache.cpp \
    src/import/StlLoader.cpp src/import/OffLoader.cpp src/import/DxfLoader.cpp \
    src/import/SvgLoader.cpp src/import/MiniXml.cpp src/import/ZipReader.cpp \
    src/import/ThreeMfLoader.cpp src/import/AmfLoader.cpp src/import/SurfaceLoader.cpp \
    src/import/stb_truetype_impl.cpp src/import/stb_image_impl.cpp \
    src/import/StbFontBackend.cpp src/import/NaiveLtrShaper.cpp src/import/TextLoader.cpp \
    tests/tools/scad_to_stl.cpp \
    -L"$MF_LIB" -lmanifold -lfmt -lz -Wl,-rpath,"$MF_LIB" -o scad_to_stl

g++ -std=c++20 -O1 -I"$MF_INC" -I"$MF_BUILD_INC" tests/tools/stl_diff.cpp \
    -L"$MF_LIB" -lmanifold -Wl,-rpath,"$MF_LIB" -o stl_diff

# Compare one file:
openscad --export-format asciistl -o real.stl some_file.scad
./scad_to_stl some_file.scad chisel.stl
./stl_diff real.stl chisel.stl
# -> volume_a=... volume_b=... sym_diff_volume=... rel_error=...
```

`rel_error` near 0 (e.g. 1e-6, from float-precision + differing
tessellation) means the same solid; anywhere from a few percent up means a
real geometry bug. This immediately found and fixed one: `polyhedron()`
faces came out with exactly negated volume on every test file (OpenSCAD's
documented clockwise-from-outside face-winding convention wasn't being
flipped to Manifold's counter-clockwise-from-outside expectation before
fan-triangulating). See docs/roadmap.md v3.9 for the full fixed/open list —
only one corpus subdirectory (`tests/data/scad/3D/features`) has been run
through this so far.
