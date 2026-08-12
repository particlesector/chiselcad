# ChiselCAD — Roadmap

> Bugs and feature work are tracked as individual [GitHub Issues](https://github.com/particlesector/chiselcad/issues),
> not roadmap checkboxes. This file only tracks forward-looking, unfiled work.

## Status

Core CSG engine, full OpenSCAD language support, and corpus-validated
correctness (echo-output and volumetric comparison against real OpenSCAD)
are complete — see git history / closed issues for that work. The only
currently open issue is
[#85](https://github.com/particlesector/chiselcad/issues/85) (cosmetic:
ChiselCAD doesn't emit OpenSCAD's arity-mismatch/file-not-found diagnostic
*wording*, though computed values already match — low priority, would
require porting a large chunk of OpenSCAD's diagnostic message catalog).

## Known gaps (not yet implemented)

- `assert()`/`echo()` as chainable **expressions** (valid OpenSCAD since
  2019.05) — currently statement-only; would need a diagnostics/echo sink
  threaded through `Interpreter::evaluate`, which has none today.
- List comprehensions don't support multi-variable `for` clauses, C-style
  `for(init; cond; next)`, or nested `for` clauses within one bracket.
- `roof()` (OpenSCAD 2021.01+, still experimental upstream) not implemented.
- `textmetrics()`/`fontmetrics()` text-layout introspection not implemented.
- `linear_extrude()`'s `segments=` parameter (edge subdivision for
  twisted/non-uniformly-scaled extrusions) is parsed nowhere — real
  OpenSCAD uses it to smooth the swept surface in that case.
- OpenSCAD corpus validation has only covered the `3D/features`
  subdirectory; `2D`, `bugs`, `bugs2D`, `misc`, `issues` are unexamined.

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
