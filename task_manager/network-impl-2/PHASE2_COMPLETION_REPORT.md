# PHASE2 — Completion Report: Cross-Thread Frame Capture Bridge

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document executed:
`PHASE2_CROSS_THREAD_FRAME_CAPTURE_BRIDGE.md`.

## Summary

Implemented exactly what the phase document specified: the one, sanctioned,
reviewed, thread-safe bridge (`gte::FrameCaptureBridge`) a network route
handler will be allowed to touch starting Phase 3, plus the `AGENTS.md`
"Networking" update documenting it as the sanctioned exception. Nothing
under `src/Network/`, `src/Renderer/`, or `src/Application/Application.h/.cpp`
was touched — per the phase document's own "What NOT to do in this phase"
section, this phase is the bridge CLASS alone, fully covered by its own
tests, with nothing wired to it yet.

## What was done

1. **`src/Application/FrameCaptureBridge.h`** (new file) — `FrameCaptureKind`
   (`Swapchain`/`GameView`), `CapturedPngImage` (PNG bytes + width/height),
   `FrameCaptureFailureReason` (`TargetNotAvailable`/`TimedOut`), and the
   `FrameCaptureBridge` class itself, matching the phase document's own
   class shape field-for-field: `RequestCaptureAndWait()` (network-thread
   side, returns a `RequestResult` with exactly one of `image`/`failure`
   meaningful, or `alreadyPending == true`), `IsCaptureRequested()`/
   `FulfillPendingRequest()`/`FailPendingRequest()` (main-thread side). Two
   private `Slot` instances (`m_swapchainSlot`/`m_gameViewSlot`), each with
   its own `std::mutex`/`std::condition_variable` — provably independent,
   never sharing state.
2. **`src/Application/FrameCaptureBridge.cpp`** (new file) — implements the
   predicate-form `wait_for(lock, timeout, [&]{ return slot.fulfilled; })`
   pattern specified in the phase document's 3.3 (closing the exact same
   lost-wakeup race class `AGENTS.md`'s Job System section already
   documents), `notify_one()` (never `notify_all()`, since at most one
   waiter can ever exist per slot — a second concurrent same-kind request is
   rejected up front with no waiting at all), and the "late arrival after
   timeout" guard (`if (!slot.requested) return;`) in both
   `FulfillPendingRequest()`/`FailPendingRequest()`. Every public method
   takes/releases its own lock only — no method calls another while already
   holding `slot.mutex`.
3. **`tests/Application/FrameCaptureBridgeTests.cpp`** (new file, 7 tests) —
   covers every scenario the phase document's 3.5 lists: timeout with no
   fulfiller, a separate thread fulfilling well before a long timeout
   (byte-for-byte image equality asserted), a second concurrent same-kind
   request returning `alreadyPending == true` immediately (asserted via
   wall-clock elapsed time, not just the return value), `FailPendingRequest()`
   delivering `TargetNotAvailable` quickly, a late
   fulfill/fail call after an already-timed-out request being a safe no-op
   that doesn't corrupt the next request, `Swapchain`/`GameView` slot
   independence, and `IsCaptureRequested()` correctly reflecting per-slot
   pending state.
4. **CMake wiring** — `src/Application/FrameCaptureBridge.h/.cpp` added to
   root `CMakeLists.txt`'s unconditional `target_sources(gte_core PRIVATE
   ...)` list (right after `MemorySnapshotBuilder.h`, alongside the rest of
   `src/Application/`) — no `GTE_ENABLE_NETWORK`/other switch dependency,
   matching Locked Design Decision #6 (this whole campaign's new modules
   always compile unconditionally). `tests/Application/
   FrameCaptureBridgeTests.cpp` added to `tests/CMakeLists.txt`'s
   `GTE_TEST_SOURCES`, right after `MemorySnapshotBuilderTests.cpp`.
5. **`AGENTS.md` update** — added the exact bullet the phase document's 3.4
   specifies to the existing "Networking" section, right after the "a route
   handler must be a PURE function..." bullet, documenting
   `FrameCaptureBridge` as the one sanctioned exception and pointing at this
   campaign's own phase document.

## Deviations from the phase document

- **None of substance.** The implementation follows the phase document's
  own `RequestCaptureAndWait()`/`FulfillPendingRequest()`/
  `FailPendingRequest()` step-by-step notes (3.3) essentially verbatim.
- One self-caught tooling mistake during editing (not a deviation from the
  plan itself): an early `edit_line` call meant to append two new lines
  after an existing one in `CMakeLists.txt`/`tests/CMakeLists.txt`
  accidentally replaced (rather than inserted after) the wrong line index
  twice, transiently deleting `EventTranslator.h`/`EventTranslatorTests.cpp`/
  `Input/InputStateTests.cpp` and duplicating `MemorySnapshotBuilder.h`/
  `MemorySnapshotBuilderTests.cpp` in the working tree. Caught immediately
  by re-reading the file back and grep-checking for duplicates/missing
  entries via `search_in_dir` before ever attempting a build; corrected in
  the same phase, and reverified with a fresh `search_in_dir` pass (each of
  `MemorySnapshotBuilder.h`, `EventTranslator`, `InputStateTests`,
  `FrameCaptureBridge` appears in the source/test lists exactly the
  expected number of times, no duplicates, nothing missing) before
  building. No source/test file content itself was ever wrong — only the
  two CMake list files were transiently mis-edited and then fixed, and the
  final committed state was fully verified.

## Compile/test verification performed

- `cmake --build build --target gte_core` — succeeded (only
  `FrameCaptureBridge.cpp` needed recompiling + relink of `libgte_core.a`).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (linked
  cleanly against the new `gte_core` + the new test file).
- Ran the new tests directly:
  `tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameCaptureBridge*` —
  **7/7 passed**.
- Per `AGENTS.md`'s own Job System precedent ("a single green run is not
  sufficient evidence for genuinely concurrent code"), stress-repeated the
  same filter with `--gtest_repeat=50` — **all 50 iterations passed, zero
  hangs/failures**.
- Ran the closest pre-existing, related test suites as an extra regression
  check (not required by the workflow rules for a non-final phase, but
  cheap and directly adjacent to this phase's own edits):
  `*EventTranslator*:*MemorySnapshotBuilder*:*NetworkServer*:*NetworkRoutes*`
  — **20/20 passed**, confirming the CMake-list mis-edit described above was
  fully corrected (nothing lost/duplicated in the final build).
- Per this campaign's workflow rules, a full `ctest`/full rebuild was
  deliberately NOT run — only the targeted `gte_core`/`GreatTamanaEngineTests`
  builds above, plus the new/adjacent tests specifically, as instructed for
  a non-final phase.

## What the next phase (PHASE3) should know

- `gte::FrameCaptureBridge` (`#include "Application/FrameCaptureBridge.h"`,
  relative to `src/`) is ready to use — construct one instance, own it in
  `Application` (constructed BEFORE `NetworkServer`, per the phase
  document's own ordering note), and hand a reference/pointer into
  `NetworkServer`'s constructor (a new, backward-compatible default
  parameter — Phase 3's own job, not touched here).
- `RequestCaptureAndWait(kind, timeoutMilliseconds = 3000)` is the ONLY
  method a route handler should ever call. `IsCaptureRequested()`/
  `FulfillPendingRequest()`/`FailPendingRequest()` are main-thread-only and
  should only ever be called from `Application::Run()`, once per frame, per
  the new `AGENTS.md` bullet.
- Nothing under `src/Network/`, `src/Renderer/`, or `src/Application/
  Application.h/.cpp` was touched in this phase — Phase 3 starts from a
  clean slate for wiring `FrameCaptureBridge` into `Application`'s
  construction order, the Game-view synchronous capture path, and the new
  `GET /get_game_view` route + response-format-negotiation helper.
- `NetworkServer`'s constructor and its existing seven no-argument
  construction call sites in `tests/Network/NetworkServerTests.cpp` remain
  completely untouched by this phase, as planned.
