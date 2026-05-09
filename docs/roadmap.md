# ChiselCAD — Roadmap

## v1 — Core CSG ✓

- [x] CMake + vcpkg build scaffold
- [x] Vulkan context, swapchain, ImGui integration
- [x] Lexer + recursive descent parser (core subset)
- [x] CSG tree evaluator (AST → CsgNode tree)
- [x] Primitive tessellator (cube, sphere, cylinder)
- [x] Manifold boolean evaluation (union, difference, intersection)
- [x] Async eval pipeline with std::stop_token cancellation
- [x] Preview render mode (color-coded primitives)
- [x] Result render mode (evaluated mesh, Blinn-Phong shading)
- [x] GPU mesh double-buffer swap
- [x] Arcball orbit camera
- [x] File watcher + VS Code external editor integration
- [x] Diagnostics panel with clickable jump-to-line
- [x] Binary STL export
- [x] Embedded ImGuiColorTextEdit editor
- [x] Mesh cache (LRU, hash-keyed by CSG subtree)

## v2 — Language Expansion ✓

- [x] Full OpenSCAD language: `for`, `if`, `let`, variables, math functions
- [x] User-defined modules and function literals
- [x] 2D primitives: `square`, `circle`, `polygon`
- [x] Extrusion: `linear_extrude`, `rotate_extrude`
- [x] `hull()` and `minkowski()`

## v2.5 — OpenSCAD Language Completeness

### Tier A — High Impact (used constantly) ✓
- [x] List indexing `v[i]`
- [x] Ternary operator `condition ? a : b`
- [x] User-defined functions `function f(x) = expr;`
- [x] `let` expression `let (x=10) child`
- [x] `undef` literal
- [x] `concat()` built-in

### Tier B — Math & String Completeness ✓
- [x] Inverse trig: `asin()`, `acos()`, `atan()`, `atan2()`
- [x] Vector math: `norm()`, `cross()`, `sign()`
- [x] `rands()`, `lookup()`
- [x] String literals + `str()`, `chr()`, `ord()`
- [x] `len()` on strings

### Tier C — Module System Completeness
- [ ] `children()` / `$children`
- [ ] `echo()`
- [ ] `assert()`
- [ ] Recursive functions (enabled by user-defined functions)

### Tier D — Geometry Operations
- [ ] `multmatrix()`
- [ ] `color()`
- [ ] `offset()`
- [ ] `projection()`
- [ ] `render()`

### Tier E — File I/O (complex)
- [ ] `include <>` / `use <>`
- [ ] `import()`
- [ ] `surface()`
- [ ] `text()` (requires font rendering — significant work)

## v3 — Tooling & Visual Quality

- [ ] VS Code LSP extension (syntax highlighting, error squiggles, completions)
- [ ] AI code assistant panel (Claude API integration)
- [ ] SSAO in result render mode
- [ ] Deferred shading pipeline
- [ ] Additional export formats: OBJ, 3MF
- [ ] UNDO/REDO via CSG tree snapshots

## Future / Research

- [ ] SDF raymarching preview mode (Vulkan compute)
- [ ] Custom boolean backend using Embree + Shewchuk robust predicates
- [ ] OpenVDB preview for ultra-complex models
- [ ] GPU-accelerated tessellation (compute shaders)
- [ ] macOS support (MoltenVK)
- [ ] Animation / parametric scrubbing
