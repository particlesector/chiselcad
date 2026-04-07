# Contributing to ChiselCAD

Thanks for your interest in contributing. ChiselCAD is early-stage software —
there's a lot of ground to cover and good contributors are very welcome.

## Before You Start

Read [docs/architecture.md](docs/architecture.md). It describes the full system
design, subsystem responsibilities, and the key decisions that have been made.
PRs that conflict with the architectural intent without discussion are unlikely
to be merged.

## Getting the Code

```bash
git clone https://github.com/YOUR_USERNAME/chiselcad.git
cd chiselcad
git submodule update --init --recursive
```

Build instructions are in [README.md](README.md).

## Code Style

- **C++20** throughout. Use modern features where they genuinely improve clarity.
- **No raw owning pointers.** Use `std::unique_ptr`, `std::shared_ptr`, or value types.
- **No `std::mutex` in the render hot path.** The async pipeline uses lock-free
  structures specifically to keep the render thread unblocked.
- **`std::visit` over virtual dispatch** for AST/CSG node types.
- Formatting is enforced by `.clang-format` (based on LLVM style, 100-column limit).
  Run `clang-format -i` before submitting.
- No warnings. All targets build with `-Wall -Wextra -Wpedantic` (or MSVC equivalent).

## Areas That Need Work

Check [docs/roadmap.md](docs/roadmap.md) for planned features. The most
impactful areas right now:

- **Language subsystem** — Lexer, parser, and AST are fully spec'd and well-isolated.
  Good entry point for contributors comfortable with parsing.
- **Primitive tessellation** — Sphere, cylinder, and cube generators. Pure geometry,
  no dependencies other than GLM. Easy to unit test.
- **Vulkan renderer** — If you have Vulkan experience, the render graph and
  pipeline setup are the biggest open pieces.
- **Test coverage** — The test `.scad` file in `tests/` is a start. Unit tests
  for the lexer, parser, and CSG evaluator would be very valuable.

## Submitting a PR

1. Open an issue first for anything non-trivial. Alignment before implementation
   saves everyone time.
2. Keep PRs focused. One logical change per PR.
3. Include a brief description of what changed and why.
4. All CI checks must pass.

## Reporting Bugs

Open a GitHub issue. Include:
- OS and GPU
- The `.scad` file that triggers the issue (or a minimal reproduction)
- Expected vs actual behaviour
- ChiselCAD version / commit hash

## License

By contributing, you agree that your contributions will be licensed under the
MIT License that covers this project.
