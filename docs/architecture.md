# ChiselCAD — Architecture

**Stack:** C++20 · Vulkan · ImGui · CMake + vcpkg
**Platforms:** Windows, Linux (macOS later)
**Boolean Backend:** Manifold (primary) + Embree (future spatial queries)
**Preview Strategy:** Async dual-phase — instant primitive preview → background boolean eval → GPU swap

---

## 1. Design Goals

1. **Extreme performance** — boolean evaluation should never block the UI. Complex models that take minutes in OpenSCAD/CGAL should take seconds.
2. **Extreme precision** — double precision throughout the evaluation pipeline; float only at GPU upload.
3. **Visual quality** — Vulkan + PBR shading + SSAO. Models should look like real objects, not 2010-era CAD wireframes.
4. **Developer experience** — VS Code as the primary editor, live file watch, LSP diagnostics, AI assistance (planned).
5. **OpenSCAD compatibility** — `.scad` files targeting the supported subset should produce identical geometry.

---

## 2. High-Level Pipeline

```
[Source File / Embedded Editor]
        │ (keystroke debounce ~300ms)
        ▼
[Lexer → Token Stream]
        │
        ▼
[Recursive Descent Parser]  ──► [Diagnostics]
        │
        ▼
[CSG Tree Evaluator]
        │
        ├──► [Low-res Primitive Tessellator]
        │           │ (milliseconds)
        │           ▼
        │    [GPU Upload → Preview Render]    ← IMMEDIATE
        │    (color-coded by operation type)
        │
        └──► [Background Thread Pool]
                    │
                    ▼
           [Full-res Tessellator + Manifold Boolean Eval]
                    │                  (cancellable via std::stop_token)
                    ▼
           [GPU Mesh Atomic Swap → Result Render]
                    │
                    └──► [STL Exporter]
```

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
│
└── src/
    ├── main.cpp
    ├── app/
    │   ├── Application.h/cpp  # Main loop, init/shutdown, ImGui layout
    │   ├── AppState.h         # Global atomic state flags
    │   └── Config.h/cpp       # User settings (JSON-backed)
    ├── editor/
    │   ├── EditorPanel.h/cpp  # ImGui panel + ImGuiColorTextEdit
    │   ├── FileWatcher.h/cpp  # Cross-platform (inotify / ReadDirChanges)
    │   ├── ExternalEditor.h/cpp # "Open in VS Code" + file sync
    │   └── DiagnosticsPanel.h/cpp
    ├── lang/
    │   ├── Token.h            # Token kinds + SourceLoc
    │   ├── Lexer.h/cpp
    │   ├── AST.h              # std::variant-based AST nodes
    │   ├── Parser.h/cpp       # Recursive descent, error recovery
    │   └── Diagnostic.h
    ├── csg/
    │   ├── CsgNode.h          # CSG tree IR (separate from AST)
    │   ├── CsgEvaluator.h/cpp # AST → CSG tree
    │   ├── PrimitiveGen.h/cpp # Sphere/cube/cylinder tessellation
    │   ├── MeshEvaluator.h/cpp # CSG tree → Manifold → mesh
    │   └── MeshCache.h/cpp    # Hash-keyed LRU mesh cache
    ├── render/
    │   ├── VulkanContext.h/cpp
    │   ├── VmaAllocator.h/cpp
    │   ├── Swapchain.h/cpp
    │   ├── RenderGraph.h/cpp  # Pass ordering, barriers
    │   ├── GpuMesh.h/cpp      # Vertex/index buffers, double-buffer swap
    │   ├── Pipeline.h/cpp
    │   ├── Camera.h/cpp       # Arcball orbit
    │   ├── Renderer.h/cpp
    │   └── shaders/
    │       ├── preview.vert/frag   # Per-primitive color-coded preview
    │       ├── mesh.vert/frag      # PBR result mesh
    │       └── grid.vert/frag      # Infinite ground grid
    ├── export/
    │   └── StlExporter.h/cpp
    └── util/
        ├── ThreadPool.h       # std::jthread pool (C++20)
        ├── Debouncer.h        # Cancellable debounce
        ├── RingBuffer.h       # Lock-free SPSC for mesh swap
        └── Log.h              # spdlog wrapper
```

---

## 4. Dependencies

```json
{
  "dependencies": [
    "vulkan",
    "vulkan-memory-allocator",
    "glfw3",
    "imgui[vulkan-binding,glfw-binding,docking-experimental]",
    "glm",
    "manifold",
    "embree3",
    "spdlog",
    "nlohmann-json",
    "imguicolortextedit"
  ]
}
```

**Embree** is included for future BVH-accelerated spatial queries and as a research
path toward a custom boolean backend. It is not used in the v1 boolean pipeline.

---

## 5. Language Subsystem

### 5.1 Supported Syntax (v1)

- Primitives: `cube`, `sphere`, `cylinder`
- Booleans: `union()`, `difference()`, `intersection()`
- Transforms: `translate()`, `rotate()`, `scale()`, `mirror()`
- Quality: `$fn`, `$fs`, `$fa`
- Literals: numbers, vectors `[x,y,z]`, `true`/`false`
- Comments: `//` and `/* */`

### 5.2 AST Design

Uses `std::variant` over inheritance — no vtables, exhaustive matching with
`std::visit`, cache-friendly layout.

```cpp
using AstNode = std::variant<PrimitiveNode, BooleanNode, TransformNode>;
using AstNodePtr = std::unique_ptr<AstNode>;

struct PrimitiveNode {
    enum class Kind { Cube, Sphere, Cylinder };
    Kind kind;
    std::unordered_map<std::string, double> params;
    SourceLoc loc;
};

struct BooleanNode {
    enum class Op { Union, Difference, Intersection };
    Op op;
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};

struct TransformNode {
    enum class Kind { Translate, Rotate, Scale, Mirror };
    Kind kind;
    glm::dvec3 vec;
    std::vector<AstNodePtr> children;
    SourceLoc loc;
};
```

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

---

## 6. CSG Tree (Intermediate Representation)

The AST is not used directly for rendering or evaluation. After parsing, a
separate **CSG Tree** is built. This decouples the language from the geometry
engine and enables subtree caching and future optimization passes.

**Key design decision:** transforms are **folded into leaf primitives** during the
AST→CSG walk. The CSG tree contains only `CsgPrimitive` and `CsgBoolean` nodes —
every primitive carries its own accumulated world-space transform matrix.

Each node carries a bottom-up `size_t hash` for cache lookup.

```cpp
struct CsgPrimitive {
    enum class Kind { Cube, Sphere, Cylinder };
    Kind kind;
    glm::dmat4 transform;     // accumulated, double precision
    std::unordered_map<std::string, double> params;
    size_t hash;
};

struct CsgBoolean {
    enum class Op { Union, Difference, Intersection };
    Op op;
    std::vector<CsgNodePtr> children;
    size_t hash;
};
```

---

## 7. Async Evaluation Pipeline

### 7.1 State Machine

```
IDLE
  │  (source change + debounce elapsed)
  ▼
PARSING
  │
  ├──► PREVIEW_TESSELLATING  →  PREVIEW_READY   (GPU upload, renders immediately)
  │
  └──► EVAL_QUEUED
              │
              ▼
       EVALUATING            (Manifold booleans, std::stop_token for cancellation)
              │
              ▼
       EVAL_READY            (atomic GPU mesh swap)
              │
              ▼
            IDLE
```

### 7.2 Cancellation

`std::stop_token` (C++20) is passed into the eval task. The evaluator checks the
token between each boolean operation. When a new source change arrives
mid-evaluation, `request_stop()` is called and the task terminates cleanly.

### 7.3 Thread Model

```
Main Thread:        ImGui + Vulkan render loop
Parse Thread:       std::jthread — debounced, owned by Application
Eval Thread Pool:   (hardware_concurrency - 1) worker threads
```

Mesh handoff uses an SPSC ring buffer with atomic index. The render thread
reads the latest completed mesh without locking.

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

- **Cube:** 12 triangles (2 per face)
- **Sphere:** UV sphere, segment count resolved from `TessParams`
- **Cylinder:** Triangle fan caps, resolved segment count for radial edges
- All geometry computed in `glm::dvec3` (double), cast to `glm::vec3` on GPU upload

---

## 9. Boolean Evaluation (Manifold)

```cpp
manifold::Manifold evaluate(const CsgNode& node, const std::stop_token& stop) {
    return std::visit(overloaded{
        [&](const CsgPrimitive& p) -> manifold::Manifold {
            return tessellateToManifold(p);
        },
        [&](const CsgBoolean& b) -> manifold::Manifold {
            auto result = evaluate(*b.children[0], stop);
            for (size_t i = 1; i < b.children.size(); ++i) {
                if (stop.stop_requested()) throw CancelException{};
                auto rhs = evaluate(*b.children[i], stop);
                switch (b.op) {
                    case Op::Union:        result = result + rhs; break;
                    case Op::Difference:   result = result - rhs; break;
                    case Op::Intersection: result = result ^ rhs; break;
                }
            }
            return result;
        }
    }, node);
}
```

`MeshCache` stores completed `manifold::Manifold` objects keyed by node hash,
with LRU eviction and a configurable memory budget.

---

## 10. Vulkan Renderer

### Render Passes (v1 — forward)

1. Depth pre-pass
2. Geometry pass (forward PBR)
3. Grid + overlays (infinite ground plane)
4. ImGui

### Preview Render Mode

Each leaf primitive is rendered with operation-context color coding:

| Context | Color | Alpha |
|---|---|---|
| Union child | White / material | 1.0 |
| Difference child (subtracted) | Red | 0.4 |
| Intersection child | Blue | 0.4 |

A toolbar toggle switches between **Preview** and **Result** modes.
Result mode is unavailable while evaluation is in progress.

### Camera

Arcball orbit: left-drag orbit, right-drag pan, scroll zoom.
Shortcuts: `F` = fit to scene, `1/2/3` = Front/Right/Top, `P` = toggle ortho.

### GPU Mesh Double-Buffering

Two mesh slots with an atomic read index. The eval thread writes to the inactive
slot then atomically swaps. The render thread always reads from the active slot
with no mutex.

---

## 11. Editor System

### Modes

**Embedded:** `ImGuiColorTextEdit` panel inside the docked layout. Syntax
highlighting for the supported OpenSCAD subset. Error markers (red squiggles)
driven by diagnostics.

**External:** File watcher monitors a `.scad` file on disk. On modification,
the file is re-read and fed to the parse pipeline. Clicking a diagnostic fires
`code --goto filepath:line:col` to jump in VS Code.

Both modes coexist — the embedded editor reflects external file changes.

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
| Spatial queries | Embree (future) | Best-in-class BVH for custom boolean research path |
| Preview strategy | Primitive decomposition | More debuggable than stencil CSG; shows tree structure |
| AST design | `std::variant` | No vtables, exhaustive matching, cache-friendly |
| Transform folding | Into leaf primitives | Simplifies evaluator; no transform nodes in CSG tree |
| Cancellation | `std::stop_token` | C++20 native cooperative cancellation |
| Mesh precision | double in eval, float on GPU | Max precision through pipeline, GPU perf on upload |
| Mesh handoff | SPSC atomic swap | Lock-free, render thread never stalls |
| File watch | Platform-native | inotify / ReadDirectoryChangesW, no polling |
| Error recovery | Skip-to-delimiter | Maximizes useful output while user is mid-edit |
