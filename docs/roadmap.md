# ChiselCAD — Roadmap

> Correctness bugs are tracked as individual [GitHub Issues](https://github.com/particlesector/chiselcad/issues),
> not roadmap checkboxes. A July 2026 audit filed 33 (5 Critical, 7 High, 14
> Medium, 7 Low) across the lexer/parser, interpreter, CSG evaluator, mesh
> generation, and file I/O. The Critical/High ones block any "drop-in
> replacement" claim and should be fixed ahead of the v3 work below.

## v1 — Core CSG ✓

- [x] CMake + vcpkg build scaffold
- [x] Vulkan context, swapchain, ImGui integration
- [x] Lexer + recursive descent parser
- [x] CSG tree evaluator (AST → CsgNode tree)
- [x] Primitive tessellator (cube, sphere, cylinder)
- [x] Manifold boolean evaluation (union, difference, intersection)
- [x] Async eval pipeline (background thread, generation-counter cancellation)
- [x] Result render mode (Blinn-Phong shading)
- [x] Arcball orbit camera
- [x] File watcher + VS Code external editor integration
- [x] Diagnostics panel with clickable jump-to-line
- [x] Binary STL export
- [x] Mesh cache (LRU, keyed by resolved params/transform)

## v2 — Language Expansion ✓

- [x] Full OpenSCAD language: `for`, `if`, `let`, variables, math functions
- [x] User-defined modules and function literals
- [x] 2D primitives: `square`, `circle`, `polygon`
- [x] Extrusion: `linear_extrude`, `rotate_extrude`
- [x] `hull()` and `minkowski()`

## v2.5 — OpenSCAD Language Completeness ✓

- [x] List indexing, ternary, user-defined functions, `let` expression, `undef`, `concat()`
- [x] Math/string completeness: inverse trig, `norm()`/`cross()`/`sign()`, `rands()`/`lookup()`, string literals + `str()`/`chr()`/`ord()`, `len()`
- [x] Module system: `children()`/`$children`, `echo()`, `assert()`, recursive functions
- [x] Geometry ops: `multmatrix()`, `render()`, `color()`, `offset()`, `projection()`
- [x] File I/O: `include`/`use` (with circular-include detection), `import()` (STL), `surface()` (text `.dat`), `text()` (Latin/Western, via vendored stb_truetype)

## v3 — OpenSCAD Language Completeness (final gaps) ✓

All 33 issues from the July 2026 correctness audit are closed. The items
below are sequenced into four phases: fix the one known correctness bug
first, land the expression-language features most real-world `.scad` files
depend on (list comprehensions need general range literals first), then the
lower-risk debug/parser ergonomics, then the remaining geometry primitives
(ending with nested extrusion, the architecturally trickiest one), and
close out with import/export breadth, which is additive and blocks nothing
else.

### Phase 1 — Correctness bug + core expression language ✓
- [x] Module-local variable assignments — currently parsed and silently discarded, a real bug
- [x] General range-literal expressions (`x = [0:5];` outside `for`)
- [x] List comprehensions (`[for (i=range) expr]`, with `if`) and `each` (depends on range literals above)

### Phase 2 — Parser/debug ergonomics ✓
- [x] CSG modifier characters `# % ! *` (root/background/disable/debug)

### Phase 3 — Geometry primitives & ops ✓
- [x] `polyhedron(points=..., faces=...)`
- [x] `resize(newsize, ...)`
- [x] Nested extrusion (extrude wrapping extrude) — currently a silent no-op

### Phase 4 — Import/export breadth ✓
- [x] Per-file diagnostics for code reached via `include`/`use`
- [x] PNG heightmap support for `surface()`
- [x] Additional `import()` formats: OFF, 3MF, AMF, DXF, SVG

## v4 — Tooling & Visual Quality

- [ ] VS Code LSP extension (syntax highlighting, error squiggles, completions)
- [ ] AI code assistant panel (Claude API integration)
- [ ] PBR material model + SSAO in result render mode
- [ ] Deferred shading pipeline
- [ ] Additional export formats: OBJ, 3MF
- [ ] UNDO/REDO via CSG tree snapshots
- [ ] Embedded in-app editor
- [ ] Preview render mode with operation-context color coding (union/difference/intersection children tinted differently)
- [ ] True double-buffered/lock-free GPU mesh swap

## Future / Research

- [ ] SDF raymarching preview mode (Vulkan compute)
- [ ] Custom boolean backend using Embree + Shewchuk robust predicates
- [ ] OpenVDB preview for ultra-complex models
- [ ] GPU-accelerated tessellation (compute shaders)
- [ ] macOS support (MoltenVK)
- [ ] Animation / parametric scrubbing
