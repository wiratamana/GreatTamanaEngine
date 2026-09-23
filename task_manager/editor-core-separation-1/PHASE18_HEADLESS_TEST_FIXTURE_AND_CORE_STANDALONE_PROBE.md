# PHASE18 — Headless `ISurfaceProvider` Test Fixture + Standalone-Core Probe

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 17. See `PHASE0`'s "Locked
Design Decision #5" (manually-invocable local probe, no real CI exists in
this repo).

## Step 1: The Goal

Two new, permanent regression-safety pieces:
1. A fake/headless `ISurfaceProvider` implementation, used only by tests,
   proving `Core` can be constructed and driven without any real
   `Window`/SDL involved at all.
2. A separate, tiny, MANUALLY-INVOCABLE local CMake project
   (`tools/ci/gte_core_standalone_probe/CMakeLists.txt`) that configures
   ONLY `gte_core` — no `gte_editor`, no main executable, nothing else —
   proving `gte_core` genuinely builds and links standalone. This is the
   ONLY place in the whole project where `gte_editor` is intentionally not
   configured; it must never appear in the main `CMakeLists.txt` as a
   user-facing option.

## Step 2: The Situation / The Problem

This is the ONLY mechanism left, after this campaign, that can still catch
a FUTURE accidental `gte_core` → `gte_editor` dependency leaking back in —
this repo's own normal build always links both together (since the
executable always links `gte_editor`), so a normal build would silently
keep working even if such a leak existed. Read design doc Section 7.3 for
the full reasoning.

## Step 3: The Plan

1. Create `tests/Fakes/HeadlessSurfaceProvider.h` (new, Tier-1-testable,
   pure): a trivial `ISurfaceProvider` implementation returning fixed/fake
   values for `VulkanInstanceExtensions()`/`Width()`/`Height()`, and either
   a genuinely-null/no-op `CreateVulkanSurface()` (if `Core`'s construction
   path can tolerate never actually presenting), or clearly documented as
   "construction-only, never call `Present()` through this fixture" if a
   real Vulkan surface is unavoidable for full construction. Determine
   which is actually true by reading `Core`'s real constructor body
   (post-Phase-12/13) — do not assume either way.
2. Add a new Tier-1 test file, e.g. `tests/Core/CoreHeadlessConstructionTests.cpp`,
   that constructs `Core` using `HeadlessSurfaceProvider` and a trivial
   `IHostServices` stub, and confirms it constructs without crashing and
   that its plain accessors (`GetRegistry()`, `GetGame()`, etc.) return
   usable references. Deliberately never calls `Core::SetEditorLayerHook()`
   at all — this test's whole point is proving `Core` is fully usable with
   that hook left at its default `nullptr`, exactly like a real future
   Player host would leave it, so every render-graph-frame-building branch
   behind it (Phase 13) must degrade to a safe no-op, not a crash. This
   does NOT need to render an actual frame — it proves CONSTRUCTION works
   headless, which is the load-bearing claim Rule 3 (design doc Section
   1.3) makes about a future Player host.
3. Create `tools/ci/gte_core_standalone_probe/CMakeLists.txt` — a minimal,
   separate CMake project that adds `gte_core`'s own `CMakeLists.txt`
   (e.g. via `add_subdirectory` pointing back at the real source tree, or
   by duplicating just the `gte_core` target definition — pick whichever
   is less likely to silently drift out of sync with the real
   `CMakeLists.txt`; a subdirectory include of the SAME root project,
   configured with a flag that skips defining `gte_editor`/the executable/
   tests, is likely cleaner than hand-duplicating the source list — decide
   during execution and document the choice).
4. Document, in a short `tools/ci/gte_core_standalone_probe/README.md`, the
   EXACT command to manually invoke this probe (e.g.
   `cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe && cmake --build build-core-probe`),
   and what a successful run proves.
5. Actually RUN the probe once, right now, to confirm it genuinely
   succeeds — this is not "add tooling and assume it works," it must be
   proven to actually build `gte_core` standalone successfully in this
   phase.
6. Compile-check: incremental build of the main repo (confirm the new
   Tier-1 test file compiles and passes via a scoped `ctest -R
   CoreHeadlessConstruction` run — not a full suite, this is a brand-new
   test).

## Files Touched

- NEW `tests/Fakes/HeadlessSurfaceProvider.h`
- NEW `tests/Core/CoreHeadlessConstructionTests.cpp`
- NEW `tools/ci/gte_core_standalone_probe/CMakeLists.txt`,
  `tools/ci/gte_core_standalone_probe/README.md`
- `tests/CMakeLists.txt` (register the new test file)

## Definition of Done

- The headless Tier-1 test passes.
- The standalone-core probe has been run manually at least once in this
  phase and confirmed to succeed, building `gte_core` alone with zero
  `gte_editor`/SDL/ImGui involvement.
- `PHASE18_COMPLETION_REPORT.md` (including the exact probe invocation
  command and its output) + git commit.

## Out of Scope

Do not wire this into a real CI pipeline (none exists — Locked Decision
#5). Do not build any part of the actual Player Build Pipeline.
