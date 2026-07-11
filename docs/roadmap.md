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

### Tier C — Module System Completeness ✓
- [x] `children()` / `$children`
- [x] `echo()`
- [x] `assert()`
- [x] Recursive functions (enabled by user-defined functions)

### Tier D — Geometry Operations ✓
- [x] `multmatrix()` — extends transform accumulation (mat4) already used by translate/rotate/scale/mirror
- [x] `render()` — pass-through node; ChiselCAD already fully evaluates, no preview/full-render split
- [x] `color()` — new inherited `ColorAttr` alongside `transform`; tints whole roots (Manifold booleans merge
      a subtree's children into one mesh with no surviving per-part identity, so color() is a per-root
      attribute, not per-leaf) — result-mode shading now reads a per-vertex base color instead of a shader constant
- [x] `offset()` — new `CsgOffset` IR node, evaluated via `CrossSection::Offset()`; `r=` rounds corners,
      `delta=` gives straight corners (mitered, or beveled with `chamfer=true`); local-space children +
      outer transform, same non-equivariance treatment as hull()/minkowski()
- [x] `projection()` — new `CsgProjection` IR node; 3-D children are unioned into one Manifold in local
      space, then flattened via `Manifold::Project()` (full silhouette, the default) or `Manifold::Slice(0)`
      (`cut=true`, a true z=0 cross-section), converted back to a `CrossSection` the same way `Polygon2D`
      already is — same local-space-children + outer-transform treatment as offset()/extrusion

### Tier E — File I/O (complex)
- [x] `include <>` / `use <>` — new `SourceLoader` (`src/lang/SourceLoader.h/.cpp`) recursively
      lexes/parses referenced files and splices them into the root `ParseResult`; `include`
      merges roots/assignments/moduleDefs/functionDefs (textual-paste semantics), `use` merges
      only moduleDefs/functionDefs. Paths resolve relative to the referencing file's directory;
      circular includes are diagnosed instead of hanging; `MeshBuilder` now goes through
      `loadSource()` instead of driving `Lexer`/`Parser` directly.
- [x] `import()` — STL only for now (OFF/3MF/AMF/DXF/SVG deferred as follow-ups). New
      `CsgLeaf::Kind::Mesh` carries raw triangle-soup data (`meshPositions`/`meshIndices`),
      resolved eagerly by `CsgEvaluator` (not deferred to `PrimitiveGen`) so a missing file or
      unreadable STL surfaces as a `Diagnostic` instead of silently producing empty geometry;
      `PrimitiveGen` just hands the data to `Manifold`'s `MeshGL` constructor. STL parsing was
      split out of `src/import/StlImporter` into a new render-independent `StlLoader` (no
      Vulkan/VMA deps) so `CsgEvaluator.cpp` — shared with the lightweight `chiselcad_tests`
      target — doesn't pull in GPU headers; `StlImporter` is now a thin `render::Vertex` wrapper
      around it, unchanged for its existing standalone-STL-viewer caller. Relative `import()`
      paths resolve against the root `.scad` file's directory (`CsgEvaluator::baseDir`, set by
      `MeshBuilder`) — same known caveat as `assert()`/`echo()` diagnostics not carrying a
      file path for code reached via `use`/`include` (AST has no per-node file identity yet).
      Also fixed a latent `MeshCache` key collision: `polygon()`/imported-mesh geometry data
      wasn't hashed into the cache key at all, so two different shapes at the same transform
      could silently swap cached meshes.
- [x] `surface()` — text `.dat` heightmap format only (PNG heightmaps deferred as a follow-up).
      New `src/import/SurfaceLoader.h/.cpp` parses the grid (`#` comments, blank lines skipped,
      every row must have the same column count) and triangulates it directly into a closed,
      watertight solid — top surface follows the height values, vertical walls, flat base —
      reusing the same `CsgLeaf::Kind::Mesh` `import()` introduced, so no `CsgNode`/
      `PrimitiveGen`/`MeshEvaluator` changes were needed. Grid row 0 maps to the *far* (max Y)
      edge, matching OpenSCAD's row convention; `center` centers the X/Y footprint *and* Z
      extent; `invert` flips heights about the grid's own max (`h' = max - h`). The solid's base
      sits at `min(0, minHeight)` rather than a hardcoded `z=0`, so heightmaps with negative
      values don't fold the base back through the top surface. Same `baseDir`-relative-path and
      `assert()`/`echo()`-filePath caveats as `import()` apply (see above).
- [x] `text()` — glyph outlines only via vendored `stb_truetype` (`third_party/stb/stb_truetype.h`,
      public domain); no FreeType/HarfBuzz. New `src/import/StbFontBackend.h/.cpp` wraps every
      `stbtt_*` call used (font load, cmap lookup, glyph outline extraction with quadratic/cubic
      curve flattening, advance width, legacy `kern`-table kerning) and `src/import/
      NaiveLtrShaper.h/.cpp` does simple left-to-right layout (UTF-8 decode, cmap lookup per
      codepoint, kerning, advance accumulation) — deliberately split into two small files along
      the exact seam a future FreeType+HarfBuzz backend would replace (glyph outline/metric
      extraction vs. text shaping), so a contributor adding that later swaps these two files for
      `FreeTypeFontBackend`/`HarfBuzzShaper` with the same function shapes, without touching
      `TextLoader.h`'s signature or `CsgEvaluator` at all. Deliberately *not* a virtual-interface
      hierarchy — this codebase prefers concrete types/`std::visit` over virtual dispatch
      (CONTRIBUTING.md), so the seam is the header boundary, not an abstract base class.
      `src/import/TextLoader.h/.cpp` orchestrates the two into a flat list of 2-D contours
      (`RawTextOutline`), which `CsgEvaluator::evalText` turns directly into a `Polygon2D` leaf —
      no new `CsgNode.h`/`PrimitiveGen.cpp` needed, since `Polygon2D`'s existing multi-path +
      `EvenOdd` fill (`PrimitiveGen.cpp`) already turns a glyph's nested contours (e.g. the counter
      of an "O") into holes for free, the same way `polygon()` handles holes.
      Known scope cuts, matching this project's convention of shipping the common case first
      (see `import()`/`surface()` above):
        - No ligatures, contextual substitution, bidi, or complex-script reordering — codepoints
          map 1:1 to glyphs in source order. Fine for static Latin/Western text (labels, part
          numbers, engraved text), not for Arabic/Hebrew/Indic scripts.
        - Kerning only via the legacy `kern` table (stb_truetype doesn't read `GPOS`, which is
          how most modern fonts actually ship kerning) — kerning silently no-ops on many fonts.
        - `direction`/`language`/`script` (OpenSCAD's bidi/shaping controls) are accepted and
          discarded, same treatment as `surface()`'s `convexity`.
        - Single line only, matching OpenSCAD — embedded newlines aren't treated specially;
          multi-line text is multiple `text()` calls composed with `translate()`.
        - No system font-name lookup (no fontconfig) — `font=` is always a file path, resolved
          exactly like `import()`/`surface()` (relative to `baseDir`, or absolute). Omitting
          `font=` falls back to a bundled default (Roboto Regular, Apache 2.0,
          `resources/fonts/Roboto-Regular.ttf` + `resources/fonts/Roboto-LICENSE.txt`), resolved
          via a new `CHISELCAD_RESOURCE_DIR` compile definition (mirrors `CHISELCAD_SHADER_DIR`;
          same known limitation of embedding a source-tree-absolute path rather than being
          properly relocatable post-install — `install(DIRECTORY resources/ ...)` was added but
          the compiled-in path isn't install-location-aware, matching the shaders' existing
          behavior).
        - Curve tessellation: adaptive recursive subdivision (tolerance proportional to the
          font's own em size) by default, so smoothness doesn't depend on glyph size; an optional
          `$fn` fixes the segment count per curve instead — not a literal port of
          `PrimitiveGen::resolveSegments()`'s circle-segment formula, since a glyph curve is
          typically a much shorter arc than a full circle.

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
