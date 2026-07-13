# ChiselCAD — Architecture

**Stack:** C++20 · Vulkan · ImGui · CMake + vcpkg
**Platforms:** Windows, Linux (macOS later)
**Boolean Backend:** Manifold
**Async Strategy:** background worker thread runs Parse→Evaluate→Mesh→Convert
off the main/render thread; the UI never blocks on evaluation (see §7)

> **Note on this document:** this file previously drifted out of sync with
> the implementation (stale AST/CSG-tree examples, a cancellation model that
> was never actually built, dependencies and editor features that were never
> added). It was corrected against the current source in July 2026. If you
> find another mismatch, treat the code as ground truth and please fix this
> file to match it.

---

## 1. Design Goals

1. **Extreme performance** — boolean evaluation should never block the UI. Complex models that take minutes in OpenSCAD/CGAL should take seconds.
2. **Extreme precision** — double precision throughout the evaluation pipeline; float only at GPU upload.
3. **Visual quality** — Vulkan shading that looks like real objects, not 2010-era CAD wireframes. Currently multi-light Blinn-Phong (§10); a PBR material model and SSAO are roadmap items (v4), not yet implemented.
4. **Developer experience** — VS Code as the primary editor via file watch + jump-to-diagnostic today (§11); a real LSP extension and AI assistance panel are both planned (roadmap v4), not yet built.
5. **OpenSCAD compatibility** — `.scad` files targeting the supported language subset should produce identical geometry. An ongoing correctness audit (GitHub Issues) is closing gaps between that goal and the current implementation.

---

## 2. High-Level Pipeline

```
[Source File on disk]
        │ (file-watcher change event)
        ▼
[MeshBuilder::requestBuild — bumps generation counter]
        │
        ▼
[Lexer → Token Stream]
        │
        ▼
[Recursive Descent Parser]  ──► [Diagnostics]
        │
        ▼
[Interpreter + CSG Tree Evaluator (AST → CsgScene)]
        │
        ▼
[MeshEvaluator: CSG tree → Manifold booleans, per root]
        │             (superseded generations are discarded, not
        │              cooperatively cancelled mid-operation)
        ▼
[Vertex/Index Buffer Conversion (BuildPhase::Converting)]
        │
        ▼
[GPU Upload → Result Render]
        │
        └──► [STL Exporter]
```

This all runs on a single background `std::thread` owned by `MeshBuilder`
(see §7) — not a thread pool, and not `std::stop_token`-based cooperative
cancellation. There is currently no separate low-res preview-tessellation
phase distinct from the full evaluation; `BuildPhase` (Idle → Parsing →
Evaluating → Meshing → Converting → Done/Error) tracks progress through one
pipeline, and the main thread polls for a finished result each frame.

---

## 3. Repository Structure

```
chiselcad/
├── CMakeLists.txt
├── vcpkg.json
├── vcpkg-configuration.json
├── .clang-format
├── docs/
│   ├── architecture.md        ← this file
│   └── roadmap.md
├── resources/
│   └── fonts/                 # Bundled default font for text() (Roboto)
├── third_party/
│   └── stb/                   # Vendored stb_truetype.h + stb_image.h (public domain)
│
└── src/
    ├── main.cpp
    ├── app/
    │   ├── Application.h/cpp  # Main loop, init/shutdown, ImGui layout
    │   ├── AppState.h         # Global atomic state flags
    │   ├── Config.h/cpp       # User settings (JSON-backed)
    │   └── MeshBuilder.h/cpp  # Background parse→CSG→mesh pipeline (§7)
    ├── editor/
    │   ├── EditorPanel.h      # Reserved placeholder — embedded editor not
    │   │                      # yet implemented; see §11
    │   ├── FileWatcher.h + FileWatcherWin32.cpp/FileWatcherInotify.cpp
    │   ├── ExternalEditor.h/cpp # "Open in VS Code" + file sync
    │   └── DiagnosticsPanel.h/cpp
    ├── lang/
    │   ├── Token.h            # Token kinds + SourceLoc
    │   ├── Lexer.h/cpp
    │   ├── AST.h              # std::variant-based AST nodes (§5.2)
    │   ├── Expr.h              # Expression sub-variant (literals, ops, calls)
    │   ├── Value.h             # Runtime value type used by the interpreter
    │   ├── Parser.h/cpp       # Recursive descent, error recovery
    │   ├── Interpreter.h/cpp  # Expression evaluation, built-ins, variable env
    │   ├── SourceLoader.h/cpp # include/use resolution, circular-include detection
    │   └── Diagnostic.h
    ├── csg/
    │   ├── CsgNode.h          # CSG tree IR (separate from AST, §6)
    │   ├── CsgEvaluator.h/cpp # AST → CSG tree (modules, functions, children(), control flow)
    │   ├── PrimitiveGen.h/cpp # Primitive/extrusion tessellation
    │   ├── MeshEvaluator.h/cpp # CSG tree → Manifold → mesh
    │   └── MeshCache.h/cpp    # LRU mesh cache, keyed by formatted params/transform
    ├── import/
    │   ├── StlLoader.h/cpp    # Render-independent binary/ASCII STL parsing
    │   ├── StlImporter.h/cpp  # render::Vertex wrapper around StlLoader
    │   ├── OffLoader.h/cpp    # import() OFF mesh parsing
    │   ├── DxfLoader.h/cpp    # import() DXF closed-entity 2-D outline parsing
    │   ├── SvgLoader.h/cpp    # import() SVG closed-shape 2-D outline parsing
    │   ├── MiniXml.h/cpp      # Minimal flat XML tokenizer shared by 3MF/AMF
    │   ├── ZipReader.h/cpp    # Minimal ZIP (stored/deflate) entry extraction, for 3MF
    │   ├── ThreeMfLoader.h/cpp # import() 3MF mesh parsing (ZipReader + MiniXml)
    │   ├── AmfLoader.h/cpp    # import() AMF mesh parsing (MiniXml)
    │   ├── SurfaceLoader.h/cpp # surface() .dat/.png heightmap parsing
    │   ├── StbFontBackend.h/cpp # stb_truetype glyph outline/metric extraction
    │   ├── NaiveLtrShaper.h/cpp # Simple left-to-right text layout
    │   └── TextLoader.h/cpp   # Orchestrates the two into 2-D contours for text()
    ├── render/
    │   ├── VulkanContext.h/cpp
    │   ├── VmaAllocator.h/cpp
    │   ├── Swapchain.h/cpp
    │   ├── RenderGraph.h/cpp  # Currently a thin placeholder, not a full pass-graph
    │   ├── GpuMesh.h/cpp      # Vertex/index buffers (single pair, wait-idle on upload — §10)
    │   ├── Pipeline.h/cpp     # Solid / Wireframe / SolidEdges pipelines
    │   ├── Camera.h/cpp       # Arcball orbit
    │   ├── Renderer.h/cpp
    │   └── shaders/
    │       ├── mesh.vert/frag       # Multi-light Blinn-Phong + rim + ACES tonemap
    │       ├── background.vert/frag # Procedural background (bgPipeline)
    │       ├── grid.vert/frag       # (compiled but currently unused — no grid pipeline exists)
    │       └── preview.vert/frag    # (compiled but currently unused by the app layer)
    ├── export/
    │   └── StlExporter.h/cpp
    └── util/
        ├── ThreadPool.h       # Unimplemented stub — not used by MeshBuilder
        ├── Debouncer.h        # Unimplemented stub — not used today
        ├── RingBuffer.h       # Lock-free SPSC — defined but currently unused (§7.3)
        ├── PathUtf8.h         # UTF-8-correct string -> std::filesystem::path
        ├── PathSuffix.h       # Case-insensitive filename-suffix matching (import()/surface() format dispatch)
        └── Log.h              # spdlog wrapper
```

---

## 4. Dependencies

Resolved via vcpkg (`vcpkg.json`):

```json
{
  "dependencies": [
    "vulkan-memory-allocator",
    "glfw3",
    "imgui[vulkan-binding,glfw-binding,docking-experimental]",
    "glm",
    "manifold",
    "spdlog",
    "nlohmann-json",
    "catch2",
    "zlib"
  ]
}
```

Vulkan itself is located via `find_package(Vulkan REQUIRED)` against the
system Vulkan SDK, not vcpkg. `zlib` (raw DEFLATE decompression, via
`find_package(ZLIB REQUIRED)`) backs `import()`'s `.3mf` support — 3MF is a
ZIP archive and `src/import/ZipReader.h/cpp` needs an inflate implementation,
not a dependency any other subsystem uses. Three more dependencies are
vendored directly in the source tree rather than pulled via vcpkg:
`third_party/stb/stb_truetype.h` (public domain, used by `text()`'s glyph
extraction), `third_party/stb/stb_image.h` (public domain, used by
`surface()`'s PNG heightmap decoding), and `resources/fonts/Roboto-Regular.ttf`
(Apache 2.0, the default font when `text()`'s `font=` is omitted).

**Embree is not currently a dependency.** It was previously planned as a
future BVH-accelerated spatial-query backend / custom-boolean research path
(see `docs/roadmap.md`, Future/Research), but nothing in the current pipeline
uses it — it isn't in `vcpkg.json` and there's no `find_package(embree3)` in
`CMakeLists.txt`.

---

## 5. Language Subsystem

### 5.1 Supported Syntax

- Primitives (3D): `cube`, `sphere`, `cylinder`, `polyhedron()`
- Primitives (2D): `square`, `circle`, `polygon`, `text()`
- Booleans/CSG: `union()`, `difference()`, `intersection()`, `hull()`, `minkowski()`
- Transforms: `translate()`, `rotate()`, `scale()`, `mirror()`, `multmatrix()`, `color()`, `render()`, `resize()`
- CSG modifier characters: `#` (highlight), `%` (background), `*` (disable), `!` (root) — see §6
- Control flow: `for` (with ranges), `if`/`else`, statement- and expression-level `let`, ternary `?:`, list comprehensions (`[for (i=range) expr]`, with `if`/`each`)
- Functions & modules: user-defined `function`/`module`, `children()`/`$children`, named + default args, recursion
- Built-ins: full math set, string/vector helpers (`concat`, `str`, `chr`, `ord`, `len`, `lookup`, `rands`, `norm`, `cross`, ...)
- 2D → 3D: `linear_extrude`, `rotate_extrude` (including nested extrusion — extrude wrapping extrude), `offset()`, `projection()`
- File I/O: `include <>`, `use <>` (via `SourceLoader`, with circular-include detection and per-file diagnostics for eval-time errors reached through either — `SourceLoc::fileId`, see §5.3/§5.4), `import()` (STL, OFF, 3MF, AMF as 3-D meshes; DXF, SVG as 2-D `Polygon2D` outlines — `layer=` for DXF), `surface()` (`.dat` text heightmaps or `.png` grayscale-luminance heightmaps)
- Diagnostics: `echo()`, `assert()`
- Quality: `$fn`, `$fs`, `$fa` — both global (file-scope) and per-node overrides
- Literals: numbers, strings, vectors `[x,y,z]`, `true`/`false`, `undef`
- Comments: `//` and `/* */`

### 5.2 AST Design

Uses `std::variant` over inheritance — no vtables, exhaustive matching with
`std::visit`, cache-friendly layout. The real variant (`src/lang/AST.h`) has
grown well beyond the original three node kinds as the language expanded:

```cpp
using AstNode = std::variant<
    PrimitiveNode, BooleanNode, TransformNode, IfNode, ForNode,
    ModuleCallNode, ExtrusionNode, LetNode, ColorNode, OffsetNode,
    ProjectionNode>;
using AstNodePtr = std::unique_ptr<AstNode>;
```

`ModuleCallNode` covers both built-in calls (echo/assert/children/etc.) and
user-defined module invocation. Each primitive/module/extrusion node stores
its arguments as `std::unordered_map<std::string, ExprPtr>` — raw expression
trees, not resolved doubles — so a call like `cube(size = w * 2)` carries
`w * 2` unevaluated until the `Interpreter` resolves it against the current
variable environment. For example:

```cpp
struct PrimitiveNode {
    enum class Kind { Cube, Sphere, Cylinder, Square2D, Circle2D, Polygon2D };
    Kind kind;
    std::unordered_map<std::string, ExprPtr> params; // "r", "h", "$fn", etc.
    bool center = false;
    SourceLoc loc;
};
```

A parallel `Expr` variant (`src/lang/Expr.h`) covers the expression
sub-language: literals, binary/unary/ternary ops, indexing, function calls,
vector literals, and `let` expressions.

### 5.3 Parser Error Strategy

- **Error recovery:** on syntax error, skip to next `}` or `;` and continue
- **Multiple errors:** all diagnostics collected; never hard-stop on first error
- **Source locations:** every node carries `SourceLoc` for diagnostic display and VS Code jump

### 5.4 Diagnostic

```cpp
enum class DiagLevel { Info, Warning, Error };

struct Diagnostic {
    DiagLevel level;
    std::string message;
    SourceLoc loc;
    std::string filePath;
};
```

**Per-file diagnostics across `include`/`use` (v3 Phase 4):** `SourceLoc`
(`src/lang/Token.h`) carries a `fileId` alongside `line`/`col`/`offset` — the
`Lexer` stamps its constructor-supplied `fileId` onto every token it emits,
and the `Parser` copies `token.loc` verbatim into every AST/`Expr` node it
builds, so `fileId` propagates through parsing for free. `SourceLoader`
(`loadSource()`) assigns each file a `fileId` in open order (root file is
always 0, matching `SourceLoc`'s own "0 is a sensible default" convention)
and returns the `fileId -> path` table as `LoadedSource::files`.

This matters because `include`/`use` don't concatenate source text — each
file is lexed/parsed independently and `SourceLoader` splices already-parsed
AST *nodes* from included files into the root's `ParseResult` — so once
`Lexer`/`Parser` are done, `filePath` string on lex/parse-time diagnostics
is all a *file's own* diagnostics need, but **eval-time** diagnostics
(`assert()`, `import()`/`surface()`/`text()`/`polyhedron()` errors, module
recursion limit — all produced later, by `CsgEvaluator` walking the already-
merged tree) have no such string to copy from; they only have the AST node's
`SourceLoc`. `MeshBuilder` threads `LoadedSource::files` into
`CsgEvaluator::fileTable` before evaluation, and `CsgEvaluator` resolves
`loc.fileId -> filePath` itself at each diagnostic-construction site
(`resolveFilePath()`), so these diagnostics carry the correct file even when
the failing `assert()`/`import()`/etc. call is reached through `include`/
`use`. The same `fileTable` also fixes `import()`/`surface()`/`text()`'s
relative-path resolution: a relative path resolves against the directory of
the file that actually contains the call (via `resolveFilePath(call.loc.fileId)`),
not always the root file's directory (`CsgEvaluator::baseDir`, which is now
only the fallback when `fileTable` is unset or the id is out of range).

---

## 6. CSG Tree (Intermediate Representation)

The AST is not used directly for rendering or evaluation. `CsgEvaluator` walks
the AST (using the `Interpreter` to resolve every `ExprPtr` param to a
concrete value) and builds a separate **CSG Tree** (`src/csg/CsgNode.h`). This
decouples the language from the geometry engine and enables subtree caching.

**Key design decision:** transforms are **folded into leaf/composite nodes**
during the AST→CSG walk — the CSG tree carries no separate transform nodes;
every node carries its own accumulated world-space `glm::mat4`. The real IR
(as of v3 Phase 3) has six node kinds, not two:

```cpp
using CsgNode    = std::variant<CsgLeaf, CsgBoolean, CsgExtrusion, CsgOffset, CsgProjection, CsgResize>;
using CsgNodePtr = std::shared_ptr<CsgNode>;

struct CsgLeaf {
    enum class Kind { Cube, Sphere, Cylinder, Square2D, Circle2D, Polygon2D, Mesh, Polyhedron };
    Kind kind = Kind::Cube;
    std::unordered_map<std::string, double> params; // resolved, not ExprPtr
    bool center = false;
    glm::mat4 transform{1.0f};
    ColorAttr color;
    // Polygon2D: resolved contour points/paths.
    // Mesh (import()/surface()) and Polyhedron (polyhedron()): raw
    // triangle-soup positions/indices.
};

struct CsgBoolean {
    enum class Op { Union, Difference, Intersection, Hull, Minkowski };
    Op op = Op::Union;
    std::vector<CsgNodePtr> children;
    glm::mat4 transform{1.0f}; // only non-identity for Hull/Minkowski (see below)
    ColorAttr color;
};
```

`CsgExtrusion` (`linear_extrude`/`rotate_extrude`), `CsgOffset` (`offset()`),
`CsgProjection` (`projection()`), and `CsgResize` (`resize()`) follow the
same shape: resolved params (or, for `CsgResize`, resolved `newX/newY/newZ`
and `autoX/autoY/autoZ` fields), a `children` list, an accumulated
`transform`, and a `ColorAttr`. `CsgResize` is the one exception to "transforms
are folded in during the AST→CSG walk": unlike `scale()`, its scale factor
depends on the tessellated bounding box of its children, which doesn't exist
until `MeshEvaluator` builds an actual `manifold::Manifold` — so
`CsgEvaluator` only resolves `newsize`/`auto` into the node, and
`MeshEvaluator::evalResize()` unions the children, measures
`Manifold::BoundingBox()`, and computes+applies the scale from that.

**`ColorAttr`** is a `{ bool has; glm::vec4 value; }` pair that accumulates
top-down exactly like the transform matrix — a nested `color()` overrides its
ancestor's for its own subtree. It's a per-*node* attribute, not per-*leaf*:
because Manifold booleans merge a subtree's children into one mesh with no
surviving per-part identity, `color()` tints a whole boolean result rather
than individual children the way OpenSCAD's CGAL-based per-facet coloring
can.

**CSG modifier characters (`# % ! *`)** are parsed as a `Modifier` bitmask
(`src/lang/AST.h`) stamped onto whichever `AstNode` they prefix — any
statement, at file scope or nested in a block — and interpreted centrally in
`CsgEvaluator::evalNode()`:
- `*` (disable) — the subtree isn't evaluated at all; `evalNode()` returns
  `nullptr` for it immediately, so nested `echo()`/`assert()`/module calls
  never run.
- `#` (highlight) — the node's own `ColorAttr` is forced to a fixed tint,
  exactly like an implicit `color()` wrapper, while it still participates in
  its parent's boolean normally. Like nested `color()`, the tint is only
  visible once the node is (or is inside) a `CsgScene` root, per the
  boolean-merge limitation below.
- `%` (background) — excluded from its parent's boolean (doesn't affect the
  computed result) and instead pushed onto a new `CsgScene::backgroundRoots`
  list, meshed and rendered like any other root but kept out of
  `MeshBuilder`'s volume/surface-area/triangle-count stats and out of the
  STL-exportable index range (`BuildResult::realIndexCount`).
- `!` (root) — every `!`-tagged node encountered anywhere in the tree is
  collected; if that list is non-empty once the whole tree has been walked,
  `CsgEvaluator::evaluate()` replaces `CsgScene::roots` with exactly that
  list (discarding `backgroundRoots` too), matching OpenSCAD's "only show
  this" semantics.

Both `#` and `%` render as fully opaque tints rather than OpenSCAD's
translucent preview — there is no alpha-blending pipeline yet (`render::Vertex`
has no alpha channel, and no pipeline enables `blendEnable`; see §10 and the
v4 roadmap).

**Local-space evaluation for non-equivariant ops:** `hull()`, `minkowski()`,
`offset()`, `projection()`, and `resize()` all evaluate their children in
local space and apply the accumulated outer `transform` once to the final
result, rather than per-child. This is required because these operations
aren't equivariant under arbitrary per-child transforms — e.g.
`MinkowskiSum(T(A), T(B)) ≠ T(A) ⊕ B` in general, and similarly resizing each
child of `resize() { a(); b(); }` independently instead of resizing their
union would not match OpenSCAD's "resize the combined shape" semantics.

**Caching:** unlike the earlier design sketch, no hash is stored on the CSG
node itself. `MeshCache` (see §9) builds a cache key on demand by formatting
each node's resolved params and transform into a string
(`src/csg/MeshEvaluator.cpp`'s key-building helpers) rather than a
precomputed bottom-up hash.

**Known scoping limitation:** the `Interpreter`'s variable environment is a
single flat map, not a real lexical scope stack. Every block-producing
`evalXxx()` (boolean/transform/if/for/extrusion/offset/projection/color)
now snapshots it before evaluating its children and restores it after
(`docs/roadmap.md` v3 Phase 1), so a local assignment inside a block is
visible to later statements in that same block but doesn't leak past it —
this fixed the previous silent-discard bug. What snapshot/restore doesn't
fix: unbound function/module parameters can still resolve to a same-named
caller variable instead of `undef`, since it's still one flat map rather
than a real scope chain (tracked in GitHub Issues).

---

## 7. Async Evaluation Pipeline

This is implemented by `MeshBuilder` (`src/app/MeshBuilder.h/cpp`), which is
simpler than earlier design sketches for this document described — there is
one background worker thread, not a thread pool, and cancellation is a
generation counter, not `std::stop_token`.

### 7.1 State Machine

`BuildPhase` (`MeshBuilder.h`):

```
Idle → Parsing → Evaluating → Meshing → Converting → Done
                                                     ↘ Error
```

`Evaluating` covers Lexer/Parser/Interpreter/`CsgEvaluator` (AST → `CsgScene`);
`Meshing` covers `MeshEvaluator` (`CsgScene` → `manifold::Manifold` per root,
i.e. where the actual boolean ops happen); `Converting` builds vertex/index
buffers from the resulting meshes.

### 7.2 Cancellation

There is no cooperative mid-operation cancellation (no `std::stop_token`,
`std::jthread`, or thread pool — `src/util/ThreadPool.h` exists only as an
unimplemented stub). Instead, `MeshBuilder::requestBuild()` increments an
atomic generation counter (`m_currentGen`). `buildOne()` re-checks
`gen == m_currentGen` at a handful of checkpoints between pipeline phases and
bails out early if a newer build has been requested; `poll()` additionally
discards any finished `BuildResult` whose generation doesn't match the
current one. A build that's already deep inside a single Manifold operation
runs that operation to completion — cancellation only takes effect at the
checkpoints between phases, not instantaneously.

> **Known gap:** the final stats-writing block in `buildOne()` (elapsed time,
> volume, surface area, triangle/vertex counts, `BuildPhase::Done`) is not
> gated by a generation check the way the actual mesh result is, so the
> on-screen stats overlay can transiently show a superseded generation's
> numbers even though the rendered geometry itself stays correct. Tracked in
> GitHub Issues.

### 7.3 Thread Model

```
Main Thread:      ImGui + Vulkan render loop, calls requestBuild()/poll() each frame
Worker Thread:     single std::thread owned by MeshBuilder, runs the full
                    Parsing→Evaluating→Meshing→Converting pipeline serially
```

Handoff is a mutex-protected `std::unique_ptr<BuildResult>` (not a lock-free
ring buffer — `src/util/RingBuffer.h` exists but isn't currently used
anywhere in the codebase). The worker signals via a condition variable when
new work arrives; `poll()` takes the result mutex briefly each frame to check
for a finished, non-stale build.

---

## 8. Primitive Tessellation

```cpp
struct TessParams {
    int    fn  = 0;     // $fn: fixed segment count (0 = auto)
    double fs  = 2.0;   // $fs: max segment size in mm
    double fa  = 12.0;  // $fa: max angle per segment in degrees
    bool   preview = false;
};
```

> **Design note — `$fn`/`$fs`/`$fa` scoping:** In OpenSCAD these are *special variables*
> that cascade lexically through the tree (a child can override them for its subtree,
> e.g. `sphere($fn = 64)`). `TessParams` is resolved from file-scope (global)
> values by default, but **per-node overrides are supported**: `PrimitiveGen`
> checks each leaf's own `params` map for `$fn`/`$fs`/`$fa` and uses those in
> preference to the global values when present. The tessellator interface
> accepts `TessParams` by value, matching the original design intent above.

- **Cube:** 12 triangles (2 per face)
- **Sphere:** UV sphere, segment count resolved from `TessParams`
- **Cylinder:** Triangle fan caps, resolved segment count for radial edges
- All geometry computed in `glm::dvec3` (double), cast to `glm::vec3` on GPU upload

---

## 9. Boolean Evaluation (Manifold)

`MeshEvaluator` (`src/csg/MeshEvaluator.h/cpp`) walks the `CsgScene` produced
by `CsgEvaluator` and converts it to Manifold meshes:

```cpp
class MeshEvaluator {
public:
    explicit MeshEvaluator(MeshCache& cache);
    bool useManifoldSphere = false; // Manifold's built-in sphere vs. the
                                     // OpenSCAD-compatible UV sphere
    std::vector<manifold::Manifold> evaluate(const CsgScene& scene);

private:
    manifold::Manifold    evalNode(const CsgNode&, const PrimitiveGen&);
    manifold::Manifold    evalLeaf(const CsgLeaf&, const PrimitiveGen&);
    manifold::Manifold    evalBoolean(const CsgBoolean&, const PrimitiveGen&);
    manifold::Manifold    evalExtrusion(const CsgExtrusion&, const PrimitiveGen&);
    manifold::Manifold    evalResize(const CsgResize&, const PrimitiveGen&);
    manifold::CrossSection getChildCrossSection(const CsgNode&, const PrimitiveGen&);
};
```

`getChildCrossSection()` is also where nested extrusion (`linear_extrude()`/
`rotate_extrude()` wrapping another extrude) is resolved: the inner
`CsgExtrusion` is fully evaluated to its 3-D `manifold::Manifold` first (its
own transform already baked in by `evalExtrusion()`), then flattened to a
silhouette via `Manifold::Project()` — the same technique `CsgProjection`
uses for `projection()` — so the outer extrude re-extrudes a well-defined
2-D cross-section instead of the geometry being dropped.

Each **root** node is evaluated independently and returned as a separate
`manifold::Manifold` (so objects nested inside other objects stay visible,
matching OpenSCAD's semantics) — the caller concatenates vertex buffers
across roots. Note there is no `std::stop_token` parameter here: this
function runs to completion once called; cancellation of a stale build
happens one level up, at `MeshBuilder`'s phase checkpoints (§7.2), not by
interrupting an in-progress Manifold operation.

`MeshCache` (§6) stores completed geometry keyed by a string built from each
node's resolved params and transform, with LRU eviction and a configurable
entry-count budget (default 128). As noted in §6, this cache is currently
instantiated fresh inside every `MeshBuilder::buildOne()` call rather than
persisted across rebuilds, so in practice it only helps with duplicate leaves
*within* a single evaluation pass, not across file saves — a known gap
tracked in GitHub Issues.

---

## 10. Vulkan Renderer

### Render Passes

Verified against the current `Pipeline`/`Renderer` code:

1. Background pass — full-screen triangle, procedural background (`bgPipeline`, `background.vert/frag`)
2. Mesh pass — `solidPipeline` or `wirePipeline` depending on `RenderMode` (`Solid`/`Wireframe`/`SolidEdges`, drawing the same mesh with different `VkPolygonMode`/depth settings for the edges overlay)
3. ImGui

`RenderGraph.h/cpp` currently exists as a thin placeholder rather than a full
pass-ordering/barrier abstraction — pass sequencing lives directly in
`Renderer.cpp` today. Shading is **multi-light Blinn-Phong** (three
directional lights + a camera-relative rim light + ACES filmic tonemapping,
see `mesh.frag`), not PBR — a PBR material model and SSAO are both roadmap
items (v4), not yet implemented. There is no separate depth pre-pass.

### Camera

Arcball orbit (`src/render/Camera.h/cpp`).

### GPU Mesh Upload

`GpuMesh` (`src/render/GpuMesh.h/cpp`) holds a single vertex/index buffer
pair, not a double-buffered pair with an atomic read index. `Renderer`
calls `vkDeviceWaitIdle()` before destroying and recreating the buffers on
each upload — safe, but not lock-free; there is currently no
render-thread-never-stalls guarantee during a mesh swap.

---

## 11. Editor System

ChiselCAD currently has one editing mode: **external**. A `FileWatcher`
(`src/editor/FileWatcher.h`, with `FileWatcherWin32.cpp`/`FileWatcherInotify.cpp`
backends) monitors a `.scad` file on disk; on modification, the file is
re-read and fed into `MeshBuilder`'s pipeline. Diagnostics appear in
`DiagnosticsPanel`; clicking one fires `code --goto filepath:line:col` to
jump to the location in VS Code (`ExternalEditor.h/cpp`).

An **embedded** editor mode (an in-app ImGui text panel with syntax
highlighting and inline error squiggles) is planned but not built —
`src/editor/EditorPanel.h` is currently a three-line reserved placeholder
with no corresponding `.cpp`, and is not compiled into the project (absent
from `CMakeLists.txt`'s source list). Don't rely on architecture text
elsewhere implying it exists today.

---

## 12. Export — Binary STL

```
Header:           80 bytes
Triangle count:   uint32_t
Per triangle:     normal (vec3) + v0/v1/v2 (vec3 each) + attribute (uint16_t = 0)
```

Pulled from the latest evaluated `manifold::Manifold`. Triggered via
`File → Export STL`.

---

## 13. Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Boolean backend | Manifold | Apache 2.0, parallel, robust, 100–1000× faster than CGAL |
| AST design | `std::variant` | No vtables, exhaustive matching, cache-friendly |
| Transform folding | Into leaf/composite nodes | Simplifies evaluator; no separate transform nodes in CSG tree |
| Cancellation | Generation counter (not `std::stop_token`) | Simpler than cooperative cancellation; coarser-grained — see §7.2 |
| Mesh precision | double in eval, float on GPU | Max precision through pipeline, GPU perf on upload |
| Mesh handoff | Mutex-protected result pointer (not lock-free) | Simpler than a ring buffer; contention is negligible at one build per file save |
| File watch | Platform-native | inotify / ReadDirectoryChangesW, no polling |
| Error recovery | Skip-to-delimiter | Maximizes useful output while user is mid-edit |
| Shading | Multi-light Blinn-Phong + rim + ACES tonemap | Simpler and cheaper than PBR for v1-v2.5; PBR/SSAO are roadmap items (v4) |

Spatial-query acceleration (previously listed here as an Embree-backed
future direction) has been moved to `docs/roadmap.md`'s Future/Research
section — it isn't a current dependency (see §4).
