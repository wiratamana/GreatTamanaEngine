# PHASE2 — COMPLETION REPORT: Frame Debugger Capture Pointer Safety Fix

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` (the
only prior completion report in this campaign folder) read for continuation
clues.

## Status: DONE

## What I did

Re-confirmed the real, current shape of every file this phase touches via
`read_file`/`search_in_dir` before editing (per Universal Rule 9) — the
strategy doc's own description of `RenderPasses.h/.cpp` and
`RenderSystem.h/.cpp` matched the real code exactly, so no path/shape
surprises.

1. **`src/Application/RenderPasses.cpp`** — removed the `#if
   GTE_ENABLE_EDITOR` / `#include "../Editor/FrameDebuggerCapture.h"` /
   `#endif` block at the top of the file, and removed
   `AddFrameDebuggerReplayPasses()`'s ENTIRE definition (both the real
   `#if GTE_ENABLE_EDITOR` body and the `#else` no-op stub) — this file now
   contains zero `#include` of `FrameDebuggerCapture.h` and zero reference to
   the complete `FrameDebuggerCaptureContext` type, confirmed via
   `search_in_dir`. Also removed the now-orphaned `ReplayStepPassNamePool()`/
   `ReplayStepPassName()` anonymous-namespace helpers (they were only ever
   used by the function that just moved out) and the now-unused
   `<cstdio>`/`<deque>`/`<utility>` includes they required — `<cstdint>` is
   kept (still used by `AddGpuSkinningPasses()`).
2. **NEW `src/Editor/FrameDebuggerReplayPasses.cpp`** — `AddFrameDebuggerReplayPasses()`'s
   real body (unconditional `#include "FrameDebuggerCapture.h"`, no `#if`
   guard needed — this file only ever compiles as part of the Editor source
   list) plus the two helper functions it needs, moved here byte-for-byte
   unchanged in behavior. `RenderPasses.h`'s own DECLARATION of this function
   is completely unchanged (it only ever needed the forward-declared
   reference parameter).
3. **`src/Application/RenderPasses.h`** — updated `AddFrameDebuggerReplayPasses()`'s
   own doc comment to describe the new split (declaration stays here, real
   body now lives in the new Editor-side file) instead of the old
   "defined here, `#if`-guarded" description. No signature change.
4. **`src/Game/RenderSystem.h`** — added a new free function declaration,
   `RecordFrameDebuggerDraws(FrameDebuggerCaptureContext& capture, Registry&
   registry, Renderer& renderer, Entity entity, const Mesh& mesh, const
   Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4&
   viewProjection)`, right after the existing forward declaration of
   `FrameDebuggerCaptureContext` — legal with only the forward declaration,
   exactly like the strategy doc's own example. Also corrected a
   now-stale doc-comment sentence on the `batchedEntities` parameter that
   used to say "(and, inside the `#if GTE_ENABLE_EDITOR` block, for
   RecordDraw()/RecordEntityDraw())" — updated to reference
   `RecordFrameDebuggerDraws()` instead, since that `#if` block no longer
   exists.
5. **`src/Game/RenderSystem.cpp`** — removed the `#if GTE_ENABLE_EDITOR`
   `#include "../Editor/FrameDebuggerCapture.h"` block, and removed the now
   -unused `#include "ECS/Components/Name.h"` (only ever used inside the
   block that just moved out). `RenderSystem::Draw()` keeps its exact
   existing `if (capture != nullptr) { ... }` null-check branch shape, now
   calling `RecordFrameDebuggerDraws(*capture, registry, renderer,
   command.entity, *mesh, *pipeline, materialTexture, viewProjection)`
   instead of dereferencing the pointer's methods directly — the `#if
   GTE_ENABLE_EDITOR` wrapper around this call is gone entirely; the
   null-check is now the only gating.
6. **NEW `src/Editor/FrameDebuggerDrawRecording.cpp`** — `RecordFrameDebuggerDraws()`'s
   real body (unconditional `#include "FrameDebuggerCapture.h"`), moved here
   byte-for-byte unchanged in behavior from what used to run inline inside
   `RenderSystem::Draw()`'s own `#if GTE_ENABLE_EDITOR` block.
7. **`CMakeLists.txt`** — added the two new files
   (`src/Editor/FrameDebuggerReplayPasses.cpp`,
   `src/Editor/FrameDebuggerDrawRecording.cpp`) to the still-conditional (for
   now) `if(GTE_ENABLE_EDITOR)` Editor source list, right after
   `FrameDebuggerCapture.cpp`, with a comment noting Phase 9 will move this
   into the real `gte_editor` target's own list.
8. Confirmed via `search_in_dir` across `src/Application/` and `src/Game/`
   that no remaining file in those two folders includes the real
   `FrameDebuggerCapture.h` header or references a complete-type method —
   every remaining hit is a comment/forward-declaration only. `Game.cpp` was
   confirmed still untouched (already safe, per the phase's own "Out of
   Scope").

## Genuine ambiguity found — resolved via `ask_questions`

PHASE2's own Step 3 literally instructs removing BOTH the `#if
GTE_ENABLE_EDITOR` body AND the `#else` no-op stub from
`RenderPasses.cpp`/`RenderSystem.cpp`. But `AddFrameDebuggerReplayPasses()`/
the `RenderSystem::Draw()` call site that now calls
`RecordFrameDebuggerDraws()` are both called **unconditionally** (no macro
guard on the call site itself — only a runtime null-check) from
`Application.cpp`/`RenderSystem.cpp`. Since the two new files are, per this
phase's own "Files Touched" list, added only inside CMakeLists.txt's
existing `if(GTE_ENABLE_EDITOR)` block (the real `gte_editor` target doesn't
exist until Phase 9), doing this literally means a `GTE_ENABLE_EDITOR=OFF`
build no longer LINKS at all — and this repo has an actively-maintained,
recently-rebuilt `build-editor-off/` directory, confirming OFF is a real,
currently-working configuration today, not a stale/abandoned one.

I called `ask_questions` with two concrete options: (1) follow PHASE2
literally, accept the OFF-mode link break as a temporary, known gap until a
later phase (Bucket B / Phase 8) fixes it, or (2) deviate narrowly by
keeping a tiny `#if !GTE_ENABLE_EDITOR` no-op stub in
`RenderPasses.cpp`/`RenderSystem.cpp` (never touching the complete type) so
OFF keeps linking. **The user chose option (1) — follow PHASE2 literally.**
This is documented here as an explicit, deliberate, TEMPORARY regression,
not an oversight:

> **`GTE_ENABLE_EDITOR=OFF` does NOT link as of this phase.** Confirmed
> directly: building `build-editor-off/`'s `GreatTamanaEngine` target fails
> at the final executable link step with two `undefined reference` linker
> errors — `gte::AddFrameDebuggerReplayPasses(...)` (referenced from
> `Application.cpp.obj`) and `gte::RecordFrameDebuggerDraws(...)`
> (referenced from `RenderSystem.cpp.obj`) — because neither function has a
> definition anywhere in that configuration (the two new files that supply
> them only compile when `GTE_ENABLE_EDITOR` is ON). This is expected to
> remain broken until Phase 8 (Bucket B conversion / macro deletion) or
> later addresses it — **do not treat this as a surprise or as something a
> future phase needs to silently work around**; it is the direct,
> acknowledged consequence of this phase's own literal instructions, signed
> off on by the user.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy.**

- `cmake -S . -B build` (re-configure to pick up the two new source files) —
  succeeded (only the pre-existing, unrelated `third_party/ktx` `git
  describe` warning, same as Phase 1's own report).
- `cmake --build build --target gte_core` — one real compile error on the
  first attempt (`FrameDebuggerReplayPasses.cpp` used `rg::PassContext`
  without a full `#include` of `RenderGraph.h` — `RenderGraphBuilder.h`
  alone only forward-declares it); fixed by adding
  `#include "../Renderer/RenderGraph/RenderGraph.h"`. Second attempt: **succeeded
  cleanly** (2 build steps — only the recompiled/relinked objects).
- `cmake --build build --target GreatTamanaEngine` (the default
  `GTE_ENABLE_EDITOR=ON` config, the one this repo actually runs) —
  **succeeded cleanly**, full executable relinked.
- Live smoke check: `run_app_background`'d the built `GreatTamanaEngine.exe`,
  then via `gte_send_request`:
  - `GET /get_swapchain` — confirmed the Editor is up and rendering
    normally.
  - `GET /frame_debugger/open`, `GET /frame_debugger/enable?value=true`,
    `GET /frame_debugger/capture` — captured a real frame (15 events, empty
    scene with only a Camera entity).
  - `GET /frame_debugger/select_event?index=12` (the "Draw Quad" leaf under
    "DrawSkyBackground") — `GET /get_swapchain` screenshot confirmed a real,
    correctly-reconstructed sky-only replay-step preview image, proving the
    relocated `AddFrameDebuggerReplayPasses()` (now in
    `src/Editor/FrameDebuggerReplayPasses.cpp`) still works correctly.
  - **Further, more thorough check beyond the phase's own minimum**: used
    `POST /instantiate_primitive` (`{"shape":"cube","name":"SmokeTestCube",...}`)
    to add a real mesh entity, then re-ran `open`/`enable`/`capture`
    (16 events this time — `RenderOpaque` now has exactly one child leaf,
    `"SmokeTestCube (Entity 1)"`). Selecting that leaf
    (`select_event?index=11`) and screenshotting `GET /get_swapchain`
    confirmed: the tree leaf's own display name resolved correctly (proving
    `RecordFrameDebuggerDraws()`'s `registry.TryGetComponent<Name>()` lookup
    and its "Entity N" fallback logic both still work after the move), the
    Inspector panel showed `Pass: RenderOpaque (Entity Draw)`, and the
    preview image showed the cube correctly composited against the
    pre-sky-background replay state (object drawn, sky not yet drawn at that
    step) — exactly the expected reconstructed-state behavior.
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0` both times, no
    new warning/error-level log entries from any of the above.
  - `stop_app_background`'d the process when done.
- **Evidence-gathering only (not part of the phase's own required checks,
  done to document the ambiguity above with real proof rather than a
  guess)**: re-ran `cmake -S . -B build-editor-off` (already-existing
  `GTE_ENABLE_EDITOR=OFF` cache) then `cmake --build build-editor-off
  --target gte_core` (succeeded — archiving a `.a` never resolves symbols)
  followed by `cmake --build build-editor-off --target GreatTamanaEngine`
  (failed at the final link step with the two `undefined reference` errors
  quoted above, exactly as predicted).

No `bug_report` was filed — no tool malfunctioned; the one compile error
encountered was my own missing `#include`, caught and fixed on the very next
attempt.

## Definition of Done — checklist

- [x] `RenderPasses.cpp`/`RenderSystem.cpp` contain zero `#include` of
      `FrameDebuggerCapture.h` and zero complete-type dereference of
      `FrameDebuggerCaptureContext` (confirmed via `search_in_dir`).
- [x] Incremental build succeeds (`gte_core` target, then the full
      `GreatTamanaEngine` executable, default ON config).
- [x] Live smoke check confirms the Frame Debugger still works exactly as
      before (replay-step preview images render correctly, per-entity leaves
      resolve correctly) — plus an extra, deliberately more thorough check
      with a real spawned mesh entity, beyond the phase's own stated minimum.
- [x] `PHASE2_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report is written (see commit
      that follows).

## Out of scope (confirmed, unchanged)

- `src/Game/Game.cpp`/`Game.h` — untouched, already safe (only ever forwards
  the bare pointer, never dereferences it) — confirmed via `search_in_dir`
  before starting.
- No CMake target surgery (`gte_editor` target, etc.) — that is Phase 9's
  job. The two new files still live inside the pre-existing, still-macro-
  gated `if(GTE_ENABLE_EDITOR)` block in the ONE `gte_core` target, exactly
  as the phase's own "Files Touched" section specified.

## Files touched

- MODIFIED: `src/Application/RenderPasses.cpp` (shrunk — real body of
  `AddFrameDebuggerReplayPasses()` and its two private helpers moved out)
- MODIFIED: `src/Application/RenderPasses.h` (doc-comment update only, no
  signature change)
- NEW: `src/Editor/FrameDebuggerReplayPasses.cpp`
- MODIFIED: `src/Game/RenderSystem.h` (new `RecordFrameDebuggerDraws()`
  declaration + one stale doc-comment fix)
- MODIFIED: `src/Game/RenderSystem.cpp` (shrunk — real body of the former
  `#if GTE_ENABLE_EDITOR` block moved out)
- NEW: `src/Editor/FrameDebuggerDrawRecording.cpp`
- MODIFIED: `CMakeLists.txt` (registered the two new files in the
  still-conditional Editor source list)
- NEW: `task_manager/editor-core-separation-1/PHASE2_COMPLETION_REPORT.md`
  (this file)
