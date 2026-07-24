# ChiselCAD

**A fast, precise, GPU-accelerated CSG modeler with OpenSCAD-compatible syntax.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)]()
[![C++20](https://img.shields.io/badge/C%2B%2B-20-informational)]()
[![Status](https://img.shields.io/badge/status-early%20development-orange)]()

---

## What is ChiselCAD?

ChiselCAD is an open-source 3D CAD modeler for engineers and makers who prefer
code-driven design. It reads OpenSCAD-syntax `.scad` files and renders them with
a modern Vulkan renderer — giving you a dramatically faster result than
legacy OpenGL-based tools.

It implements nearly all of the OpenSCAD language today — primitives, booleans,
transforms, control flow, user-defined functions/modules and function
literals, 2D extrusion, `color()`/`offset()`/`projection()`, and file I/O
(`include`/`use`/`import`/`surface`/`text`). See
[Supported Language](#supported-language) below for the full breakdown and
[docs/roadmap.md](docs/roadmap.md) (v3.7/v3.8) for the small number of constructs
still missing — mainly `assert()`/`echo()` used as chained expressions inside
function bodies, multi-variable/C-style list comprehensions, and `roof()`.

All 33 issues from the original correctness audit (v3) are closed. A July
2026 completeness re-audit (v3.7) fixed three gaps found by re-reading the
source against the OpenSCAD manual (`log()`/`ln()` were swapped, `str()`
dropped vector/range arguments, `$vpf` was missing), and a follow-up pass
(v3.8) went further — running OpenSCAD's own test corpus through a live,
installed OpenSCAD binary and diffing against ChiselCAD's output — which
found a parser crash on `$special=value` call arguments (fixed) plus a
handful of confirmed-but-still-open gaps (dot-member access like `v.x`,
calling a function-literal expression's result directly, named/positional
argument ordering, and some Unicode-string edge cases). See
[docs/roadmap.md](docs/roadmap.md) (v3.8) for the full list.

```scad
difference() {
    cube([50, 35, 7]);
    translate([25, 17.5, -1])
        cylinder(h = 9, r = 5.5);
    translate([6, 6, -1])
        cylinder(h = 9, r = 2.8);
}
```

> **ChiselCAD vs OpenSCAD at a glance**
>
> | | OpenSCAD | ChiselCAD |
> |---|---|---|
> | Renderer | Legacy OpenGL + OpenCSG | Vulkan, multi-light Blinn-Phong + rim lighting (PBR/SSAO planned) |
> | Boolean backend | CGAL (slow) / Manifold (experimental) | Manifold (always on) |
> | UI responsiveness | Blocks on F6 render | Async — UI never freezes |
> | Editor | Embedded QScintilla | External (VS Code) + file watch + diagnostics panel; embedded editor and LSP planned |
> | AI assistance | None | Claude integration (planned) |
> | Language coverage | Full OpenSCAD | Nearly all of the language; a handful of constructs remain (see below) |

---

## Features

- **Vulkan renderer** with multi-light Blinn-Phong shading, rim lighting, and ACES tonemapping (PBR + SSAO on the roadmap)
- **Async dual-phase preview** — instant primitive display while booleans evaluate in the background
- **[Manifold](https://github.com/elalish/manifold) boolean backend** — 100–1000× faster than CGAL
- **OpenSCAD-syntax compatible** — `.scad` files work as-is for most of the language (see below)
- **VS Code integration** — edit in VS Code, ChiselCAD hot-reloads on save with live error feedback
- **ImGui interface** — lightweight, dockable, fast
- **Cross-platform** — Windows and Linux (macOS planned)
- **C++20** throughout

### Supported Language

| Category | Supported |
|---|---|
| Primitives (3D) | `cube`, `sphere`, `cylinder`, `polyhedron()` |
| Primitives (2D) | `square`, `circle`, `polygon`, `text()` |
| Booleans / CSG | `union()`, `difference()`, `intersection()`, `hull()`, `minkowski()` |
| Transforms | `translate()`, `rotate()`, `scale()`, `mirror()`, `multmatrix()`, `color()`, `resize()` |
| CSG modifiers | `#` (highlight), `%` (background), `!` (root), `*` (disable) |
| Control flow | `for`, `if`/`else`, `let`, ternary `?:`, ranges (`[a:b]`/`[a:b:c]`), list comprehensions (single-variable) |
| Functions & modules | user-defined `function`/`module`, `children()`/`$children`, named + default args |
| Built-ins | full math set, string/vector helpers (`concat`, `str`, `len`, `lookup`, `rands`, ...) |
| 2D → 3D | `linear_extrude`, `rotate_extrude` (including nested extrusion), `offset()`, `projection()` |
| File I/O | `include <>`, `use <>` (per-file diagnostics), `import()` (STL, OFF, 3MF, AMF, DXF, SVG), `surface()` (`.dat` or `.png`) |
| Diagnostics | `echo()`, `assert()` (statement form only — not yet as chained expressions) |
| Quality | `$fn`, `$fs`, `$fa`, `$vpr`/`$vpt`/`$vpd`/`$vpf` (global and per-node) |
| Export | Binary STL |

v3–v3.6 (see [docs/roadmap.md](docs/roadmap.md)) are complete. v3.7/v3.8
found and fixed further gaps (`log()`/`ln()`, `str()` on vectors, `$vpf`,
a `$special=value` call-argument parser crash) via both source audit and
corpus testing against a live OpenSCAD binary. What's left — `assert()`/
`echo()` as expressions, multi-variable list comprehensions, `roof()`,
non-STL export, vector dot-member access, and a few other confirmed gaps —
is tracked in [docs/roadmap.md](docs/roadmap.md).

---

## Building

### Prerequisites

- CMake 3.25+
- Vulkan SDK 1.3+
- vcpkg
- A C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)

### Windows / Linux

```bash
git clone https://github.com/particlesector/chiselcad.git
cd chiselcad
git submodule update --init --recursive

cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The binary will be at `build/chiselcad` (Linux) or `build/Release/chiselcad.exe` (Windows).

### Dependencies

All resolved via vcpkg:

| Library | Purpose |
|---|---|
| Vulkan + VMA | GPU rendering |
| GLFW | Window + input |
| ImGui (docking) | UI |
| Manifold | CSG boolean operations |
| GLM | Math |
| spdlog | Logging |
| nlohmann-json | Config |
| ImGuiColorTextEdit | Embedded code editor |

---

## VS Code Workflow

1. Open a `.scad` file in VS Code
2. Launch ChiselCAD and click **File → Watch File** (or pass the path as an argument)
3. ChiselCAD monitors the file — every save triggers an instant re-evaluation
4. Errors appear in ChiselCAD's diagnostics panel with file/line/column — click to jump

A full LSP extension for VS Code is planned (see [docs/roadmap.md](docs/roadmap.md)).

---

## Documentation

| Document | Description |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Full system architecture, subsystem design, and key decisions |
| [docs/roadmap.md](docs/roadmap.md) | Planned features and future direction |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute |

---

## Project Status

ChiselCAD is in **active development**. The core rendering pipeline, CSG
evaluator, and nearly all of the OpenSCAD language are implemented and
working (see [Supported Language](#supported-language) above). Current focus
is closing the remaining language gaps tracked in
[docs/roadmap.md](docs/roadmap.md) (v3.8) — found via corpus testing against
a live OpenSCAD binary, see `tests/tools/README.md` — and the v4 tooling/
visual-quality work, before making a full drop-in-replacement claim.

If you want to follow along or contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

MIT — see [LICENSE](LICENSE).

ChiselCAD uses [Manifold](https://github.com/elalish/manifold) (Apache 2.0),
[Dear ImGui](https://github.com/ocornut/imgui) (MIT), and other open-source
libraries. See [docs/architecture.md](docs/architecture.md) for the full dependency list.
