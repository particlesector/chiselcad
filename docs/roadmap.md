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
- [x] `$vpr`/`$vpt`/`$vpd` viewport special variables — `MeshBuilder`'s
  `requestBuild()` now takes a `ViewportState` snapshot (derived from
  `Application::currentViewport()`, reading the camera's pitch/yaw/target/
  distance) and plumbs it into a pre-populated `Interpreter` before
  evaluation, mirroring how `fileTable` is already threaded through the
  `evaluate(result, interp)` overload. Headless/test evaluation (a
  default-constructed `Interpreter`) falls back to OpenSCAD's own defaults
  (`$vpr=[55,0,25]`, `$vpt=[0,0,0]`, `$vpd=140`) instead of `undef`. No
  roll in this camera model, so `$vpr[1]` is always 0.
- [x] `cube`/`sphere`/`translate`/etc. are no longer reserved lexer
  keywords — matching real OpenSCAD, they're ordinary identifiers that the
  Parser recognises by name only at statement-start call position
  (`kBuiltinNodeNames` in `Parser.cpp`). The Lexer's keyword table now
  holds only genuine grammar keywords (`if`/`else`/`for`/`each`/`module`/
  `function`/`let`/`include`/`use`/`undef`/`true`/`false`); builtins and
  variables live in separate namespaces, so `cube = 5;` followed later by
  `cube(cube);`, a module parameter named `scale`, or a `for`/`let`
  variable named `rotate` all now parse correctly. This was the last
  gap deferred from the initial v3.5 pass, closing out feature completeness
  with OpenSCAD's builtin/keyword surface.

## v3.6 — First-class function literals ✓

- [x] Function literals (`f = function(x) x*2;`, OpenSCAD 2019.05+) as
  values: assignable to variables, storable in lists, passable as
  arguments to (and returnable from) other functions, and callable later
  via ordinary `f(...)` syntax wherever `f` names a variable holding one.
  Adds a `Value::Tag::Function` closure (AST pointer + captured defining
  scope) alongside the existing named `function foo(x) = expr;` form.
  Direct self-binding (`fact = function(n) n<=1 ? 1 : n*fact(n-1);`)
  recurses by name without needing a named `function` def.
- [x] `is_function()` now recognizes actual function values (was a
  stubbed-`false` placeholder pending this work).

## v3.7 — OpenSCAD completeness re-audit ✓

A July 2026 re-audit (source read against OpenSCAD's actual builtin surface,
not just the v3/v3.5/v3.6 checklists) found three real gaps the prior passes
missed:

- [x] `log()`/`ln()` — `log()` was implemented as the *natural* logarithm
  (`std::log`), and there was no `ln()` at all. OpenSCAD has this backwards
  from C: `log()` is base-10, `ln()` is natural log. Easy mistake (C's own
  `log()` is natural log) but a real correctness bug for any script doing
  `log(100)` expecting `2`. Fixed: `log()` now calls `std::log10`, `ln()`
  added as `std::log`. The stray non-OpenSCAD `log10()` name this
  interpreter had invented is gone (folded into the now-correct `log()`).
- [x] `str()` only handled number/bool/string/undef arguments — a vector,
  range, or function argument silently contributed nothing (`str([1,2,3])`
  returned `""` instead of `"[1, 2, 3]"`). Fixed with a recursive formatter
  (`formatValueRec`) matching OpenSCAD's rule: a bare top-level string
  argument is unquoted, but a string nested inside a vector is quoted
  (`str("hi") == "hi"` but `str([1,"hi"]) == [1, "hi"]`) — mirrors
  `CsgEvaluator::formatValue`'s existing echo()/assert() formatting, kept as
  a separate implementation since `Interpreter` has no dependency on
  `CsgEvaluator`.
- [x] `$vpf` (viewport field-of-view) special variable — `$vpr`/`$vpt`/`$vpd`
  were plumbed through in v3.5 but `$vpf` was missed. Added the same way:
  `ViewportState::vpf` (default 22.5°, matching OpenSCAD's own default)
  plumbed from `Camera::fovDeg` through `Application::currentViewport()` →
  `MeshBuilder::buildOne()` → `Interpreter::setViewport()`.

Gaps identified but deliberately not closed in this pass — real OpenSCAD
surface, but each either architecturally deeper or low-value enough to
warrant a separate decision rather than folding into this fix-up:

- [ ] `assert()`/`echo()` as chainable **expressions** (`function f(x) =
  assert(x>0, "must be positive") x*2;`, valid OpenSCAD since 2019.05).
  Currently both are statement-only (dispatched in `CsgEvaluator`, which has
  the diagnostics/echo-message sinks); a function body only ever reaches
  `Interpreter::evaluate`, which has no diagnostic channel at all today.
  Wiring this in cleanly means threading a diagnostics/echo sink through
  every `Interpreter::evaluate` call site, not just adding a case to
  `callBuiltin`.
- [ ] List comprehensions don't support multi-variable `for` clauses
  (`[for (i=[0:2], j=[0:2]) i*3+j]`), C-style `for(init; cond; next)`, or
  nested `for` clauses within one bracket (`[for (i=a) for (j=b) i+j]`) —
  `ListCompExpr`/`ListCompBody` (Expr.h) are explicitly single-variable only.
- [ ] `roof()` (OpenSCAD 2021.01+, and still marked experimental upstream)
  not implemented.
- [ ] `textmetrics()`/`fontmetrics()` text-layout introspection functions
  (2021.01+) not implemented.
- [ ] Export is binary STL only. Every one of the import formats this
  project already supports (OFF/AMF/3MF/DXF/SVG) has no matching exporter,
  unlike real OpenSCAD which round-trips all of them plus CSG/PNG. Already
  tracked below under v4 ("Additional export formats"), scoped there to
  OBJ/3MF — worth revisiting for the fuller OpenSCAD export list.

## v3.8 — Corpus validation against OpenSCAD's own test suite

Every prior completeness pass (v3–v3.7) was a source-level read against the
OpenSCAD manual/builtin list — and each pass found real bugs the previous
ones missed anyway. That pattern (not the absence of bugs) is what prompted
a different kind of check: install real OpenSCAD (`apt install openscad`,
2021.01 on Ubuntu noble — runs fully headless for non-geometry output, no
Xvfb needed) and run its own ~120-file `echo` regression-test corpus through
both it and a new headless ChiselCAD runner (`tests/tools/scad_dump.cpp` —
see `tests/tools/README.md`), diffing output line by line. This found two
real bugs in under an hour that four rounds of manual audit had missed:

- [x] **Parser crash on `$special = expr` as a plain call argument.**
  `f($fn=64, 1)` (or the same on a user module call) wasn't recognized as a
  named argument at all outside the primitive-specific param parsers — the
  parser tried to parse `$fn` as a positional expression, then choked on the
  trailing `=`, and the recovery cascaded into a wall of unrelated parse
  errors for the rest of the file. This one test file alone
  (`arg-permutations.scad`) was silently invalidating ~30 of the corpus's
  ~120 files' worth of signal. Fixed in both the function-call and
  module-call argument-list parsers.
- [x] **`$special` named args now dynamically scope into function/closure
  calls, not just module calls.** Once parseable, `CsgEvaluator`'s
  module-call binding already applied *any* named arg into scope regardless
  of whether it matched a declared parameter (correct — special variables
  are dynamically scoped, not ordinary parameters). `Interpreter`'s
  function-call and closure-call binding didn't do the same, so
  `function f() = $fn; f($fn=64)` would parse but silently ignore the
  override. Fixed to match, and verified byte-for-byte against real
  OpenSCAD's output for both cases.

Both are covered by regression tests (`tests/test_parser.cpp`,
`tests/test_interpreter.cpp`, `tests/test_csg_evaluator.cpp`) and verified
against the full 544-test suite (built directly with system `g++`/`apt`
packages for Catch2/glm/spdlog — see `tests/tools/README.md` — since this
environment has no vcpkg/Manifold/Vulkan).

Confirmed via the same corpus run but **not yet fixed** — real gaps, ranked
roughly by expected real-world impact:

- [ ] **Dot-member access on vectors/ranges**: `v.x`/`v.y`/`v.z`/`v.w` and
  `range.begin`/`range.step`/`range.end` (OpenSCAD 2019.05+). Only bracket
  indexing (`v[0]`) is supported today; `IndexExpr` (Expr.h) has no dotted
  form. (`vector-swizzling` test.)
- [ ] **Calling the result of an arbitrary expression**, e.g.
  `(function(x) function(y) x+y)(2)(5)` (currying/IIFE). `FunctionCall`
  (Expr.h) is name-keyed (`std::string name`) — it can only represent
  "call the thing named `name`", not "call this expression's result". Needs
  a distinct callee-expression AST shape, not just a grammar tweak.
- [ ] **Named/positional argument interleaving order.** OpenSCAD's actual
  rule (confirmed empirically via `arg-permutations.scad`, all 119
  permutations): positional args bind to parameter slots by a plain
  left-to-right counter that does **not** skip slots already targeted by a
  named arg, and named/positional bindings apply strictly in the order
  written in the call — so `f(a=1, 2)` gives `a=2` (the trailing positional
  overwrites the earlier named value at the same slot), not `a=1, b=2` as
  ChiselCAD currently produces (named args always win over positional,
  positional args skip already-named slots). Affects `Interpreter`'s
  function/closure-call binding and `CsgEvaluator::evalModuleCall` — all
  three currently share the "named wins, positional fills the gaps" model.
- [ ] Possible UTF-8/Unicode string-handling gaps (`unicode-tests`,
  `utf8-tests`, `nbsp-latin1-test`, `string-unicode`, `search-tests-unicode`
  all mismatch) — `Value::str` is a raw `std::string`; `len()`/indexing/
  `chr()`/`ord()` likely count bytes, not codepoints, for multi-byte UTF-8.
  Not yet root-caused in detail.
- [ ] Recursion-depth differences on deliberately-deep-recursion test files
  (`recursion-test-function`, `tail-recursion-tests`,
  `issue3118-recur-limit`) — `kMaxCallDepth` (200, chosen for MSVC's 1 MiB
  default stack) is far lower than whatever depth OpenSCAD's own tests
  expect to succeed. A deliberate safety/compatibility tradeoff, not
  obviously wrong, but worth a second look.
- [ ] A longer tail of smaller mismatches not yet individually triaged:
  variable redefinition/scoping (`redefinition`, `value-reassignment-tests`,
  `variable-scope-tests`, `function-scope`, `let-module-tests`), and
  edge cases in `search()`/`norm()`/`cross()`/`concat()`/`rands()`/
  `parent_module()`. Full list of mismatching test names is reproducible via
  `tests/tools/README.md`'s corpus script.
- Cosmetic (not a value/behavior bug): ChiselCAD doesn't emit OpenSCAD's
  arity-mismatch ("`abs() number of parameters does not match`") or
  file-not-found warning text — builtins just silently return `undef` on a
  bad call, matching OpenSCAD's *value*, just not its *diagnostic wording*.
  ~36 of the corpus's ~106 raw failures are this category alone.

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
