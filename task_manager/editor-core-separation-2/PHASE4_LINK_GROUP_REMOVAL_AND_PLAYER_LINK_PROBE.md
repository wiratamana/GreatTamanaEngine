# PHASE4 — Remove the `RESCAN` Workaround; Add the Permanent Player-Link Probe

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it in full first, especially
Locked Design Decisions #6 and #7. This phase is the campaign's own proof
step: PHASE1-3 made the code changes; THIS phase is what mechanically
demonstrates they actually worked, permanently, not just "as of one manual
check".

## Step 1: The Goal (Where are we going?)

Two things, in this order:

1. **Remove `$<LINK_GROUP:RESCAN,gte_editor,gte_core>`** from both
   `CMakeLists.txt` (the `GreatTamanaEditor` executable) and
   `tests/CMakeLists.txt` (the `GreatTamanaEngineTests` executable),
   replacing each with a plain `target_link_libraries(... PRIVATE gte_editor)`.
   If PHASE1-3 genuinely closed every real `gte_core -> gte_editor`-only-
   symbol dependency, a plain, single-pass link MUST now succeed on its own
   — GNU `ld`'s default single-scan-per-archive behavior is only a problem
   when a real circular symbol need exists. If it does NOT succeed, that is
   this campaign's own immediate, unambiguous signal that PHASE2 or PHASE3
   missed something real — do not "fix" that by putting `RESCAN` back
   without first re-investigating why (see Step 4's troubleshooting note).
2. **Add a new, permanent, checked-in probe project,
   `tools/ci/gte_core_player_link_probe/`**, that proves — via a REAL
   executable link against `gte_core.a` alone, no `gte_editor` involved at
   all — that Rule 3 of the design doc's Four Hard Rules ("a thin
   Player-build host... can link `gte_core.a` alone") is mechanically true,
   forever, re-runnable by any future phase/campaign. This supplements (does
   NOT replace) the existing `tools/ci/gte_core_standalone_probe/`, which
   stays completely untouched — that probe proves gte_core builds as an
   ARCHIVE with zero `gte_editor` `#include`s; this NEW probe proves a
   `gte_core`-alone EXECUTABLE actually LINKS, which
   `editor-core-separation-1`'s own Phase 19 explicitly documented the old
   probe can never do (an archive build never resolves cross-translation-unit
   symbols at all).

## Step 2: The Situation (Where are we now?)

Re-read, in full, before starting:
- `tools/ci/gte_core_standalone_probe/CMakeLists.txt` and `README.md` — the
  exact, proven "nested `cmake -S <repo root> -B <private inner dir>
  -DGTE_CORE_STANDALONE_PROBE_ONLY=ON`" mechanism this phase's own new probe
  reuses verbatim (the SAME mechanism, NOT `add_subdirectory()` — that was
  already tried and found broken during `editor-core-separation-1` Phase 18
  because every `cmake/Fetch*.cmake` module hardcodes `${CMAKE_SOURCE_DIR}`,
  not `${CMAKE_CURRENT_SOURCE_DIR}` — do not attempt `add_subdirectory()`
  again, it will fail the exact same way).
- The root `CMakeLists.txt`'s own `GTE_CORE_STANDALONE_PROBE_ONLY` option
  declaration and both of its `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard
  blocks (the ImGui/ImGuizmo fetch block, and the "Final executable" block).
- `CMakeLists.txt`'s own `target_link_libraries(GreatTamanaEditor PRIVATE
  "$<LINK_GROUP:RESCAN,gte_editor,gte_core>")` line and its large explanatory
  comment above it.
- `tests/CMakeLists.txt`'s own matching
  `target_link_libraries(GreatTamanaEngineTests PRIVATE
  "$<LINK_GROUP:RESCAN,gte_editor,gte_core>" GTest::gtest GTest::gtest_main)`
  line and its comment.
- `src/Core/Core.h`'s public method list — confirm `Core::BuildFrame()`
  (or whichever public, non-inline, `.cpp`-defined method you choose) is
  still public and still defined in `Core.cpp` (not accidentally inlined) —
  needed for Step 3.3's link-forcing trick.
- `src/Game/RenderSystem.h`'s two `Draw()` overloads' exact current
  signatures (needed to disambiguate a member-function-pointer expression in
  Step 3.3 — an overloaded member function's address needs an explicit
  target type to resolve which overload you mean).

## Step 3: The Plan — exact steps

### Step 3.1 — Remove the `RESCAN` generator expression (do this FIRST, before adding the new probe, so a genuine remaining gap is caught by the SIMPLEST possible reproduction)

In `CMakeLists.txt`:
- Replace `target_link_libraries(GreatTamanaEditor PRIVATE
  "$<LINK_GROUP:RESCAN,gte_editor,gte_core>")` with
  `target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)`.
- Rewrite the large comment block above this line (currently explaining the
  Phase-9-era wrinkle and Phase 17's "this is now understood to be
  PERMANENT" correction) to instead state, as a fresh, self-contained fact
  (not a changelog): `gte_editor` is linked with a plain, single-pass
  `target_link_libraries()` call because `gte_core`'s own object files carry
  zero unresolved references to any `gte_editor`-only symbol — see
  `task_manager/editor-core-separation-2/PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md`
  and `PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md` for the two
  fixes that made this true, and
  `tools/ci/gte_core_player_link_probe/` for the permanent, mechanical proof.
  Per Rule 7 in this document's own conventions, do not phrase this as "used
  to be X, now it's Y" — state the current, correct fact plainly, as if
  writing it fresh.

In `tests/CMakeLists.txt`:
- Same replacement:
  `target_link_libraries(GreatTamanaEngineTests PRIVATE gte_editor GTest::gtest GTest::gtest_main)`.
- Same comment rewrite.

### Step 3.2 — Compile+link check BEFORE writing any new probe code

`cmake --build build --target GreatTamanaEditor` and
`cmake --build build --target GreatTamanaEngineTests`. **Both must succeed.**
If either fails with an `undefined reference` error, STOP — do not proceed
to Step 3.3. Re-read the exact undefined symbol name in the error. If it is
`gte::RecordFrameDebuggerDraws`, `gte::AddFrameDebuggerReplayPasses`,
`gte::Logger::Query`/`Clear`/`EntryCount`/`IsEnabled`/`LatestEntryId`, or
anything else clearly traceable to PHASE2/PHASE3's own intended fixes, that
means one of those phases' own conversion was incomplete (a call site this
strategy's file inventory missed, or a signature mismatch) — go back and fix
the ROOT CAUSE in the appropriate earlier phase's own files, do not paper
over it here by re-adding `RESCAN`. If it is something else entirely
(unrelated to this campaign), call `ask_questions` before deciding how to
proceed — it may be a pre-existing, unrelated issue this campaign should not
silently absorb responsibility for.

### Step 3.3 — Write the new probe's `main.cpp`

Create `tools/ci/gte_core_player_link_probe/main.cpp`. Its ONLY job is to
force the linker to pull `RenderSystem.cpp.obj`, `Core.cpp.obj`, and
`Network/NetworkServer.cpp.obj` out of `libgte_core.a` (each one carries at
least one of this campaign's own fixed call sites, or — for `NetworkServer.cpp`
— is the one file PHASE3 fixed), WITHOUT needing to fully, safely construct
a real windowed `Core` (which would need a real `ISurfaceProvider`/Vulkan
instance this tiny probe has no business standing up). Use
member-function-pointer expressions to force linkage without ever calling
anything:

```cpp
// tools/ci/gte_core_player_link_probe/main.cpp
//
// editor-core-separation-2 campaign, PHASE4 - a tiny, PERMANENT, checked-in
// program whose only job is to be linked against libgte_core.a ALONE (never
// gte_editor, never SDL, never ImGui) and have that link genuinely SUCCEED.
// See tools/ci/gte_core_player_link_probe/README.md for the full "why" and
// the exact command to run this by hand.
//
// This program is never actually EXECUTED as part of the probe (see
// README.md) - only COMPILED and LINKED. It forces the linker to pull
// RenderSystem.cpp.obj, Core.cpp.obj, and Network/NetworkServer.cpp.obj out
// of libgte_core.a's own archive (each one carries at least one of this
// campaign's own fixed gte_core -> gte_editor call sites) by taking the
// ADDRESS of one real, public, .cpp-defined method from each - forcing the
// WHOLE containing .o file to be extracted from the archive and its every
// remaining internal reference to be resolved, without this probe needing
// to safely, fully CONSTRUCT any of these (Core in particular needs a real
// ISurfaceProvider/Vulkan instance this tiny probe has no business standing
// up) or ever calling anything that could crash.

#include "Core/Core.h"
#include "Game/RenderSystem.h"
#include "Network/NetworkServer.h"

#include <optional>
#include <unordered_set>

int main()
{
    // Forces Core.cpp.obj to be pulled from libgte_core.a - proves
    // Core::BuildFrame()'s own internal call (formerly the free function
    // gte::AddFrameDebuggerReplayPasses(), gte_editor-only) has no
    // remaining undefined reference. Never called - see this file's own
    // header comment.
    void (gte::Core::*forceLinkCore)() = &gte::Core::BuildFrame;
    (void)forceLinkCore;

    // Forces RenderSystem.cpp.obj to be pulled from libgte_core.a - proves
    // RenderSystem::Draw()'s own internal call (formerly the free function
    // gte::RecordFrameDebuggerDraws(), gte_editor-only) has no remaining
    // undefined reference. The explicit member-function-pointer TYPE below
    // disambiguates which of RenderSystem::Draw()'s two overloads this
    // refers to (the float-aspect overload) - re-confirm this signature
    // against the real, current RenderSystem.h before relying on it; update
    // it here if that header's signature has since changed. Never called.
    using DrawAspectOverload = void (gte::RenderSystem::*)(gte::Registry&, gte::Renderer&, float,
        gte::IFrameDebuggerCaptureRecorder*, std::optional<std::size_t>, const std::unordered_set<gte::Entity>&);
    DrawAspectOverload forceLinkRenderSystem = &gte::RenderSystem::Draw;
    (void)forceLinkRenderSystem;

    // Forces Network/NetworkServer.cpp.obj to be pulled from
    // libgte_core.a - proves NetworkServer's own GET /get_logs/
    // POST /clear_logs route registration (formerly direct calls to
    // gte::Logger::Query()/Clear()/etc., gte_editor-only) has no remaining
    // undefined reference. Safe to actually CONSTRUCT (unlike Core) -
    // NetworkServer's constructor only registers httplib route handlers, it
    // never calls Start() (no socket is ever opened) and needs no exotic
    // interface fixture.
    gte::Network::NetworkServer networkServer;
    (void)networkServer;

    return 0;
}
```

Cross-check `RenderSystem::Draw()`'s REAL, current signature (both
overloads) against `RenderSystem.h` before finalizing
`DrawAspectOverload`'s exact parameter list — if PHASE2 changed the
`capture` parameter's type to `IFrameDebuggerCaptureRecorder*` exactly as
planned, this should match; if any OTHER parameter has drifted since this
strategy was written (e.g. a new trailing parameter added by an unrelated,
concurrent campaign), update this type alias to match reality.

### Step 3.4 — Wire the new executable into the root `CMakeLists.txt`, gated `GTE_CORE_STANDALONE_PROBE_ONLY`

Immediately after `gte_core`'s own `target_link_libraries(gte_core PRIVATE
saba_pmx)` line (i.e. right before the existing
`if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) # --- Final executable --- ...`
block — NOT the EARLIER `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) # --- Editor
library --- ...` block further up the same file, which is a SEPARATE guarded
region that already closed with its own `endif()` well before this point;
re-confirm both exact line numbers with `search_in_dir` for
`"--- Editor library ---"` and `"--- Final executable ---"` before editing,
since this file has TWO separate `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)`
blocks and only the SECOND one — "Final executable" — immediately follows
the `saba_pmx` line), add a NEW block:

```cmake
# editor-core-separation-2 campaign, PHASE4 - the REAL, mechanical proof
# that gte_core.a links standalone (design doc Section 1.3, Rule 3) - a
# genuinely separate concern from GTE_CORE_STANDALONE_PROBE_ONLY's own
# original job (skipping gte_editor/the main executable/tests so
# tools/ci/gte_core_standalone_probe/ can build ONLY the gte_core ARCHIVE).
# This executable target only exists in THIS exact configuration
# (GTE_CORE_STANDALONE_PROBE_ONLY=ON) - it is never part of a normal
# `cmake -S . -B build` configure, exactly like gte_editor/GreatTamanaEditor
# are never part of a GTE_CORE_STANDALONE_PROBE_ONLY=ON configure (the
# inverse of the guard immediately below this block). See
# tools/ci/gte_core_player_link_probe/README.md for the exact command to
# build+link this by hand.
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    add_executable(gte_core_player_link_probe tools/ci/gte_core_player_link_probe/main.cpp)
    target_link_libraries(gte_core_player_link_probe PRIVATE gte_core)
endif()
```

Do not place this INSIDE the existing `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)`
block (that would be a contradiction — this new target must exist ONLY WHEN
that flag IS on, the exact opposite condition).

### Step 3.5 — The new probe project folder

Create `tools/ci/gte_core_player_link_probe/CMakeLists.txt`, closely
mirroring `tools/ci/gte_core_standalone_probe/CMakeLists.txt`'s own proven
nested-invocation mechanism (re-read that file in full immediately before
writing this one — copy its structure, adjust only what must differ):

```cmake
cmake_minimum_required(VERSION 3.19)

# editor-core-separation-2 campaign, PHASE4 - a tiny, MANUALLY-INVOCABLE,
# CI-only local CMake project that configures the REAL repository root
# CMakeLists.txt with GTE_CORE_STANDALONE_PROBE_ONLY=ON (exactly like
# tools/ci/gte_core_standalone_probe/ already does - see that project's own
# CMakeLists.txt for the full "why nested cmake, not add_subdirectory()"
# reasoning, which applies identically here) and then builds ONE MORE
# target that ONLY exists in that configuration:
# gte_core_player_link_probe - a tiny executable LINKED AGAINST gte_core.a
# ALONE. Unlike tools/ci/gte_core_standalone_probe/ (which only proves
# gte_core BUILDS AS AN ARCHIVE with zero gte_editor #include - an archive
# build never resolves cross-translation-unit symbols at all, so it cannot
# catch an undefined-reference-style violation), THIS probe's own build
# step is a REAL EXECUTABLE LINK - it fails immediately, mechanically, if
# gte_core's own object files carry any unresolved reference to a symbol
# only gte_editor.a defines. See README.md (this same folder).
project(GteCorePlayerLinkProbe LANGUAGES NONE)

get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gte_core_inner_build")

add_custom_target(gte_core_player_link_probe ALL
    COMMAND "${CMAKE_COMMAND}"
            -S "${GTE_REPO_ROOT}"
            -B "${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
            -G Ninja
            "-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
            "-DGTE_BUILD_TESTS=OFF"
    COMMAND "${CMAKE_COMMAND}"
            --build "${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
            --target gte_core_player_link_probe
    COMMENT "Configuring + LINKING gte_core_player_link_probe against gte_core ALONE (zero gte_editor/SDL/ImGui) in ${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
    VERBATIM
    USES_TERMINAL
)
```

Create `tools/ci/gte_core_player_link_probe/README.md`, mirroring
`tools/ci/gte_core_standalone_probe/README.md`'s own structure exactly
(What this is / How it works / Exact command to run this by hand / What a
successful run proves / What this probe deliberately does NOT do), adjusted
for the fact this one proves a real LINK, not just an archive build, and
explicitly cross-referencing the existing probe as a sibling, not a
replacement. The exact command to document:
```
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
cmake --build build-player-link-probe
```

### Step 3.6 — Do NOT touch `tools/ci/gte_core_standalone_probe/` at all in this phase

Re-confirm via `git status`/`git diff` at the end of this phase that
`tools/ci/gte_core_standalone_probe/`'s two files are byte-for-byte
unchanged. It remains valid, useful, and untouched.

## Step 4: Compile check

1. Run the existing probe first, as a sanity baseline (it must still pass
   exactly as before — this phase changes nothing it depends on). **Do NOT
   pass `--target gte_core`** — the outer meta-project (`LANGUAGES NONE`)
   only ever defines ONE real CMake target, the custom target
   `gte_core_standalone_probe` itself (its own `ALL`-default target), which
   is what nested-invokes the real, inner
   `-DGTE_CORE_STANDALONE_PROBE_ONLY=ON` build and builds ITS OWN "gte_core"
   target internally — there is no target literally named "gte_core"
   reachable from this OUTER build directory at all; passing
   `--target gte_core` here fails with an "unknown target" error. Just run
   the plain default build (or pass `--target gte_core_standalone_probe`
   explicitly, if you prefer to be explicit rather than relying on the
   default `ALL` target):
   ```
   cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe
   cmake --build build-core-probe
   ```
2. Run the NEW probe:
   ```
   cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe
   cmake --build build-player-link-probe
   ```
   **This must succeed with a real, final link of `gte_core_player_link_probe`**
   (confirm the executable file actually exists afterward, e.g. via
   `browse_dir` on the inner build directory). If it fails with an
   `undefined reference`, this is a genuine, real regression signal — do not
   silently work around it in this probe's own CMakeLists.txt; go back and
   find the real, remaining gap in `gte_core`'s own source (most likely a
   call site PHASE2/PHASE3's own file inventory missed — re-run
   `search_in_dir(src, "Editor/")` scoped to non-`src/Editor/` files as a
   final sweep).

   **Troubleshooting note**: if the FIRST failure you hit here is an
   unrelated compile error inside `main.cpp` itself (a type mismatch, a
   missing `#include`, an ambiguous overload) rather than a genuine linker
   `undefined reference`, that is this probe's OWN bug, not a regression in
   `gte_core` — fix `main.cpp` and retry before concluding anything about
   `gte_core` itself.
3. Re-run the normal, full build once more
   (`cmake --build build --target GreatTamanaEditor` and
   `--target GreatTamanaEngineTests`) to reconfirm Step 3.2's earlier
   success still holds after this phase's own CMakeLists.txt edits.
4. `run_app_background` the real `GreatTamanaEditor.exe` one more time,
   `gte_send_request` a `GET /get_swapchain` to confirm the whole engine
   still renders correctly end-to-end after the `RESCAN` removal.
   `stop_app_background` when done.

## Step 5: Wrap-up

- Write `PHASE4_COMPLETION_REPORT.md`: the exact `RESCAN`-removal diff, the
  new probe's final file contents, both probes' run results (paste the real
  terminal output/exit status), and the full-build re-confirmation result.
  If the `RESCAN` removal did NOT succeed on the first try, document exactly
  what was still broken and how you found/fixed it — this is valuable,
  honest history for PHASE5's own final closeout, do not omit a rocky path
  if there was one.
- `git_add` + `git_commit`.
- Call `ask_questions` for any genuine ambiguity — in particular, if the
  member-function-pointer trick in Step 3.3 does not compile against the
  REAL current signatures for a reason this document didn't anticipate, ask
  before inventing a substantially different mechanism.
