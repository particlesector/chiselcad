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

It implements most of the OpenSCAD language today — primitives, booleans,
transforms, control flow, user-defined functions/modules, 2D extrusion,
`color()`/`offset()`/`projection()`, and file I/O (`include`/`use`/`import`/
`surface`/`text`). See [Supported Language](#supported-language) below for the
full breakdown and the handful of constructs still missing.

It is **not yet** a verified drop-in replacement, for two reasons: a few
language constructs aren't implemented yet (tracked in
[docs/roadmap.md](docs/roadmap.md), v3), and an ongoing correctness audit has
found real bugs — including some common primitive argument forms that
currently produce silently wrong geometry. These are tracked individually in
[GitHub Issues](https://github.com/particlesector/chiselcad/issues); until the
Critical/High-severity ones are resolved, verify output geometry rather than
assuming full compatibility.

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
> | Language coverage | Full OpenSCAD | Broad subset — most of the language; a few constructs + open correctness bugs remain (see below) |

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
| Control flow | `for`, `if`/`else`, `let`, ternary `?:`, ranges (`[a:b]`/`[a:b:c]`), list comprehensions |
| Functions & modules | user-defined `function`/`module`, `children()`/`$children`, named + default args |
| Built-ins | full math set, string/vector helpers (`concat`, `str`, `len`, `lookup`, `rands`, ...) |
| 2D → 3D | `linear_extrude`, `rotate_extrude` (including nested extrusion), `offset()`, `projection()` |
| File I/O | `include <>`, `use <>` (per-file diagnostics), `import()` (STL, OFF, 3MF, AMF, DXF, SVG), `surface()` (`.dat` or `.png`) |
| Diagnostics | `echo()`, `assert()` |
| Quality | `$fn`, `$fs`, `$fa` (global and per-node) |
| Export | Binary STL |

v3 (see [docs/roadmap.md](docs/roadmap.md)) is complete. See the
[issue tracker](https://github.com/particlesector/chiselcad/issues) for
known correctness bugs currently being fixed.

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
evaluator, and most of the OpenSCAD language are implemented and working
(see [Supported Language](#supported-language) above). Current focus is
closing the remaining language gaps and fixing correctness bugs found by an
ongoing audit — both tracked in [docs/roadmap.md](docs/roadmap.md) and
[GitHub Issues](https://github.com/particlesector/chiselcad/issues) — before
making any drop-in-replacement claim.

If you want to follow along or contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

MIT — see [LICENSE](LICENSE).

ChiselCAD uses [Manifold](https://github.com/elalish/manifold) (Apache 2.0),
[Dear ImGui](https://github.com/ocornut/imgui) (MIT), and other open-source
libraries. See [docs/architecture.md](docs/architecture.md) for the full dependency list.
