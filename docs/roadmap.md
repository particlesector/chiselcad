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

## v3.5 — OpenSCAD Language Completeness (audit follow-up) ✓

The v3 phases above closed the 33 filed correctness issues, but "feature
complete with OpenSCAD" was never verified against OpenSCAD's actual
builtin/special-variable surface — a July 2026 completeness audit (source
read, not just the issue tracker) found real gaps the v3 checklist didn't
cover. Fixed:

- [x] `PI` constant (was entirely absent — scripts had to hard-code 3.14159)
- [x] `is_undef()`/`is_bool()`/`is_num()`/`is_string()`/`is_list()`/`is_function()` type predicates
- [x] `search()` (string/list/table search, matching OpenSCAD's flat-vs-nested result shape)
- [x] `version()`/`version_num()` (fixed compatibility level, so version-gated library code picks its modern branch)
- [x] `$preview`/`$t` special variables (fixed defaults: no animation/dual-render-pass distinction exists yet)
- [x] `parent_module(idx)`/`$parent_modules`
- [x] `linear_extrude(slices=...)` — was parsed but silently ignored; twist division count now honors it over the `$fn` default
- [x] `children([vector])` / `children([range])` — only a single plain-number index worked before
- [x] `echo(name=value)` now formats as `name = value`, matching OpenSCAD

**Explicitly deferred, not done in this pass** (tracked here so they aren't
lost, not because they don't matter):
- First-class function literals (`f = function(x) x*2;`, OpenSCAD 2019.05+)
  as values assignable to variables/passable as arguments — the language
  has named `function foo(x) = expr;` defs but no closure value type; adding
  one touches the `Value` variant, the parser grammar, and call dispatch
  broadly enough that it deserves its own pass rather than riding along with
  smaller fixes.
- `$vpr`/`$vpt`/`$vpd` viewport special variables — would need the render
  layer's camera state plumbed into the interpreter, a cross-subsystem wire
  that doesn't exist today; rarely load-bearing in real `.scad` files.
- `cube`/`sphere`/`translate`/etc. are reserved lexer keywords rather than
  ordinary identifiers, so (unlike real OpenSCAD) a script can't use one of
  those names as a variable. Real-world impact is low, but it's a genuine
  parser-level deviation and any fix is a grammar-level change, not a
  point fix — out of scope here.

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
