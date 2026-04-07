# ChiselCAD — Roadmap

## v1 — Core CSG (current focus)

- [ ] CMake + vcpkg build scaffold
- [ ] Vulkan context, swapchain, ImGui integration
- [ ] Lexer + recursive descent parser (core subset)
- [ ] CSG tree evaluator (AST → CsgNode tree)
- [ ] Primitive tessellator (cube, sphere, cylinder)
- [ ] Manifold boolean evaluation (union, difference, intersection)
- [ ] Async eval pipeline with std::stop_token cancellation
- [ ] Preview render mode (color-coded primitives)
- [ ] Result render mode (evaluated mesh, PBR shading)
- [ ] GPU mesh double-buffer swap
- [ ] Arcball orbit camera
- [ ] File watcher + VS Code external editor integration
- [ ] Diagnostics panel with clickable jump-to-line
- [ ] Binary STL export
- [ ] Embedded ImGuiColorTextEdit editor
- [ ] Mesh cache (LRU, hash-keyed by CSG subtree)

## v2 — Language Expansion

- [ ] Full OpenSCAD language: `for`, `if`, `let`, variables, math functions
- [ ] User-defined modules and function literals
- [ ] 2D primitives: `square`, `circle`, `polygon`
- [ ] Extrusion: `linear_extrude`, `rotate_extrude`
- [ ] `hull()` and `minkowski()`

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
