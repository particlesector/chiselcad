# ChiselCAD — Roadmap

> Bugs and feature work are tracked as individual [GitHub Issues](https://github.com/particlesector/chiselcad/issues),
> not roadmap checkboxes. This file only tracks forward-looking, unfiled work.

## Status

Core CSG engine, full OpenSCAD language support, and corpus-validated
correctness (echo-output and volumetric comparison against real OpenSCAD)
are complete — see git history / closed issues for that work. Remaining
known language/testing gaps are tracked as GitHub issues (labeled
`help wanted`) rather than listed here.

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
