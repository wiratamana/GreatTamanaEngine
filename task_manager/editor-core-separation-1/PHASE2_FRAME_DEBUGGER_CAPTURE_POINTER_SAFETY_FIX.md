# PHASE2 — Frame Debugger Capture: Fix The Two Real Pointer Dereferences

## Parent
`PHASE0_MASTER_STRATEGY.md`

## Step 1: The Goal

`RenderPasses.cpp` and `RenderSystem.cpp` — both destined to live in
`gte_core` — currently dereference the COMPLETE `FrameDebuggerCaptureContext`
type (calling real methods on it) behind a `#if GTE_ENABLE_EDITOR` guard.
This is a genuine link hazard: once `FrameDebuggerCapture.h/.cpp`'s real
method bodies live only in `gte_editor`, either file, left as-is, makes
`gte_core.a` carry an unresolved external symbol only `gte_editor.a` can
supply. Fix both BEFORE any CMake surgery happens.

## Step 2: The Situation / The Problem

Read `src/Editor/FrameDebuggerCapture.h`'s own header comment first — it
names all three files that touch `FrameDebuggerCaptureContext`:

- `src/Game/Game.cpp` — SAFE already. `Game::Render()` only ever forwards
  the pointer onward, never dereferences it. Its own comment states this
  outright. **No change needed for this file.**
- `src/Application/RenderPasses.cpp` — NOT safe. `AddFrameDebuggerReplayPasses()`
  calls real methods on the complete type (`capture.SetReplayStepPreviews(...)`),
  wrapped in `#if GTE_ENABLE_EDITOR` / `#include "../Editor/FrameDebuggerCapture.h"`.
- `src/Game/RenderSystem.cpp` — NOT safe. `RenderSystem::Draw()`'s own
  `#if GTE_ENABLE_EDITOR` block calls `capture->RecordDraw(...)`/
  `capture->RecordEntityDraw(...)` — real method calls on the complete type.

## Step 3: The Plan

1. Read `src/Application/RenderPasses.h` and `.cpp` in full. Confirm the
   exact current signature and body of `AddFrameDebuggerReplayPasses()`.
2. Create a NEW file, `src/Editor/FrameDebuggerReplayPasses.cpp`, in
   `namespace gte { ... }`, with an unconditional
   `#include "FrameDebuggerCapture.h"` at the top (no `#if` guard needed —
   this file will only ever be compiled as part of `gte_editor`). Move the
   ENTIRE current body of `AddFrameDebuggerReplayPasses()` (the part
   requiring the complete `FrameDebuggerCaptureContext` type) into this new
   file. The function's DECLARATION stays exactly where it already is, in
   `RenderPasses.h` — it only ever needs the forward-declared type there.
3. Remove the `#if GTE_ENABLE_EDITOR` body (and the now-empty `#else` stub,
   if one exists) from `RenderPasses.cpp` itself — after this phase,
   `RenderPasses.cpp` must contain ZERO reference to
   `FrameDebuggerCaptureContext`'s complete type and zero `#include` of
   `FrameDebuggerCapture.h`.
4. Read `src/Game/RenderSystem.h` and `.cpp` in full. Confirm the exact
   current shape of `RenderSystem::Draw()`'s `#if GTE_ENABLE_EDITOR` block.
5. Extract that block's body (the part calling `capture->RecordDraw(...)`/
   `capture->RecordEntityDraw(...)`) into a NEW free function declared in
   `RenderSystem.h`, e.g.:
   ```cpp
   // RenderSystem.h — forward declaration only, no #include needed.
   class FrameDebuggerCaptureContext;
   namespace gte {
   void RecordFrameDebuggerDraws(FrameDebuggerCaptureContext& capture, /* whatever else Draw() currently passes */);
   }
   ```
   taking `FrameDebuggerCaptureContext&` BY REFERENCE (legal with only a
   forward declaration, exactly like the pointer parameter it replaces).
   Define it FOR REAL only in a new file,
   `src/Editor/FrameDebuggerDrawRecording.cpp` (unconditional
   `#include "FrameDebuggerCapture.h"`, `namespace gte { ... }`).
6. `RenderSystem::Draw()` keeps its existing null-check branch shape
   (`if (capture != nullptr) { ... }`), now calling
   `RecordFrameDebuggerDraws(*capture, ...)` through a DECLARED-BUT-NOT-YET-
   LINKED-IN-`gte_core`-ALONE function, instead of dereferencing the pointer
   directly. Remove the `#if GTE_ENABLE_EDITOR` wrapper around this call
   entirely — the null-check itself is now the only gating needed (this is
   a preview of Phase 8's macro deletion, applied narrowly here since this
   exact code cannot compile any other way once Phase 9 splits the CMake
   targets).
7. Confirm via `search_in_dir` for `FrameDebuggerCaptureContext` and
   `FrameDebuggerCapture.h` across `src/Application/` and `src/Game/` that
   NO remaining file in those two folders includes the real header or
   references a complete-type method — only forward-declared pointer/
   reference parameters should remain.
8. Compile-check: incremental build of the current single `gte_core` target
   (the CMake split hasn't happened yet — this still all compiles into one
   archive today, just with the code correctly PARTITIONED so the future
   split is safe). Confirm no new warnings/errors.
9. Live smoke check: `run_app_background` the engine, use `gte_send_request`
   to hit `GET /frame_debugger/enable` then `/frame_debugger/capture`,
   confirm the Frame Debugger still works exactly as before (a replay-step
   preview image is still produced). Pull `GET /get_logs` to confirm no new
   error-level log entries appeared. `stop_app_background` when done.

## Files Touched

- `src/Application/RenderPasses.cpp` (shrink)
- NEW `src/Editor/FrameDebuggerReplayPasses.cpp`
- `src/Game/RenderSystem.h` (add free function declaration)
- `src/Game/RenderSystem.cpp` (shrink)
- NEW `src/Editor/FrameDebuggerDrawRecording.cpp`
- `CMakeLists.txt` — add the two new files to the (still-conditional, for
  now) Editor source list. Phase 9 will move this into the real
  `gte_editor` target's own list.

## Definition of Done

- `RenderPasses.cpp`/`RenderSystem.cpp` contain zero `#include` of
  `FrameDebuggerCapture.h` and zero complete-type dereference of
  `FrameDebuggerCaptureContext`.
- Incremental build succeeds, live smoke check confirms Frame Debugger still
  works.
- `PHASE2_COMPLETION_REPORT.md` written + git commit.

## Out of Scope

Do not touch `Game.cpp` (already safe, confirmed above). Do not do any
CMake target surgery yet (Phase 9).
