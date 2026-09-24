# `gte_core` Player-Link Probe

## What this is

A tiny, **manually-invocable** local CMake project. It is NOT a real CI
pipeline (this repository has none of its own - only vendored
`third_party/**/.github` workflows exist, which belong to third-party
dependencies, not this project - see `PHASE0_MASTER_STRATEGY.md`'s Locked
Design Decision #5). It configures the real repository root `CMakeLists.txt`
with `GTE_CORE_STANDALONE_PROBE_ONLY=ON` (exactly the same mechanism
`tools/ci/gte_core_standalone_probe/` already uses - see that project's own
`CMakeLists.txt`/`README.md` for the full "why nested cmake, not
`add_subdirectory()`" reasoning, which applies here identically) and builds
ONE target that only exists in that exact configuration:
`gte_core_player_link_probe` - a tiny executable **linked against `gte_core.a`
alone** (no `gte_editor`, no SDL3, no ImGui at all).

This is a sibling of `tools/ci/gte_core_standalone_probe/`, not a
replacement. That probe proves `gte_core` **builds as an archive** with zero
`gte_editor` `#include`s - an archive build never resolves cross-translation-
unit symbols at all, so it is structurally incapable of catching an
undefined-reference-style violation (see that probe's own README.md, and
`editor-core-separation-1`'s own Phase 19 closeout, for why). THIS probe's
own build step is a REAL, FINAL EXECUTABLE LINK - it fails immediately,
mechanically, with a genuine linker `undefined reference` error, the moment
any of `gte_core`'s own object files carries an unresolved reference to a
symbol only `gte_editor.a` defines. This is exactly the mechanism design doc
Section 1.3's Rule 3 ("a thin Player-build host... can link `gte_core.a`
alone and get a running, renderable engine") requires, made permanent and
re-runnable by any future phase/campaign, instead of being a one-off,
never-committed manual experiment (as `editor-core-separation-1`'s own Phase
19 originally was).

## How it works

`main.cpp` `#include`s three real `gte_core` headers (`Core/Core.h`,
`Game/RenderSystem.h`, `Network/NetworkServer.h`) and forces the linker to
pull `Core.cpp.obj`, `RenderSystem.cpp.obj`, and `Network/NetworkServer.cpp.obj`
out of `libgte_core.a`'s own archive - each one carries at least one of the
real `gte_core -> gte_editor`-only call sites this campaign's PHASE2/PHASE3
fixed:

- `void (gte::Core::*)() = &gte::Core::BuildFrame;` - a member-function-
  pointer expression, that forces `Core.cpp.obj` to be extracted and every
  one of its own remaining internal references resolved (proves
  `Core::BuildFrame()`'s own internal call, formerly the free function
  `gte::AddFrameDebuggerReplayPasses()`, has no remaining undefined
  reference). This expression alone never CONSTRUCTS a `gte::Core` - its own
  job is purely the LINK-time proof (see the PHASE5 bonus check below for
  where a real `Core` genuinely IS constructed).
- An explicitly-typed member-function-pointer expression for
  `&gte::RenderSystem::Draw` (the float-aspect overload - `RenderSystem::Draw()`
  has two overloads, so an explicit target type is required to disambiguate
  which one is meant), also never called, forcing `RenderSystem.cpp.obj` to
  be extracted (proves `RenderSystem::Draw()`'s own internal call, formerly
  the free function `gte::RecordFrameDebuggerDraws()`, has no remaining
  undefined reference).
- A real, actually-constructed `gte::Network::NetworkServer networkServer;`
  (safe to construct, unlike `Core` - its constructor only registers
  `httplib` route handlers, it never calls `Start()`, so no socket is ever
  opened, and needs no exotic interface fixture) - forces
  `Network/NetworkServer.cpp.obj` to be extracted (proves `GET /get_logs`/
  `POST /clear_logs`'s route registration, formerly direct calls to
  `gte::Logger::Query()`/`Clear()`/etc., has no remaining undefined
  reference).

None of these three forced-link mechanisms ever actually RUN any of this
logic - they only take addresses / construct a socket-less server. The only
thing being tested BY THEM is whether the FINAL LINK STEP succeeds.

## editor-core-separation-3 campaign, PHASE5 - a fourth, genuinely EXECUTED bonus check

`PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md`, Step 3.2 added a bonus
check to this same `main()`, appended after the three forced-link lines
above: a real, headless `gte::Core` is constructed (via
`tests/Fakes/HeadlessSurfaceProvider.h`, the same `VK_EXT_headless_surface`
mechanism `CoreHeadlessConstructionTests.cpp` already uses), wrapped in a
`try`/`catch` that self-skips, loudly, with a clear message, on any machine
whose Vulkan driver lacks that extension - mirroring
`CoreHeadlessConstructionTests.cpp`'s own established precedent exactly
(there is no separate boolean "is this supported" predicate anywhere in this
codebase - the skip signal IS the constructor throwing). When it does not
self-skip, it calls `Core::LoadPlugins()` against this probe's own real,
shared `plugins/` folder and `Core::BuildFrame()` once, confirming neither
crashes.

**This means `gte_core_player_link_probe.exe` is now sometimes worth actually
RUNNING, not just linking** - unlike the original three checks above (which
only ever prove something at LINK time and do nothing observable if
executed), this fourth check only ever does anything when the resulting
`.exe` is genuinely executed. Either outcome (bonus check `PASS` or a clean,
documented `SKIPPED`) is an acceptable result - only a genuine crash/hang is
a failure.

## Exact command to run this by hand

From the repository root:

```
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
cmake --build build-player-link-probe
build-player-link-probe\gte_core_inner_build\gte_core_player_link_probe.exe
```

(there is no target literally named `gte_core_player_link_probe` reachable
from the OUTER build directory besides its own custom target of that exact
name, which is the `ALL`-default target - a plain `cmake --build
build-player-link-probe` builds it, no `--target` needed, though passing
`--target gte_core_player_link_probe` explicitly also works. The third line -
actually running the built `.exe` - is now meaningful too, per the PHASE5
bonus check above; it was previously only ever built/linked, never run.)

## What a successful run proves

- `gte_core.a` (`libgte_core.a` on this toolchain) links into a real,
  standalone EXECUTABLE with zero `gte_editor` involvement, zero ImGui, zero
  SDL3 - design doc Section 1.3's Rule 3, mechanically, not just "as of one
  manual phase-19 experiment".
- Every real `gte_core -> gte_editor`-only-symbol call site this campaign
  (PHASE2's `IFrameDebuggerCaptureRecorder`, PHASE3's `ILogQueryCapability`)
  fixed stays fixed, forever, re-checked on every future run of this probe -
  a regression here would fail this probe's own build step immediately with
  a genuine `undefined reference` linker error, the same class of failure
  `editor-core-separation-1`'s own Phase 19 throwaway probe once reproduced.
- (PHASE5) When actually EXECUTED, and this machine's Vulkan driver supports
  `VK_EXT_headless_surface`: a real, headless `gte::Core` can be constructed,
  load every plugin `.dll` in the shared `plugins/` folder, and run
  `BuildFrame()` once, with no crash - the closest this probe can get to "the
  runtime-tier plugin's render-graph pass renders correctly in the Player
  probe too" without a real window/swapchain to screenshot.

## What this probe deliberately does NOT do

- It does not build any part of the actual Player Build Pipeline (design doc
  Section 8) - that is a separate, later initiative, explicitly out of scope
  for this entire campaign (see `PHASE0_MASTER_STRATEGY.md`'s "Non-Goals").
- It does not replace `tools/ci/gte_core_standalone_probe/`, which remains
  completely untouched and still correctly catches a different class of bug
  (an accidental `#include` violation, e.g. `gte_core` `#include`-ing
  `<imgui.h>` directly) that an archive build alone can still catch.
- It is not wired into any GitHub Actions workflow or other real CI system -
  none exists in this repository (Locked Design Decision #5). Run it by
  hand, as documented above, whenever you want to re-confirm `gte_core.a`
  still links standalone (and, per PHASE5, that a headless `Core` still
  loads plugins/builds a frame without crashing).
- It never appears as a user-facing option inside the main build - do not
  set `GTE_CORE_STANDALONE_PROBE_ONLY` manually in a normal
  `cmake -S . -B build` configure.
