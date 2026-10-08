# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` is the canonical agent entry point (mandatory workflow, stack boundaries, style, product constraints) and applies here in full. Key points: every task goes Antora spec (behavior, constraints, acceptance criteria) → GitHub issue linking the spec → labels for type, `area:*` and `status:ready` → tests with the expected failure observed (RED) → implementation, passing checks and a PR linked to the issue with a closing keyword (`Closes #N`), with evidence recorded in the issue. Never implement undocumented or untracked behavior; never claim Android support from desktop-only tests; do not add payment integrations or pick a license (`LICENSE.draft.md` is a draft, not in force).

## Build and test

Dependencies (Ceres, Eigen, GoogleTest) come from the vcpkg manifest (`vcpkg.json`, pinned baseline); Qt 6.5+ (Quick, Test) comes from the system/SDK (set `CMAKE_PREFIX_PATH` if needed).

```bash
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/desktop -j 4
ctest --test-dir build/desktop --output-on-failure
```

- Core only, no Qt: add `-DSKETCHCAD_BUILD_APP=OFF`.
- Single test binary / case: `ctest --test-dir build/desktop -R sketch_test --output-on-failure`, or `./build/desktop/sketch_test --gtest_filter=Suite.Case`. GoogleTest cases are auto-registered via `gtest_discover_tests`; `sketch_controller_test`, `qml_test` and `qml_smoke` are plain CTest entries.
- QML tests and smoke need `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` (CTest sets this itself; set it manually when running the binaries directly). `./build/desktop/sketchcad --smoke` is the app smoke mode.
- Docs: `npm ci && npm run docs` (Antora). Antora reads Git HEAD, so commit docs before building the site.
- CI (`.github/workflows/ci.yml`) runs the desktop build + ctest on Ubuntu 24.04 and the Antora build. `build/` is a local build directory (it also holds scratch files from backlog tooling); `design/` holds the interactive HTML UI mockups (tablet sketch, part, assembly, drawing).

## Architecture

Layering is deliberate and must be preserved:

- `include/sketchcad/` + `src/` → `sketchcad_core` static library. **Must stay free of Qt and OCCT.** Ceres is a private link dependency.
  - `sketch.h`/`sketch.cpp`: the general validated sketch model (issue #4) — `Sketch` with stable `EntityId` (uint64) entities as a `std::variant` of point/line/circle/arc, construction flag, polylines. Creation functions return `std::optional<EntityId>` (reject invalid/non-finite input); mutations are transactional.
  - `rectangle.h`/`rectangle.cpp`: the earlier issue #1 prototype — a preset constrained rectangle solved with Ceres (dimensions, translation, origin anchor, rollback on contradictory width). It is a demo, not the general solver.
  - `document.h`/`document.cpp`: `Document` (issue #5) — the single change boundary for a `Sketch`: atomic commands on a working copy, a commit-step hook for the future solver, linear snapshot Undo/Redo, grouped gestures and dirty state. Contract in `document-commands.adoc`.
  - `sketch_view.h`/`sketch_view.cpp` and `sketch_tool.h`/`sketch_tool.cpp` (issue #9): Qt-free viewport math (`ViewTransform` world mm Y-up ↔ screen px Y-down, `bounds`, `pick`, simple grid/point `snap`, `grid_step_for_scale`) and the `ToolSession` drawing/selection state machine that issues `Document` commands. Contract in `sketch-viewport.adoc`.
- `app/` → Qt layer (U01, issue #9). `SketchController` (`sketchcad_controller` library, Q_PROPERTY/Q_INVOKABLE bridge, snake_case slots) wraps `Document` + `ToolSession` + `ViewTransform` and publishes geometry as SVG path strings in viewport pixels; `SketchViewport.qml` renders them with Qt Quick Shapes (`CurveRenderer`, ADR-0002) and maps pointer input; `Main.qml` is the Material tablet shell (tool rail, context bar, zoom); `main.cpp` builds the `sketchcad` executable (`--smoke`, `--screenshot <file>`). The core `Rectangle` demo has no UI any more.
- `tests/`: GoogleTest for core (`rectangle_test`, `sketch_test`, `document_test`, `sketch_view_test`, `sketch_tool_test`), Qt Test for UI (`sketch_controller_test`, `qml_test` driving actual QML controls offscreen and saving `sketch_u01.png` next to the binary).

The solver is not yet wired to the general `Sketch`; the planned sequence (generalized solver S01, issue #6, is next) lives in the backlog/implementation-plan pages. Modeling constraints from `AGENTS.md` that shape the code: millimeters, finite-parameter validation, transactional solve with rollback, explicit per-residual tolerance checks (Ceres convergence ≠ constraints satisfied), and DOF analysis must exclude drag/stabilization residuals.

## Documentation

Canonical docs are Antora AsciiDoc in `docs/modules/ROOT/pages/*.adoc` (nav in `nav.adoc`): product-vision, scope-0.1/1.0, roadmap, backlog-0.1, decision-log, licensing, sketch-model, build, and `handoff.adoc` (current project state for the next agent — read it first; much of it is in Russian). README is only a short entry point. Style: Google C++ (`.clang-format` is `BasedOnStyle: Google`), snake_case functions and variables, no owning raw pointers.
