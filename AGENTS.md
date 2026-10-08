# Agent instructions

## Mandatory workflow
Every task follows this order:

1. Write or update the Antora specification (docs/modules/ROOT/pages/*.adoc): behavior, constraints and acceptance criteria.
2. Create a GitHub issue that links the specification.
3. Apply labels: task type, subsystem (`area:*`) and `status:ready`.
4. Write the tests and record the expected failure (RED) before implementing.
5. Implement the behavior, pass the checks and link the PR to the issue (a closing keyword such as `Closes #N` in the PR description).

Never implement undocumented or untracked behavior. Reference the issue in commits. Record verification evidence and limitations in the issue. Do not claim Android support from desktop-only tests.

## Stack and boundaries
C++20, CMake/Ninja, Qt Quick/QML, Ceres and Eigen. OCCT is planned for a later modeling milestone. Keep the sketch model and solver independent of Qt and OCCT. Use stable entity IDs, millimeters, finite parameter validation, transactional solve/rollback and explicit tolerance checks. Least-squares convergence does not prove constraints are satisfied. DOF analysis must exclude drag/stabilization residuals.

## Style
Google C++ Style with snake_case functions and variables. Prefer small focused modules. No owning raw pointers. Tests: GoogleTest for core; Qt Test for UI. Canonical documentation lives in docs/modules/ROOT/pages/*.adoc, including architecture, ADRs, build instructions and plans. README is a short entry point; AGENTS.md is the agent entry point.

## Product
Android tablet-first parametric CAD, distributed using pay what you want. Public repository visibility does not select a software license. Do not add payment integrations or choose a license without a recorded project decision.
