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
a modern Vulkan renderer — giving you a dramatically faster and better-looking
result than legacy OpenGL-based tools.

It is **not** a drop-in OpenSCAD replacement. It targets the core CSG workflow —
primitives, booleans, and transforms — and does that subset extremely well.

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
> | Renderer | Legacy OpenGL + OpenCSG | Vulkan, PBR shading, SSAO |
> | Boolean backend | CGAL (slow) / Manifold (experimental) | Manifold (always on) |
> | UI responsiveness | Blocks on F6 render | Async — UI never freezes |
> | Editor | Embedded QScintilla | VS Code + LSP + file watch |
> | AI assistance | None | Claude integration (planned) |
> | Language coverage | Full OpenSCAD | Core CSG subset (v1) |

---

## Features

- **Vulkan renderer** with PBR shading and SSAO — models look like real objects
- **Async dual-phase preview** — instant primitive display while booleans evaluate in the background
- **[Manifold](https://github.com/elalish/manifold) boolean backend** — 100–1000× faster than CGAL
- **OpenSCAD-syntax compatible** — `.scad` files work as-is for the supported subset
- **VS Code integration** — edit in VS Code, ChiselCAD hot-reloads on save with live error feedback
- **ImGui interface** — lightweight, dockable, fast
- **Cross-platform** — Windows and Linux (macOS planned)
- **C++20** throughout

### Supported Language (v1)

| Category | Supported |
|---|---|
| Primitives | `cube`, `sphere`, `cylinder` |
| Booleans | `union()`, `difference()`, `intersection()` |
| Transforms | `translate()`, `rotate()`, `scale()`, `mirror()` |
| Quality | `$fn`, `$fs`, `$fa` |
| Export | Binary STL |

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

ChiselCAD is in **early development**. The architecture is designed, the test model
is ready, and the build scaffold is in place. Core subsystems are being implemented.

If you want to follow along or contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).

---

## License

MIT — see [LICENSE](LICENSE).

ChiselCAD uses [Manifold](https://github.com/elalish/manifold) (Apache 2.0),
[Dear ImGui](https://github.com/ocornut/imgui) (MIT), and other open-source
libraries. See [docs/architecture.md](docs/architecture.md) for the full dependency list.
