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
- [x] **Dot-member access on vectors/ranges** (issue #79): added a `Dot` token
  (`Token.h`/`Lexer.cpp`) and a `MemberExpr` postfix AST node
  (`Expr.h`/`Parser.cpp`'s `parsePostfix`), evaluated in `Interpreter.cpp`
  for `Value::Tag::Vector` (`.x`/`.y`/`.z`/`.w`, the `.r`/`.g`/`.b`/`.a`
  aliases, and multi-letter swizzles like `.xyzw`/`.xr`/`.rrrr`, one output
  element per member letter) and `Value::Tag::Range` (`.begin`/`.step`/
  `.end` reading `rangeStart`/`rangeStep`/`rangeEnd`). Out-of-range/
  unrecognized members return `undef`, matching bracket-indexing's existing
  convention. Covered by `tests/test_interpreter.cpp`.
- [x] **Named/positional argument interleaving order** (issue #81): reworked
  `Interpreter`'s function-call and closure-call binding (`bindOrderedArgs`
  in `Interpreter.h`, shared by both) to replay arguments in their original
  call-site textual order instead of two separate named-then-positional
  passes — a positional arg now advances an unconditional left-to-right
  counter that doesn't skip slots already targeted by a named arg, and
  whichever of a named/positional binding to the same slot comes later in
  the call wins, matching OpenSCAD's actual rule (verified against all 119
  permutations in `arg-permutations.scad`). While investigating, found
  `CsgEvaluator::evalModuleCall`'s module-call binding — despite this
  issue's original suspicion that all three call sites shared the same
  buggy model — was **already** implementing the correct order-replay rule
  (it walks `call.args` in original order and binds unconditionally as it
  goes), so it was left unchanged; only the two `Interpreter` call sites
  needed the fix. Covered by `tests/test_interpreter.cpp`.

Both are covered by regression tests (`tests/test_parser.cpp`,
`tests/test_interpreter.cpp`, `tests/test_csg_evaluator.cpp`) and verified
against the full 544-test suite (built directly with system `g++`/`apt`
packages for Catch2/glm/spdlog — see `tests/tools/README.md` — since this
environment has no vcpkg/Manifold/Vulkan).

Confirmed via the same corpus run but **not yet fixed** — real gaps, ranked
roughly by expected real-world impact:

- [x] **Calling the result of an arbitrary expression** (issue #80), e.g.
  `(function(x) function(y) x+y)(2)(5)` (currying/IIFE). `FunctionCall`
  (Expr.h) is name-keyed (`std::string name`) — it could only represent
  "call the thing named `name`", not "call this expression's result".
  Added a distinct `CallExpr` node (`callee: ExprPtr` + args) produced by
  `parsePostfix` whenever `(args)` follows any primary expression (not just
  a bare identifier, which `parsePrimary` already consumes directly into a
  `FunctionCall`) — so `f(2)(5)`, `(function(x) ...)(2)(5)`, and chains of
  either now parse. Evaluated in `Interpreter::evaluate` by evaluating the
  callee to a `Value` and requiring it to already be a function (closure);
  there's no name to fall back to a builtin/`function` def by, so a
  non-function callee yields `undef` rather than erroring. Covered by
  `tests/test_parser.cpp` and `tests/test_interpreter.cpp`.
- [ ] Possible UTF-8/Unicode string-handling gaps (issue #82) (`unicode-tests`,
  `utf8-tests`, `nbsp-latin1-test`, `string-unicode`, `search-tests-unicode`
  all mismatch) — `Value::str` is a raw `std::string`; `len()`/indexing/
  `chr()`/`ord()` likely count bytes, not codepoints, for multi-byte UTF-8.
  Not yet root-caused in detail.
- [ ] Recursion-depth differences (issue #83) on deliberately-deep-recursion test files
  (`recursion-test-function`, `tail-recursion-tests`,
  `issue3118-recur-limit`) — `kMaxCallDepth` (200, chosen for MSVC's 1 MiB
  default stack) is far lower than whatever depth OpenSCAD's own tests
  expect to succeed. A deliberate safety/compatibility tradeoff, not
  obviously wrong, but worth a second look.
- [ ] A longer tail of smaller mismatches not yet individually triaged (issue #84):
  variable redefinition/scoping (`redefinition`, `value-reassignment-tests`,
  `variable-scope-tests`, `function-scope`, `let-module-tests`), and
  edge cases in `search()`/`norm()`/`cross()`/`concat()`/`rands()`/
  `parent_module()`. Full list of mismatching test names is reproducible via
  `tests/tools/README.md`'s corpus script.
- Cosmetic (not a value/behavior bug, issue #85): ChiselCAD doesn't emit OpenSCAD's
  arity-mismatch ("`abs() number of parameters does not match`") or
  file-not-found warning text — builtins just silently return `undef` on a
  bad call, matching OpenSCAD's *value*, just not its *diagnostic wording*.
  ~36 of the corpus's ~106 raw failures are this category alone.

## v3.9 — Geometry corpus validation (volumetric, tessellation-agnostic)

v3.8's corpus run only covered `echo()`-based tests — computed *values*,
never actual mesh output — because ChiselCAD (Manifold) and OpenSCAD (CGAL,
or its own separate Manifold integration) tessellate primitives completely
differently: different vertex counts, ordering, and coordinates even for a
byte-perfect-correct geometry engine. A raw point/vertex diff between their
STL exports would "fail" even when the underlying solid is identical, so it
was never a viable comparison — until now.

The fix is a tessellation-agnostic check: build the *real* C++ Manifold
library (not available via `apt`, but builds standalone from source in ~20s
with zero extra dependencies once pinned to v3.5.2 — the version this
environment builds against, since Manifold's `master` branch has since
dropped `CrossSection::FillRule`, which ChiselCAD's `PrimitiveGen.cpp`/
`MeshEvaluator.cpp` still use), export the same `.scad` file to STL from
both OpenSCAD and ChiselCAD's *actual compiled mesh pipeline*
(`CsgEvaluator` → `MeshEvaluator` → `PrimitiveGen`, exactly as
`MeshBuilder::buildOne()` runs it — not a reimplementation), reload both as
Manifolds, and measure the volume of their **symmetric difference**
((A−B)∪(B−A)). Two meshes representing the same solid have a symmetric
difference volume of ~0 regardless of how differently each one discretized
it; a real shape difference shows up as a non-trivial fraction of the
model's own volume. Two new standalone tools do this (see
`tests/tools/README.md` for the exact build/run steps):

- `scad_to_stl.cpp` — runs ChiselCAD's real mesh pipeline on a `.scad` file
  and writes the (BatchBoolean-unioned) result as ASCII STL.
- `stl_diff.cpp` — loads two ASCII STL files as Manifolds (via
  `MeshGL::Merge()` to weld the triangle-soup positions STL always exports),
  and reports `volume_a`, `volume_b`, `sym_diff_volume`, and their ratio.

Run against `tests/data/scad/3D/features/*.scad` (OpenSCAD's own 3D-feature
test corpus), this immediately found a real, high-confidence, now-fixed bug:

- [x] **`polyhedron()` faces came out with exactly negated volume** — every
  `polyhedron-*` test file showed `volume_b == -volume_a` to the last digit.
  Root cause: OpenSCAD's documented `polyhedron()` convention lists each
  face's vertices **clockwise as seen from outside the solid**; Manifold
  (like most engines) expects **counter-clockwise from outside**.
  `CsgEvaluator::evalPolyhedron()`'s fan-triangulation preserved the input
  winding verbatim instead of flipping it. Fixed by swapping the last two
  indices of each fan triangle; verified against real OpenSCAD on
  `polyhedron-cube`/`polyhedron-soup`/`polyhedron-concave-test` (now exact,
  `sym_diff_volume == 0`) with a regression test on the raw triangle indices
  (`tests/test_csg_evaluator.cpp`).

A second pass fixed three more real bugs, all found by bisecting
`sphere-tests`/`cylinder-diameter-tests` line by line against real
OpenSCAD once the test infrastructure improved (main's `chiselcad_core`
split — merged in from `main` mid-investigation — made it possible to
build ChiselCAD's real `PrimitiveGen`/`MeshEvaluator` against an installed
Manifold via plain `cmake -DCHISELCAD_BUILD_GUI=OFF`, instead of the
original ad hoc `g++` object-file dance):

- [x] **`r`/`d` precedence**: when both a radius and a diameter argument are
  given (`sphere(r=1, d=10)`), OpenSCAD always uses `d`, order-independent
  (confirmed empirically: `sphere(d=10, r=1)` gives the same result) — not
  "`d` only when `r` is absent", which is what `CsgEvaluator` did for
  `sphere`/`cylinder`(`r`/`r1`/`r2`)/`circle`. Fixed in all five call sites;
  regression test added on the resolved `CsgLeaf::params`.
- [x] **Per-node `$fa`/`$fs` overrides were silently ignored.**
  `PrimitiveGen::resolveSegments()` only accepted a per-node `$fn` override;
  `$fa`/`$fs` overrides fell through to the *global* values regardless of
  what the call site actually specified, so `sphere(5, $fa=40, $fs=0.3)`
  tessellated at the global default resolution instead of the coarser one
  it explicitly asked for. This alone accounted for most of
  `sphere-tests`'s remaining error once winding/`r`-`d` were fixed. Fixed
  by threading `$fa`/`$fs` through the same way `$fn` already was;
  regression test (`runBuild` on two fixtures with the same radius but
  different `$fa`/`$fs`, checking triangle counts diverge) added to
  `tests/test_headless_build.cpp` — the first geometry-level (real
  Manifold mesh) unit test this codebase has had, made possible by the
  same `chiselcad_core` split.
- [x] **`cylinder(r1=..., r2` unset`)` wrongly defaulted `r2` to `r1`'s
  value** (a uniform cylinder) instead of independently defaulting to
  `1.0` (a tapered cone down to radius 1) — confirmed against real
  OpenSCAD's actual STL output. Fixed; regression test checks the exact
  frustum volume formula.

Together these fully resolved `sphere-tests` and `cylinder-diameter-tests`
(both now `sym_diff_volume` at floating-point noise level, down from 11%
and 55% relative error respectively) and incidentally unblocked several
other files that had been failing for an unrelated **test-tool** reason:
`scad_to_stl`'s `BatchBoolean(Add)` across *all* top-level roots at once
turned out to fail outright (empty combined result) the moment *any* one
root was degenerate/invalid (e.g. `cylinder(r1=0, r2=0)` in
`cube-tests.scad`/`cylinder-tests.scad`) — confirmed via `chiselcad_cli
--stats -` on the same file, which meshes each root independently and
correctly kept the valid geometry while just flagging the bad root as an
error diagnostic. Fixed `scad_to_stl` to drop non-`NoError`-status roots
before unioning, matching that real per-root behavior. This was a harness
bug, not a ChiselCAD product bug, but it had been hiding real signal:
`cube-tests`, `hull3-tests`, and `2d-3d` all turned out to already match
exactly once unblocked.

Confirmed via the same corpus subdirectory (`3D/features`) but **not yet
fixed**:

- [x] **Non-planar polyhedron faces** (issue #86) (`polyhedron-nonplanar-tests`) —
  turned out **not** to be a fan-triangulation bug at all; see v3.11 below.
- [x] **`linear_extrude()`'s default height was 1, not OpenSCAD's actual
  default of 100.** Found bisecting `linear_extrude-tests.scad` line by
  line: `linear_extrude(v=[3,2,5]) square([10,10])` (no `height=` given)
  gives volume 10000 in real OpenSCAD — a 10×10 profile times height
  100 — not 100 (height 1). Fixed in `MeshEvaluator::evalExtrusion()`.
- [x] **A malformed `scale=` vector (anything but exactly 2 elements) was
  partially applied instead of rejected.** `linear_extrude(height=20,
  scale=[4,5,6]) square(10)` — 3 elements, not the 2 (`[sx,sy]`) OpenSCAD's
  `scale=` accepts — comes out as a plain *unscaled* 10×10×20 prism (volume
  2000) in real OpenSCAD, i.e. the whole malformed vector is ignored;
  `CsgEvaluator` was instead taking just the first two components (`sx=4,
  sy=5`), giving a wrongly-scaled volume of 17000. Fixed to require exactly
  2 elements. Both fixes together brought `linear_extrude-tests.scad` from
  102% relative error down to 13.6%; regression tests added (CsgEvaluator-
  level for the scale rejection, `runBuild`-level for the default height).
- [ ] `linear_extrude-tests.scad`'s remaining ~13.6% error (issue #91), and the other
  three `linear_extrude-*` files, are not yet further root-caused —
  **and a real methodological wrinkle turned up while bisecting**: the
  installed oracle is OpenSCAD 2021.01 (`apt`'s only option here), but the
  test corpus is cloned from `openscad/openscad`'s current `master` branch.
  `linear_extrude(h=10, ...)` (the corpus file's own comment: "`h` is an
  alias for `height`") triggers `WARNING: variable h not specified as
  parameter` on the installed 2021.01 oracle — i.e. **2021.01 itself
  doesn't recognize `h=` for `linear_extrude`**, while ChiselCAD does.
  Whether that's a genuine ChiselCAD-ahead-of-2021.01 case (`h=` added in a
  later real OpenSCAD release the corpus already assumes) or an actual
  ChiselCAD-only invention needs checking against a newer real OpenSCAD
  build (or the current online manual) before touching it either way —
  don't "fix" this by matching the older oracle without that check first.
  The same applies to `segments=` (also unrecognized by 2021.01). This
  caveat likely explains some fraction of the other still-open mismatches
  below too, not just `linear_extrude`.
- [ ] **`rotate_extrude` volume mismatches** (issue #87): `rotate_extrude-tests` (27%),
  `rotate_extrude-angle` (71%) — contrast with `rotate_extrude-touch-vertex`/
  `rotate_extrude-touch-edge`, which pass at floating-point-noise level, so
  this is parameter-specific (likely the `angle=` partial-revolution case
  given `-angle` is the worse of the two) rather than a blanket
  `rotate_extrude` bug.
- [ ] (issue #88) `intersection-tests` (6.4%), `cylinder-tests` (13%, improved from
  totally-blocked but still a real remaining gap after the harness fix
  above), `primitive-inf-tests` (83%), `ifelse-tests` (175%),
  `module-recursion`, `resize-tests` (1.5%), `surface-simple` — real
  mismatches, not yet individually triaged.
- [ ] `assign-tests` and `intersection_for-tests` (issue #89) still produce zero valid
  geometry even after the harness fix — unlike the files that fix unblocked,
  these appear to genuinely fail in `MeshEvaluator`/`PrimitiveGen` itself
  (every root invalid, not just one), not just in the test tool. Needs the
  same per-root `chiselcad_cli --stats` triage the harness bug above got.
- [ ] Several other files (issue #90) (`polyhedron-tests`, `minkowski3-difference-test`,
  `scale3D-tests`, `for-nested-tests`, `render-tests`, `mirror-tests`,
  `for-tests`, `edge-cases`, `rotate-parameters`,
  `scale-mirror2D-3D-tests`, `transform-tests`) fail to parse at all under
  ChiselCAD — mostly `for`/transform-argument grammar edge cases not yet
  individually triaged (some may double-count v3.8 gaps already listed
  above, e.g. multi-variable `for`).

Only one of the corpus's several subdirectories (`3D/features`) has been
run through this so far — `2D`, `bugs`, `bugs2D`, `misc`, `issues` are
still unexamined. This should become a standing regression suite (rerun
after any geometry-affecting change), not a one-time audit.

## v3.10 — All 11 issue #90 parse failures fixed; issue #91's oracle-version question resolved

Environment note: this pass had no access to a live OpenSCAD oracle or a
buildable Manifold (network egress in this session is scoped to
`particlesector/chiselcad` only, so `vcpkg`/other-repo downloads used by the
usual build are blocked). All of the parser/grammar fixes below were instead
verified for real: the language+CSG subsystem (`src/lang`, `src/csg`, and
the GPU/Manifold-free import loaders — the same set `tests/tools/README.md`
already documents as buildable with plain `g++ -lz`, no vcpkg needed) was
compiled and linked against real `glm`/`nlohmann-json`/`zlib`/a
from-source-built Catch2, and the *actual* `tests/test_parser.cpp` /
`tests/test_csg_evaluator.cpp` suites were run — 344 test cases / 1758
assertions, all passing, including new regression tests for every fix below.
Each of issue #90's 11 originally-listed files was also fed through the real
`Lexer`/`Parser`/`CsgEvaluator` directly (fetched fresh from
`openscad/openscad`'s current corpus) and confirmed to both parse and
fully evaluate without error. What's still unverified against a live
OpenSCAD binary is exact geometric/volumetric correctness (the v3.9-style
`sym_diff_volume` comparison) — that needs the oracle this environment
doesn't have.

- [x] **Resolved the v3.9 oracle-version question for `linear_extrude(h=...)`
  and `segments=`** (issue #91). Checked real OpenSCAD's current `master`
  source (`src/core/LinearExtrudeNode.cc`) directly rather than the outdated
  `apt` 2021.01 binary: `parameters[{"height", "h"}]` confirms `h` genuinely
  is a real, current alias for `height` — ChiselCAD's support for it is
  correct, not a ChiselCAD invention, and not something to "fix" back to
  2021.01 behavior. `segments=` is also a genuine, distinct current
  parameter (`node->segments`, validated separately from `slices`) — but
  unlike `h=`, ChiselCAD doesn't implement it **at all** (no `segments`
  param is read anywhere in `MeshEvaluator::evalExtrusion`). Per
  `src/geometry/linear_extrude.cc`, real OpenSCAD only acts on it when a
  twist or non-uniform scale is active: it subdivides each outline edge
  (`Discretizer::splitOutline(outline, twist, scale_x, scale_y, num_slices,
  segments)`) before extruding, so a twisted/scaled `linear_extrude` gets a
  smoother swept surface instead of faceting along the profile's original
  edges. This is a real, previously-unidentified gap and likely explains
  some fraction of `linear_extrude-tests.scad`'s remaining ~13.6% error —
  filed as a follow-up rather than implemented blind here (see
  `tests/tools/README.md`), since matching `splitOutline`'s exact per-edge
  subdivision behavior needs bisecting against a live modern OpenSCAD build,
  not a guess with no way to check the result.
- [x] **`for()` with a completely empty argument list failed to parse**
  (`for-tests.scad`, issue #90 — opens with exactly this on line 2:
  `for();`). `Parser::parseFor()` unconditionally expected an `Ident '='`
  right after `(`. Real OpenSCAD's `for()` argument list is a generic
  module-call argument list, legitimately allowed to be empty. Fixed by
  making `ForNode` hold a `std::vector<ForClause>` (see next item) that's
  simply empty in this case; `CsgEvaluator::evalFor()` treats an empty
  clause list as zero iterations, matching real OpenSCAD's `builtin_for()`
  (`src/core/control.cc`'s `if (!inst->arguments.empty())` guard) — it skips
  the body entirely rather than running it once vacuously.
- [x] **Multi-variable `for (i=..., j=..., k=...)` wasn't supported at all**
  (`for-nested-tests.scad`, `mirror-tests.scad`, `edge-cases.scad` — 3 of
  issue #90's 11 files). `ForNode` only ever held one `var`/`range` pair, so
  `Parser::parseFor` parsed the first clause then unconditionally expected
  `)`, erroring on the comma before a second clause. Restructured `ForNode`
  to hold a list of `ForClause`s; `CsgEvaluator::evalFor` now recurses one
  clause at a time as nested loops (Cartesian product, outermost = first
  clause), evaluating each clause's range expression only once its own turn
  comes up — so a later clause can reference an earlier one's already-bound
  variable (`for (i=[0:3], j=[0:i])`), matching real OpenSCAD.
- [x] **`rotate(a, v)` — the angle+arbitrary-axis form — wasn't supported at
  all**, and neither were `rotate()`/`mirror()` with zero arguments
  (`rotate-parameters.scad`, `transform-tests.scad`,
  `scale-mirror2D-3D-tests.scad` — 3 of issue #90's 11 files).
  `Parser::parseTransform` called `parseExpr()` exactly once for a single
  required argument, for all of translate/rotate/scale/mirror/multmatrix —
  so `rotate(30, [0,1,0])` (two positional args), `rotate(v=..., a=...)`
  (named), and a bare `rotate()`/`mirror()` (zero args) all failed to parse.
  Added a dedicated argument-list parser for `rotate` (positional/named `a`
  and `v`, matching real OpenSCAD's `Parameters::parse(arguments, {"a",
  "v"})`) and made the single-positional-argument case optional for the
  rest. `CsgEvaluator::makeMatrix` now implements the angle+axis rotation
  itself via `glm::rotate(angle, axis)` (falling back to the Z axis when `v`
  isn't given — mathematically identical to the old hard-coded Z-only
  scalar case, so existing scalar `rotate(angle)` callers are unaffected),
  and gives each transform its own OpenSCAD-documented default for a
  missing/non-numeric argument (confirmed against real OpenSCAD's
  `TransformNode.cc`: translate → `[0,0,0]`, scale → `[1,1,1]`, mirror →
  `[1,0,0]`) instead of the one-size-fits-all `{0,0,0}` (or, for a scalar,
  `{0,0,n}`) every transform previously shared.
- [x] **`scale(2)` (a bare scalar) scaled only the Z axis by 2, leaving X/Y
  untouched, instead of scaling uniformly** — a real, silent correctness bug
  found while fixing the above (the same shared "scalar → Z axis" decoding
  rule is correct for `rotate(angle)` but was wrong for `scale`/`translate`/
  `mirror`, which never had their own scalar handling). Confirmed against
  real OpenSCAD's `builtin_scale()`: a non-vector numeric argument falls
  back to `scalevec.setConstant(num)` — uniform. Fixed for `Scale`
  specifically (`transform-tests.scad`'s `scale(0.5) mycyl()` and
  `scale3D-tests.scad`'s `scale(2) obj3D()` both exercise this).
- [x] **`union`/`difference`/`intersection`/`hull`/`minkowski` couldn't take
  any arguments at all**, even `minkowski(convexity=2)`, a real OpenSCAD
  parameter (`minkowski3-difference-test.scad`, issue #90).
  `Parser::parseBoolean` called `expect(RParen)` immediately after `(` with
  no attempt to parse anything in between. Now parses (and discards) a
  generic param list first, same treatment `render()`'s `convexity=` already
  got.
- [x] **Module definitions required a brace-block body — a single-statement
  body with no braces (e.g. `module obj3D() cylinder(r=1, ...);`, an
  extremely common one-liner idiom) failed to parse** (`scale3D-tests.scad`,
  `scale-mirror2D-3D-tests.scad`, issue #90). Every *other* body (for/if/
  transform/boolean/...) already accepted both forms via the shared
  `parseBody()` helper; module definitions had their own hard-coded
  `expect(LBrace)` instead. Now uses `parseBody()` like everything else.
- [x] **A leaf primitive (cube/sphere/.../polygon) followed by a trailing
  child statement failed to parse** (`scale-mirror2D-3D-tests.scad`'s
  `module obj2D() polygon(...) square(...);` — `square()` is bound as
  `polygon()`'s syntactic child, which OpenSCAD's grammar allows for *any*
  module instantiation even though a leaf primitive's semantics ignore it).
  `Parser::parsePrimitive` never called `parseBody()` at all. Now does, and
  discards the result — matching how `union()`/`translate()`/etc. already
  tolerate a bare `;` with no child via the very same call.
- [x] **`polygon()`'s positional (unnamed) points list was fundamentally
  broken for any point count** — found while root-causing the item above,
  not itself one of the original 11 files, but the same corpus file
  (`scale-mirror2D-3D-tests.scad`) also exercises it, and it's a
  significant, previously-unnoticed correctness bug independent of issue
  #90. `parseParamList`'s generic "positional vector" case assumes any
  bracketed positional argument is an `[x,y,z]`-shaped triple and
  decomposes it into `"x"`/`"y"`/`"z"` keys, capped at 3 elements. For
  `polygon([[x,y],...])` this is wrong in two different ways depending on
  point count: **exactly 3 points** parsed without error but landed under
  `"x"`/`"y"`/`"z"` instead of `"points"` — the key `CsgEvaluator`'s
  polygon handling actually reads — so the polygon silently got *zero*
  points; **4 or more points** (the overwhelmingly common real case) failed
  to parse outright, since the loop's hard-coded 3-element cap left a
  dangling `,` where `)`'s matching `]` was expected. Added a dedicated
  `Parser::parsePolygonParams` (first positional → `points`, second →
  `paths`, matching real OpenSCAD's declared `polygon(points, paths,
  convexity)` order; third positional/`convexity` parsed and discarded).
  Every unnamed `polygon([...])` call in the wild — likely most real-world
  `.scad` files that use `polygon()` at all — was affected by one of these
  two failure modes before this fix.

All of the above have regression tests in `tests/test_parser.cpp` and/or
`tests/test_csg_evaluator.cpp` (tagged `[bugfix]`), verified passing against
the real compiled `Lexer`/`Parser`/`Interpreter`/`CsgEvaluator` (see the
environment note above) — not just read for syntactic plausibility.

Issue #90's remaining scope: only the `3D/features` corpus subdirectory has
been checked (matching v3.9's existing note); `2D`, `bugs`, `bugs2D`,
`misc`, `issues` are still unexamined and may turn up further parse
failures of their own.

## v3.11 — issue #86 (non-planar polyhedron() faces) re-investigated and closed

This environment turned out to have real `apt` network access after all
(unlike the v3.10 pass): `apt-get install openscad` gives a live 2021.01
oracle, and Manifold v3.5.2 builds from source in ~20s exactly as
`tests/tools/README.md` describes, so this pass could go back to the actual
volumetric `scad_to_stl`/`stl_diff` methodology instead of language-level
checks only.

- [x] **Issue #86 was misdiagnosed — not a fan-triangulation bug.**
  `polyhedron-nonplanar-tests.scad`'s reported `2.94` (real OpenSCAD) vs.
  `1.29` (ChiselCAD) mismatch was bisected by isolating each of the file's
  three `polyhedron()` calls into its own `.scad` file and diffing each
  individually against a live OpenSCAD 2021.01 + Manifold v3.5.2 oracle.
  Two of the three (the slightly-non-planar hexahedron and the small
  heptagon-pyramid) already matched to floating-point noise (`rel_error` ~
  1e-7) — meaning the fan-triangulation-from-vertex-0 approach
  `CsgEvaluator::evalPolyhedron` uses was never actually wrong for this
  corpus file. The third — a real-world truncated icosidodecahedron wrapped
  in `scale(0.02) polyhedron(...)` — reproduced the mismatch in isolation
  (`volume_a=1.65443 volume_b=0`, confirmed by rebuilding at the exact
  commit (`696522c`) the issue was filed against). The actual cause: `scale(
  0.02)` is a **bare scalar**, and at that commit `CsgEvaluator::makeMatrix`
  still had the pre-v3.10 `scale(<scalar>)` bug (see v3.10's `for`/`rotate`/
  `scale` fixes) that scaled only the Z axis — turning this polyhedron into
  a wildly non-uniform, self-intersecting mesh that Manifold's `Status()`
  still reported as `NoError` but whose true volume collapsed to ~0 after
  boolean cleanup. That bug was already fixed in commit `18f7a54` (filed
  under issue #90, not #86) as an incidental side effect of unrelated
  parser work — nobody had re-run the v3.9 volumetric corpus check
  afterward to notice it also closed #86. Re-running it now:
  `polyhedron-nonplanar-tests.scad`'s full file matches the oracle at
  `rel_error=3.5e-6`, and the previously-broken `scale(0.02)` case alone
  matches at `rel_error=1.4e-6` — both floating-point noise, no code change
  needed to `CsgEvaluator::evalPolyhedron`/`PrimitiveGen` itself. Added
  `tests/fixtures/headless/polyhedron_nonplanar_scale.scad` (the exact
  reproducing case, straight from the OpenSCAD corpus) and a
  `chiselcad_tests` regression pinning its volume to the oracle's
  `1.6544281372`, so this doesn't silently regress independently of the
  `scale(scalar)` unit test that already covers the actual root cause.
- Takeaway for future corpus-mismatch triage: a mismatch reported against
  a file with multiple unrelated constructs (here, both a genuinely
  non-planar `polyhedron()` *and* a bare-scalar `scale()`) doesn't
  necessarily implicate the construct the file is named after — isolating
  each construct individually (as this pass and the original v3.9 harness-
  bug triage both did) is worth doing before writing a fix for the wrong
  code path.

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
