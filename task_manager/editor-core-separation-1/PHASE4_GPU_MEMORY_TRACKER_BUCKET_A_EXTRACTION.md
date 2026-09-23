# PHASE4 — Bucket A: Extract Editor-Only Debug-Name Tracking

## Parent
`PHASE0_MASTER_STRATEGY.md`

## Step 1: The Goal

`GpuMemoryTracker` and its callers currently carry `#if GTE_ENABLE_EDITOR`
guards around debug-name storage (`SetDebugName()`/`GetDebugName()`/
`m_debugNames`). After this phase, `GpuMemoryTracker`'s own header carries
ZERO macro tied to "is this an Editor build" — the debug-name feature moves
into a new, `gte_editor`-owned overlay type that wraps the plain tracker
from the outside, via an always-present, unconditional, opaque-handle-keyed
hook.

## Step 2: The Situation / The Problem

Read `src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp` in full first (confirm
real path via `browse_dir` on `src/Renderer/Memory/` if it has moved).
Design doc Section 2.6/7.1 identifies these exact files needing the same
treatment — re-confirm every one is real via `search_in_dir` for
`GTE_ENABLE_EDITOR` scoped to `src/Renderer/`:

- `src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp` — the `SetDebugName()`/
  `GetDebugName()` methods + `m_debugNames` map.
- `GpuResourceFactory.h`/`.cpp` — locate its real path via `search_in_dir`
  for `class GpuResourceFactory` (design doc doesn't give a full path).
- `src/Renderer/Renderer.h` — declares/forwards the same debug-name concept.
- Callers: `Buffer.cpp`, `DepthBuffer.cpp`, `Texture2D.cpp`,
  `VolumeTexture.cpp`, `RenderTexture.cpp` — locate real paths via
  `search_in_dir` for `m_tracker->SetDebugName` (design doc doesn't give
  full paths for these either — likely under `src/Renderer/` or
  `src/Renderer/Vulkan/`, confirm before editing).
- `src/Renderer/Memory/SdlMemoryTracker.h` — re-read it fresh; confirm
  whether it currently has its OWN `#if GTE_ENABLE_EDITOR` guard around any
  of its own tracked data (the design doc's Section 7.1 Step 2 says it
  "gets the same treatment" as `GpuMemoryTracker` — this contradicts
  Section 2.4's framing of it as an already-correct precedent; resolve this
  by reading the actual current file, not by trusting either summary
  blindly).

## Step 3: The Plan

1. Read every file listed above in full. Build the REAL, current list of
   every `#if GTE_ENABLE_EDITOR` site touching debug names (do not assume
   the design doc's list is complete or even fully accurate — it says so
   itself).
2. Design the always-present hook `GpuMemoryTracker` exposes for this,
   e.g.:
   ```cpp
   // Always compiled, no macro. A side table keyed by GpuResourceHandle —
   // NEVER a field on the tracked resource itself.
   class GpuMemoryTracker {
   public:
       // ... existing always-core methods unchanged ...
       using DebugNameHook = std::function<void(GpuResourceHandle, std::string_view)>;
       void SetDebugNameHook(DebugNameHook hook); // called by whoever wants to observe naming
   };
   ```
   OR, simpler and more consistent with "Core never pushes into Editor-owned
   types" (design doc Section 5.3): keep `GpuMemoryTracker` with NO hook at
   all, and instead have the new `gte_editor`-owned overlay type
   (`EditorGpuMemoryNameOverlay`) maintain its OWN
   `std::unordered_map<GpuResourceHandle, std::string>`, populated by
   wrapping/observing resource creation calls from the Editor side rather
   than being pushed data from `gte_core`. Decide which shape is the
   smaller, cleaner diff by actually reading how `Buffer.cpp` et al. call
   `SetDebugName()` today (is it called at construction time, with the
   handle available? If yes, the Editor-side overlay CANNOT intercept it
   without `gte_core` itself calling out — meaning SOME hook is
   unavoidable). Use `ask_questions` if this genuinely could go either way
   after reading the real code — this is a real design fork, not a
   mechanical choice.
3. Once the hook shape is decided: remove EVERY `#if GTE_ENABLE_EDITOR`
   around `SetDebugName()`/`GetDebugName()`/`m_debugNames` in
   `GpuMemoryTracker.h`/`.cpp` — either delete the feature from
   `GpuMemoryTracker` entirely (if the overlay approach needs zero
   core-side change) or make the hook call UNCONDITIONAL (never
   macro-gated) if a hook is required.
4. Create `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp` (new
   `gte_editor`-owned type) implementing the actual name storage +ImGui
   display glue the Memory panel needs today.
5. Update every caller (`Buffer.cpp`, `DepthBuffer.cpp`, `Texture2D.cpp`,
   `VolumeTexture.cpp`, `RenderTexture.cpp`, `GpuResourceFactory.cpp`,
   `Renderer.cpp`) to remove its own `#if GTE_ENABLE_EDITOR` guard around
   whatever `SetDebugName()`-adjacent call it makes today — either deleting
   the call (if the feature fully moved out) or making it unconditional (if
   a hook-call remains, now safe since the hook itself is null/no-op by
   default with no macro needed).
6. Apply the same resolved treatment to `SdlMemoryTracker.h` based on what
   Step... (the actual read in Step "Situation" above) found.
7. Confirm via `search_in_dir` for `GTE_ENABLE_EDITOR` scoped to
   `src/Renderer/` — zero results expected after this phase for anything
   related to debug-name tracking specifically (other, unrelated
   `GTE_ENABLE_EDITOR` sites in `src/Renderer/`, if any, are handled by
   later Bucket B/C phases — do not scope-creep into fixing those here).
8. Compile-check: incremental build. Live smoke check: `run_app_background`,
   open the Editor's Memory panel via `gte_send_request` (`GET
   /activate_tab?name=Memory` or whatever the real endpoint/tab name is —
   confirm via `GET /list_tabs`), take a screenshot via
   `gte_send_request("/get_swapchain")` — the real, documented endpoint for
   capturing the current rendered frame (see `AGENTS.md`'s "Networking"
   section; there is no `/get_editor_view` endpoint anywhere in this
   codebase — do not invent one), and visually confirm GPU resource debug
   names still display correctly in the Memory panel now visible on screen.

## Files Touched

- `src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp`
- `src/Renderer/Memory/SdlMemoryTracker.h` (if the real read confirms it
  needs the same treatment)
- `GpuResourceFactory.h`/`.cpp`, `src/Renderer/Renderer.h`/`.cpp` (real
  paths confirmed during execution)
- `Buffer.cpp`, `DepthBuffer.cpp`, `Texture2D.cpp`, `VolumeTexture.cpp`,
  `RenderTexture.cpp` (real paths confirmed during execution)
- NEW `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp`
- `CMakeLists.txt` (add the new Editor-side overlay file to the still-
  conditional-for-now Editor source list)

## Definition of Done

- `GpuMemoryTracker.h`/`.cpp` (and every caller above) contain zero
  `#if GTE_ENABLE_EDITOR` related to debug names.
- Memory panel still shows correct GPU resource debug names, confirmed via
  a live screenshot.
- `PHASE4_COMPLETION_REPORT.md` written + git commit, explicitly recording
  which hook shape (Step 2) was chosen and why.

## Out of Scope

Do not touch Bucket B (runtime capability) sites or Bucket C (dead branch)
cleanup here — those are Phases 5-8.
