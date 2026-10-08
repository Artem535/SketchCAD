# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` is the canonical agent entry point (mandatory workflow, stack boundaries, style, product constraints) and applies here in full. Key points: every behavior change goes docs (Antora, with acceptance criteria) → GitHub issue with type/area/status labels → failing test (observe RED) → implementation → full relevant test run, with evidence recorded in the issue. Never implement undocumented or untracked behavior; never claim Android support from desktop-only tests; do not add payment integrations or pick a license (`LICENSE.draft.md` is a draft, not in force).

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
  - `document.h`/`document.cpp`: `Document` (issue #5) — the single change boundary for a `Sketch`: atomic commands on a working copy, a commit-step hook for the future solver, linear snapshot Undo/Redo, grouped gestures and dirty state. Contract in `document-commands.adoc`.
  - `sketch_view.h`/`sketch_view.cpp` and `sketch_tool.h`/`sketch_tool.cpp` (issue #9): Qt-free viewport math (`ViewTransform` world mm Y-up ↔ screen px Y-down, `bounds`, `pick`, simple grid/point `snap`, `grid_step_for_scale`) and the `ToolSession` drawing/selection state machine that issues `Document` commands. Contract in `sketch-viewport.adoc`.
  - `constraint.h` + constraints in `Sketch`, `solver.h`/`constraint_solver.cpp` (issue #6): constraints (coincident, horizontal/vertical, parallel/perpendicular, tangent, equal, fix) share the entity ID allocator and block erasing referenced entities; `solve(Sketch&)` builds one Ceres problem of hard residuals, re-checks every residual against `kLengthTolerance`/`kAngleTolerance` and writes back only on `kSolved` (else `kInvalidInput`, `kUnsatisfied`, `kNumericalFailure`); `solver_step()` is the `Document` commit step. Contract in `constraint-solver.adoc`.
  - Driving dimensions (issue #7): `kLength`, `kDistance` (point–point, point–line with stored side), `kAngle` (undirected, radians) and `kRadius` are constraints with a `value`, created with `add_dimension` and changed with `set_dimension` inside a `Document` command; `measure()` in `solver.h` gives current values. `ToolSession` delete removes the constraints of erased geometry in the same command. Contract in `dimensions.adoc`. The issue #1 `Rectangle` prototype was retired here.
  - `diagnostics.h`/`constraint_diagnostics.cpp` (issue #8): `diagnose(const Sketch&)` solves a copy, takes the row-normalized Jacobian of the persistent constraints, and reports DOF, rank, dependent (redundant/conflicting, not minimal) and violated constraint IDs, or `kUnknown` with a reason instead of guessing. Thresholds and normalization are fixed in ADR-0003; `src/solver_problem.h` is the internal Ceres problem shared with `solve`. Contract in `diagnostics.adoc`.
- `app/` → Qt layer (U01, issue #9). `SketchController` (`sketchcad_controller` library, Q_PROPERTY/Q_INVOKABLE bridge, snake_case slots) wraps `Document` (with the solver as commit step) + `ToolSession` + `ViewTransform`, exposes `add_dimension`/`set_dimension`/`dimensions` (angles in degrees, failure keys in `message`) and publishes geometry as SVG path strings in viewport pixels; `SketchViewport.qml` renders them with Qt Quick Shapes (`CurveRenderer`, ADR-0002) and maps pointer input; `Main.qml` is the Material tablet shell (tool rail, context bar, zoom); `main.cpp` builds the `sketchcad` executable (`--smoke`, `--screenshot <file>`).
- `tests/`: GoogleTest for core (`sketch_test`, `document_test`, `sketch_view_test`, `sketch_tool_test`, `constraint_test`, `constraint_solver_test`, `dimension_test`, `constraint_diagnostics_test`), Qt Test for UI (`sketch_controller_test`, `qml_test` driving actual QML controls offscreen and saving `sketch_u01.png` next to the binary).

The solver runs on every app command, but constraints and dimensions have no QML UI yet (U02); the planned sequence lives in the backlog/implementation-plan pages. Modeling constraints from `AGENTS.md` that shape the code: millimeters, finite-parameter validation, transactional solve with rollback, explicit per-residual tolerance checks (Ceres convergence ≠ constraints satisfied), and DOF analysis must exclude drag/stabilization residuals.

## Documentation

Canonical docs are Antora AsciiDoc in `docs/modules/ROOT/pages/*.adoc` (nav in `nav.adoc`): product-vision, scope-0.1/1.0, roadmap, backlog-0.1, decision-log, licensing, sketch-model, build, and `handoff.adoc` (current project state for the next agent — read it first; much of it is in Russian). README is only a short entry point. Style: Google C++ (`.clang-format` is `BasedOnStyle: Google`), snake_case functions and variables, no owning raw pointers.
