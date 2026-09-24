# PHASE4 — COMPLETION REPORT: `RESCAN` Removal + Permanent Player-Link Probe

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE4_LINK_GROUP_REMOVAL_AND_PLAYER_LINK_PROBE.md` exactly, with no
deviation and — notably — **no rocky path**: both `RESCAN` removals linked
successfully on the FIRST attempt, and the new probe linked successfully on
the FIRST attempt too. This is the honest, best-case outcome the campaign was
hoping for, proving PHASE2/PHASE3 genuinely closed every real
`gte_core -> gte_editor`-only-symbol dependency.

## Step 0: Pre-existing completion reports

Read `PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`, and
`PHASE3_COMPLETION_REPORT.md` in full before starting.

- PHASE1: pure `EditorPanelCatalog.h` relocation, zero ambiguity, zero
  deviation.
- PHASE2: `IFrameDebuggerCaptureRecorder` interface (closes Defects A and B).
  One real, self-introduced `namespace gte { #include ... }` nesting bug,
  found and fixed by its own mandatory compile check. Final interface shape
  pasted in full in that report — cross-checked against the real, current
  `src/Core/FrameDebuggerCaptureRecorder.h` before writing this phase's own
  probe `main.cpp` (confirmed byte-for-byte unchanged since PHASE2 landed).
- PHASE3: `ILogQueryCapability`/`EditorLogQueryCapability` (closes Defect C).
  One real, necessary deviation (`s_editorLogQueryCapability` had to be a
  namespace-scope static, not function-local, because `NetworkServer`'s
  member-initializer list needs its address before the constructor body
  runs). Confirmed `NetworkServer`'s constructor real, current signature
  (six parameters, all defaulted) directly against `src/Network/NetworkServer.h`
  before writing this phase's own probe `main.cpp` — matches PHASE3's own
  report exactly, no drift.

No carry-forward clue from any of the three reports changed this phase's own
plan — all three phases explicitly stated the `$<LINK_GROUP:RESCAN,...>`
workaround was left untouched, exactly as expected, with removal being this
phase's own job.

## Step 1: Re-confirmation against real, current source (before editing)

- `search_in_dir(repo root, "LINK_GROUP:RESCAN")` → confirmed the two real,
  current occurrences: `CMakeLists.txt` line 999
  (`target_link_libraries(GreatTamanaEditor PRIVATE "$<LINK_GROUP:RESCAN,gte_editor,gte_core>")`)
  and `tests/CMakeLists.txt` line 2293
  (`"$<LINK_GROUP:RESCAN,gte_editor,gte_core>"` inside
  `target_link_libraries(GreatTamanaEngineTests PRIVATE ...)`).
- `search_in_dir(repo root, "--- Editor library ---")` → confirmed this guard
  block starts at `CMakeLists.txt` line 663 and closes with its own
  `endif()` at line 871 — well before the `saba_pmx` line.
- `search_in_dir(repo root, "--- Final executable ---")` → confirmed this
  SECOND, separate `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard block
  starts at `CMakeLists.txt` line 958, immediately after
  `target_link_libraries(gte_core PRIVATE saba_pmx)` at line 950 — exactly
  the insertion point the phase doc's own Step 3.4 warning identified. The
  new probe-wiring block was inserted between these two lines (950 and 958),
  never inside the earlier "Editor library" block.
- Confirmed `src/Game/RenderSystem.h`'s two real, current `Draw()` overload
  signatures (lines 208-210 and 234-236) — the float-aspect overload's
  parameter list matches the phase doc's own predicted
  `DrawAspectOverload` type EXACTLY, byte for byte:
  `void Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight, IFrameDebuggerCaptureRecorder* capture = nullptr, std::optional<std::size_t> maxDrawCount = std::nullopt, const std::unordered_set<Entity>& batchedEntities = {});`
  — no signature drift since PHASE2 landed.
- Confirmed `src/Network/NetworkServer.h`'s real, current constructor
  signature (lines 124-129) — six parameters, all defaulted to `nullptr`,
  matching PHASE3's own report exactly:
  `explicit NetworkServer(FrameCaptureBridge* = nullptr, EngineCommandBridge* = nullptr, EditorUiCommandBridge* = nullptr, FrameDebuggerCommandBridge* = nullptr, AssetImportCommandBridge* = nullptr, ILogQueryCapability* = nullptr);`
  — confirms `gte::Network::NetworkServer networkServer;` (all-default
  construction) compiles and is safe (registers httplib routes only, never
  calls `Start()`).
- Confirmed `src/Core/Core.h`'s `Core::BuildFrame()` (line 112) is still
  `public`, still declared with no parameters, still defined in `Core.cpp`
  (not inlined) — `class Core` opens at line 92, `public:` at line 93,
  `void BuildFrame();` at line 112, inside the same public section as
  `Update()`/`Present()`.
- Confirmed `NetworkServer`'s real namespace nesting:
  `namespace gte::Network { ... }` (line 52 of `NetworkServer.h`) — so the
  probe's construction is `gte::Network::NetworkServer`, not
  `gte::NetworkServer`.
- Re-read `tools/ci/gte_core_standalone_probe/CMakeLists.txt` and
  `README.md` in full before writing the new probe's own files, copying its
  proven nested-`cmake`-invocation structure (not `add_subdirectory()`,
  confirmed already broken for this repo's own `Fetch*.cmake` modules, per
  that project's own header comment).

No line-number drift was found anywhere — every location the phase doc
predicted matched the real, current source exactly.

## Step 2: `$<LINK_GROUP:RESCAN,...>` removal — the exact diff

### `CMakeLists.txt`

**Before** (lines 960-999, i.e. the large PHASE9-era rationale comment plus
the `RESCAN` link line):

```cmake
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
# Rule 4) - links gte_editor, NEVER gte_core directly; gte_editor's own
# PUBLIC dependency on gte_core (above) brings it in transitively. There is
# no configuration of this repo's own CMakeLists.txt in which this
# executable links gte_core alone.
#
# REAL LINKER WRINKLE, DISCOVERED DURING PHASE 9 OF editor-core-separation-1
# (documented here, not silently worked around): ... [Phase 9/17-era history,
# ~35 lines, ending with:]
#
# UPDATE, PHASE17 (...): ... this `$<LINK_GROUP:RESCAN,...>` is therefore a
# permanent fixture of this build, not a temporary Phase-9-era workaround.
target_link_libraries(GreatTamanaEditor PRIVATE "$<LINK_GROUP:RESCAN,gte_editor,gte_core>")
```

**After** (real, current, self-contained fact, not a changelog — per the
phase doc's own Step 3.1 instruction not to phrase it as "used to be X, now
it's Y"):

```cmake
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
# Rule 4) - links gte_editor, NEVER gte_core directly; gte_editor's own
# PUBLIC dependency on gte_core (above) brings it in transitively. There is
# no configuration of this repo's own CMakeLists.txt in which this
# executable links gte_core alone.
#
# gte_editor is linked with a plain, single-pass target_link_libraries()
# call because gte_core's own object files carry zero unresolved references
# to any gte_editor-only symbol - see
# task_manager/editor-core-separation-2/PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md
# and PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md for the two
# fixes that made this true, and tools/ci/gte_core_player_link_probe/ for
# the permanent, mechanical proof (a real executable link against gte_core.a
# alone, no gte_editor involved at all).
target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)
```

### `tests/CMakeLists.txt`

**Before** (lines 2278-2296):

```cmake
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
# Step 9) - links gte_editor (never gte_core directly, matching
# GreatTamanaEditor's own Rule-4 precedent above): Editor/*Tests.cpp above
# (EditorCameraTests.cpp, LoggerTests.cpp, FrameDebuggerDataTests.cpp, ...)
# needs symbols that now permanently live in gte_editor's own archive.
# gte_editor's own PUBLIC dependency on gte_core brings it in transitively.
#
# Same real, discovered-during-Phase-9 linker wrinkle as the root
# CMakeLists.txt's own GreatTamanaEditor target (see that file's own
# extensive comment right above its matching target_link_libraries() call) -
# gte_core's own RenderSystem.cpp/NetworkServer.cpp still
# call several gte_editor-only symbols directly (RecordFrameDebuggerDraws(),
# Logger::Query()/Clear(), ...) - a real, permanent (not just "until Phase 13")
# `$<LINK_GROUP:RESCAN,...>` treatment, not just a plain gte_editor link.
target_link_libraries(GreatTamanaEngineTests PRIVATE
    "$<LINK_GROUP:RESCAN,gte_editor,gte_core>"
    GTest::gtest
    GTest::gtest_main
)
```

**After**:

```cmake
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
# Step 9) - links gte_editor (never gte_core directly, matching
# GreatTamanaEditor's own Rule-4 precedent above): Editor/*Tests.cpp above
# (EditorCameraTests.cpp, LoggerTests.cpp, FrameDebuggerDataTests.cpp, ...)
# needs symbols that now permanently live in gte_editor's own archive.
# gte_editor's own PUBLIC dependency on gte_core brings it in transitively.
#
# gte_editor is linked with a plain, single-pass target_link_libraries()
# call because gte_core's own object files carry zero unresolved references
# to any gte_editor-only symbol - see
# task_manager/editor-core-separation-2/PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md
# and PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md for the two
# fixes that made this true, and tools/ci/gte_core_player_link_probe/ for
# the permanent, mechanical proof (a real executable link against gte_core.a
# alone, no gte_editor involved at all).
target_link_libraries(GreatTamanaEngineTests PRIVATE
    gte_editor
    GTest::gtest
    GTest::gtest_main
)
```

## Step 3: Compile+link check BEFORE writing any new probe code (Step 3.2 of the phase doc)

```
cmake --build build --target GreatTamanaEditor
```
**Result: SUCCESS, first try.** Only a relink was needed (no `.cpp` source
changed by this phase, only `CMakeLists.txt`'s own link-line/comment) —
`ninja` output: `[1/2] Linking CXX executable GreatTamanaEditor.exe;
Staging ... .spv ...; Copying SDL3.dll next to GreatTamanaEditor`. Zero
`undefined reference` errors.

```
cmake --build build --target GreatTamanaEngineTests
```
**Result: SUCCESS, first try.** `ninja` output: `[1/1] Linking CXX
executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to
GreatTamanaEngineTests`. Zero `undefined reference` errors.

**No rocky path here** — this is the single strongest, most direct
mechanical confirmation that PHASE2's `IFrameDebuggerCaptureRecorder`
conversion and PHASE3's `ILogQueryCapability` conversion each closed every
real call site: a plain, single-pass GNU `ld` link (no `RESCAN`) resolved
`gte::RecordFrameDebuggerDraws`/`gte::AddFrameDebuggerReplayPasses`/
`gte::Logger::Query`/`Clear`/`EntryCount`/`IsEnabled`/`LatestEntryId` with
zero remaining undefined references, on the first attempt.

## Step 4: The new probe's final files (pasted in full)

### `tools/ci/gte_core_player_link_probe/main.cpp`

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
    // refers to (the float-aspect overload) - re-confirmed against the real,
    // current RenderSystem.h (lines 208-210) before finalizing this probe;
    // update it here if that header's signature has since changed. Never
    // called.
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

This matches the phase doc's own required shape byte-for-byte — no
signature drift meant no changes were needed relative to the doc's own
pasted example.

### `tools/ci/gte_core_player_link_probe/CMakeLists.txt`

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

This matches the phase doc's own required shape byte-for-byte too.

### `tools/ci/gte_core_player_link_probe/README.md`

Mirrors `tools/ci/gte_core_standalone_probe/README.md`'s own structure
(What this is / How it works / Exact command to run this by hand / What a
successful run proves / What this probe deliberately does NOT do), explicitly
cross-referencing the existing probe as a sibling, not a replacement, and
explaining the member-function-pointer forced-linkage mechanism and why
`NetworkServer` alone is safe to actually construct. Full content committed
verbatim in the repo at that path (not re-pasted here in full — see the file
itself; it is ~85 lines and its exact prose is not load-bearing for this
report, unlike the two CMake/`.cpp` files above whose exact bytes matter for
the mechanical proof).

### Root `CMakeLists.txt` — new wiring block (Step 3.4)

Inserted immediately after `target_link_libraries(gte_core PRIVATE saba_pmx)`
(confirmed real line 950) and immediately before the comment/`if(NOT
GTE_CORE_STANDALONE_PROBE_ONLY)` guard that opens the "--- Final executable
---" block (confirmed real line 958) — NOT inside the earlier "--- Editor
library ---" guard block (confirmed that block's own `if`/`endif()` pair is
lines 662/871, fully closed well before this insertion point):

```cmake
target_link_libraries(gte_core PRIVATE saba_pmx)

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

Matches the phase doc's own required shape byte-for-byte.

### `.gitignore` — new entry (not explicitly mandated by the phase doc, but
necessary for cleanliness, mirroring the existing precedent)

The new probe's own private, nested inner build directory
(`build-player-link-probe/`, created by running `cmake -S
tools/ci/gte_core_player_link_probe -B build-player-link-probe` per its own
README) needed a `.gitignore` entry, exactly mirroring the existing
`/build-core-probe/` entry already there for the sibling probe:

```gitignore
/build-core-probe/
# editor-core-separation-2 campaign, PHASE4 - the new player-link probe's
# own build tree (tools/ci/gte_core_player_link_probe/), never committed,
# same as /build-core-probe/ above.
/build-player-link-probe/
```

## Step 5: `tools/ci/gte_core_standalone_probe/` untouched — confirmed (Step 3.6)

`git status` (see final state below) shows zero modification to either file
in `tools/ci/gte_core_standalone_probe/` — it remains byte-for-byte
unchanged, as required.

## Step 6: Compile check (Step 4 of the phase doc) — real terminal output

### 4.1 — Existing standalone-core probe, run first as a sanity baseline

```
cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe -G Ninja
cmake --build build-core-probe
```

(No `--target gte_core` passed — per the phase doc's own explicit warning,
that target name does not exist reachable from this outer build directory;
the plain default `ALL` build was used instead, which builds the one real
custom target, `gte_core_standalone_probe`, that this outer meta-project
defines.)

**Result: SUCCESS.** Configured a fresh inner build
(`build-core-probe/gte_core_inner_build/`), compiled 224 objects (all of
`gte_core`'s own translation units plus its third-party static-library
dependencies — `saba_pmx`, `volk`, `ktx`, `astcenc-avx2-static`), and linked
`libgte_core.a` as the final step:
```
[224/224] Linking CXX static library libgte_core.a
```
Zero errors. (The `KTX-Software`/`fatal: No names found, cannot describe
anything.` git-describe warning is a pre-existing, unrelated, harmless
`third_party/ktx` cosmetic warning — confirmed present in every prior
phase's own build output too, not introduced by this phase.)

### 4.2 — New player-link probe

```
cmake -S tools/ci/gte_core_player_link_probe -B build-player-link-probe -G Ninja
cmake --build build-player-link-probe
```

**Result: SUCCESS, first try — no `undefined reference`, no compile error.**
Configured a fresh inner build
(`build-player-link-probe/gte_core_inner_build/`), compiled 226 objects
(the same 224 as the existing probe, plus this probe's own
`main.cpp.obj`), linked `libgte_core.a`, and — critically — linked the final
executable:
```
[225/226] Linking CXX static library libgte_core.a
[226/226] Linking CXX executable gte_core_player_link_probe.exe
```
Confirmed via `browse_dir` on
`build-player-link-probe/gte_core_inner_build/` afterward that
`gte_core_player_link_probe.exe` genuinely exists on disk (32.6 MB,
timestamped 2026-09-24 11:03), alongside `libgte_core.a` (63.8 MB) — a real,
successfully-linked executable, not just a "build succeeded" message with no
actual output artifact.

This is this campaign's own Definition of Done, satisfied mechanically: a
real `cmake --build` linked a real, standalone executable against
`gte_core.a` alone (no `gte_editor` anywhere in this configuration's own link
line at all) that references `RenderSystem::Draw()` and constructs a
`gte::Network::NetworkServer`, and it succeeded.

### 4.3 — Re-run the normal, full build once more (reconfirm Step 3.2's success still holds)

```
cmake --build build --target GreatTamanaEditor
cmake --build build --target GreatTamanaEngineTests
```

**Result: both `ninja: no work to do.`** — both targets were already
up to date from Step 3's own build (no source file changed since; this
phase's own edits are pure `CMakeLists.txt` link-line/comment changes,
already picked up and successfully relinked in Step 3). Confirms nothing
broke between Step 3 and this final reconfirmation.

### 4.4 — Live runtime smoke test

Launched `build/GreatTamanaEditor.exe` via `run_app_background` (PID 18844).

`GET /get_swapchain` → `200`, `Content-Type: image/png`, 130760 bytes — a
real rendered frame (Scene/Game panels showing the default sky/atmosphere
gradient, Hierarchy showing `Entity 0 (Camera)`, Project panel showing
`TestScene.gtscene`) — no visual regression after the `RESCAN` removal.

`stop_app_background(pid: 18844)` called afterward — confirmed terminated
("Stopped process PID 18844 (GreatTamanaEditor) successfully.").

## Deviations from the plan

**None, on the code-changes side.** Every line number the phase doc's own
Step 2 ("Re-read... before starting") section predicted matched the real,
current source exactly — no drift since the doc was written (right after
PHASE3 landed).

**One addition beyond the phase doc's own explicit file list, done for
project cleanliness, not required by the plan's own text**: added a
`.gitignore` entry for `/build-player-link-probe/`, mirroring the existing
`/build-core-probe/` entry, so a manual run of the new probe (per its own
README's documented command) doesn't leave an untracked, uncommitted 32MB+
build tree sitting in `git status` forever. This is the same category of
"add `Core/` for clarity while you're there" latitude PHASE1's own report
used for its extra doc-comment sweep — a mechanical, zero-risk, clearly
beneficial addition, not an architectural decision.

**No rocky path.** Both `RESCAN` removals linked on the first attempt with
zero `undefined reference` errors, and the new probe linked on the first
attempt with zero errors of any kind. This is the direct, honest, mechanical
confirmation that PHASE2's `IFrameDebuggerCaptureRecorder` conversion and
PHASE3's `ILogQueryCapability` conversion each closed every real call site
completely — no gap was found requiring a return to an earlier phase's own
files, and `RESCAN` was never put back.

No genuine architectural ambiguity was encountered — `ask_questions` was not
needed. `delegate_task` was never called, per PHASE0's own Universal Rule 8.

## An unrelated, pre-existing observation (not a defect of this phase's own work)

`tools/ci/gte_core_standalone_probe/README.md`'s own "How it works" section
currently describes its mechanism as `add_subdirectory()`-based
("this folder's own `CMakeLists.txt` `add_subdirectory()`s the real
repository root..."), but the REAL, current
`tools/ci/gte_core_standalone_probe/CMakeLists.txt` actually uses the nested
`add_custom_target()` + separate `cmake -S ... -B ...` invocation mechanism
(the same mechanism this phase's own new probe uses) — its own header
comment even explicitly documents WHY `add_subdirectory()` was tried first
and abandoned. This looks like a genuine, pre-existing doc/code drift in that
file's own README (most likely: the README was written against an earlier,
abandoned design and never updated after the pivot), but per this phase's own
Step 3.6 instruction ("Do NOT touch `tools/ci/gte_core_standalone_probe/` at
all in this phase"), it was left completely untouched, exactly as instructed.
Flagging it here, honestly, as an observation for a future phase/maintainer,
not something this phase silently ignored.

## Git

Both the code changes (`CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`,
the new `tools/ci/gte_core_player_link_probe/` project) and this report are
committed together, in one commit, on `feature/editor-core-separation`
(branch never switched, per Universal Rule 2).
