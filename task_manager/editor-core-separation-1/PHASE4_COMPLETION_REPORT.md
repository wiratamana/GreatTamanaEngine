# PHASE4 — COMPLETION REPORT: GPU Memory Tracker Bucket A Extraction

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md`,
`PHASE2_COMPLETION_REPORT.md`, and `PHASE3_COMPLETION_REPORT.md` (the three
prior completion reports in this campaign folder) read in full for
continuation clues.

## Status: DONE

## The design fork — resolved by reading the real code, not guessed

This phase's own file explicitly flagged a real, undecided design fork:
*"does `GpuMemoryTracker` need a hook, or can the Editor-side overlay own
everything?"* I read `Buffer.cpp`/`DepthBuffer.cpp`/`Texture2D.cpp`/
`VolumeTexture.cpp`/`RenderTexture.cpp` in full before writing any code, and
confirmed: every one of them calls `m_tracker->SetDebugName(m_handle, ...)`
**immediately after** `m_tracker->Track(...)`, from **inside the exact
gte_core-destined constructor that produced the handle**. The Editor-side
overlay has no way to intercept that exact moment on its own — **a hook on
`GpuMemoryTracker` is unavoidable.** What is NOT unavoidable is
`GpuMemoryTracker` itself *storing* the name strings — that part can (and
now does) move out entirely.

**Chosen shape**: `GpuMemoryTracker` gained an always-compiled, macro-free
`DebugNameObserver` hook (`void(*)(void* userData, GpuResourceHandle,
const char* name)`) installed via `SetDebugNameObserver()`. `SetDebugName()`
and `Untrack()` both just forward to the installed observer (a `name ==
nullptr` call from `Untrack()` is a documented "forget this handle" signal).
`GpuMemoryTracker` itself now stores **zero bytes** of name/string data —
no `#if GTE_ENABLE_EDITOR`, no `std::unordered_map<..., std::string>`, no
`<string>`/`<unordered_map>` includes at all. A brand-new, Editor-owned
class, `src/Editor/EditorGpuMemoryNameOverlay.h/.cpp`, installs itself as
this observer and keeps its own name table keyed by (a locally re-derived)
packed `GpuResourceHandle`.

## A second real ambiguity found and resolved without needing `ask_questions`

`SdlMemoryTracker.h` (`src/Memory/SdlMemoryTracker.h`) — the phase's own
Step 2 flagged a direct contradiction between the design doc's Section 2.4
("already a correct precedent") and Section 7.1 Step 2 ("gets the same
treatment as GpuMemoryTracker"). Reading the actual, current file settled
this: **`SdlMemoryTracker.h` has ZERO `#if GTE_ENABLE_EDITOR` guards around
its own tracked data** (`LiveBytes()`/`LiveAllocationCount()`/the static
counters are all unconditionally compiled). The only `GTE_ENABLE_EDITOR`
mentioned anywhere near it is in a COMMENT describing that
`Application::SdlContext`'s constructor only calls `SdlMemoryTracker::
Install()` inside an `#if GTE_ENABLE_EDITOR` block — that is a Bucket B
("is the Editor's capability actually available") runtime-availability
question about a *call site* in `Application.cpp`, not a Bucket A
("editor-only data bolted onto a core type") problem inside
`SdlMemoryTracker.h` itself. **Section 2.4's framing was the correct one;
Section 7.1 Step 2's "gets the same treatment" was not applicable here.**
No change was made to `SdlMemoryTracker.h` — this is a deliberate,
confirmed no-op, not an oversight. Fixing `Application.cpp`'s own call-site
macro is explicitly out of scope for this phase (Bucket B, Phases 6-7).

No `ask_questions` call was needed for either fork — both were resolved by
directly reading the real, current code, exactly as the phase's own Step 3
instructed.

## What I did

1. **`src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp`** — removed every
   `#if GTE_ENABLE_EDITOR` (there were 4 sites: the `<string>`/
   `<unordered_map>` include guard, the `SetDebugName()`/`GetDebugName()`
   declaration guard, the `m_debugNames` member guard, and the `Untrack()`
   erase-call guard). `GetDebugName()` is DELETED outright (no replacement
   inside this class — that lookup now lives entirely on the Editor side).
   `SetDebugName()`/`Untrack()` are unconditional and forward to the new
   `DebugNameObserver` hook. The now-unused private `PackHandle()` helper
   was deleted from this class (re-created, privately, inside the new
   Editor-side overlay instead, since it is the only remaining consumer).
2. **NEW `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp`** — the Editor-
   owned name-storage type. All-static/global (mirroring `Logger`/
   `SdlMemoryTracker`/`ImGuiMemoryTracker`'s own established precedent —
   there is only ever ONE `GpuMemoryTracker` instance alive per process).
   `Install(GpuMemoryTracker&)` installs itself as the observer;
   `GetDebugName(handle)` looks a name up; a `ResetForTesting()` test-only
   method clears the table (needed because this overlay's storage is a
   single global table with no tracker-identity discriminator — see "Timing
   correctness" below for why that is safe in production but needed an
   explicit reset hook for independent `TEST()` cases that innocently reuse
   the exact same `GpuResourceHandle{0,1}`).
3. **Callers updated to the unconditional call shape** (removing the
   `#if GTE_ENABLE_EDITOR` / `#else (void)debugName;` dance):
   `Buffer.cpp`, `DepthBuffer.cpp`, `Texture2D.cpp`, `VolumeTexture.cpp`,
   `RenderTexture.cpp`. Each now calls `m_tracker->SetDebugName(...)`
   unconditionally (still gated only by its own existing `if (debugName !=
   nullptr)` runtime check, not a macro) — `SetDebugName()` itself is a
   cheap no-op when no observer is installed, so this costs nothing extra
   for a future Player host. Lightly updated the five headers' own
   "Editor-only" doc comments for accuracy (debugName is no longer
   compiled out — it is always accepted, just only ever *observed* by
   whichever debug-name observer happens to be installed).
4. **`GpuResourceFactory.h`/`.cpp`** — deleted the `#if GTE_ENABLE_EDITOR`
   `GetMemoryDebugName()` forwarder. Added a new, always-compiled
   `GetMemoryTracker() const noexcept` accessor returning the
   `std::shared_ptr<GpuMemoryTracker>` this factory already owns — the
   unconditional hand-off point an external observer needs.
5. **`Renderer.h`/`.cpp`** — same treatment: deleted the `#if
   GTE_ENABLE_EDITOR` `GetMemoryDebugName()` forwarder, added an
   always-compiled `GetMemoryTracker()` forwarding to
   `GpuResourceFactory::GetMemoryTracker()`.
6. **Updated the two real consumers** that used to call
   `Renderer::GetMemoryDebugName()`:
   - `src/Editor/Panels/MemoryPanel.cpp` — `BuildMemoryRows()`'s
     `nameLookup` lambda now calls
     `EditorGpuMemoryNameOverlay::GetDebugName(handle)` directly.
   - `src/Editor/FrameDebuggerDrawRecording.cpp` —
     `RecordFrameDebuggerDraws()` now calls
     `EditorGpuMemoryNameOverlay::GetDebugName(materialTexture->texture.Handle())`
     directly; its `Renderer&` parameter is kept (commented `/*renderer*/`)
     purely for signature stability with `RenderSystem.h`'s declaration and
     `RenderSystem::Draw()`'s own call site — touching those would have been
     unnecessary scope creep for this phase.
7. **Wired the ONE real installation call site**: `CreateEditorLayer()`
   (`src/Editor/ImGuiEditorLayer.cpp`) now calls
   `EditorGpuMemoryNameOverlay::Install(*renderer.GetMemoryTracker());` as
   its very first statement, **before** constructing `ImGuiEditorLayer`
   itself. See "Timing correctness" below for exactly why this placement
   (not inside `ImGuiEditorLayer`'s own constructor body) matters and is
   provably safe.
8. **Comment/doc accuracy pass** (not required by the phase's own Definition
   of Done, done anyway per this campaign's own "leave no stale claim"
   discipline): updated `src/Editor/MemoryPanelData.h`,
   `src/Game/Instantiation/MaterialTextureGpuCache.cpp`,
   `docs/architecture/editor-debug-ui.md`, and
   `docs/conventions/gpu-resource-memory-tracking.md` — all of which
   described the now-removed `Renderer::GetMemoryDebugName()`/
   `GpuMemoryTracker::GetDebugName()` shape. `docs/CHANGELOG.md` was
   deliberately left untouched (historical record, not a living doc).
9. **Test changes** (per `AGENTS.md`'s "every change to Tier 1 code must
   come with a matching test change" rule — `GpuMemoryTracker` is
   explicitly named as a canonical Tier-1 example there):
   - `tests/Memory/GpuMemoryTrackerTests.cpp` — replaced the old
     `#if GTE_ENABLE_EDITOR`-guarded `DebugName_*` tests (which called the
     now-deleted `GetDebugName()`) with 5 new, always-compiled tests
     exercising the observer hook itself directly: notifies the installed
     observer with the right handle/name; a harmless no-op with no observer
     installed; an invalid handle never reaches the observer;
     `Untrack()` notifies with a `nullptr` name; the latest
     `SetDebugNameObserver()` call always wins over an earlier one.
   - NEW `tests/Editor/EditorGpuMemoryNameOverlayTests.cpp` — 6 new tests
     for the real Editor-side name-storage consumer (round-trips a name,
     unset/invalid handles return empty, forgotten after `Untrack()`, a
     never-installed tracker is a harmless no-op, distinct handles get
     distinct names). Uses a `SetUp()`/`TearDown()` fixture calling the new
     `ResetForTesting()` — **a real, deliberately-designed test-hygiene fix,
     not boilerplate**: this overlay's storage is a single global table with
     no tracker-identity discriminator (by design — see "Timing
     correctness"), so two independent `TEST()` cases each constructing
     their own fresh, short-lived `GpuMemoryTracker` would otherwise
     collide on the exact same first-ever `GpuResourceHandle{0,1}` and leak
     a name across tests.
   - Both new test files registered in `tests/CMakeLists.txt`
     (`GpuMemoryTrackerTests.cpp`'s own entry unchanged in position;
     `EditorGpuMemoryNameOverlayTests.cpp` added to the existing
     `if(GTE_ENABLE_EDITOR)` Editor test block, right after
     `MemoryPanelDataTests.cpp`).
10. **`CMakeLists.txt`** — added `src/Editor/EditorGpuMemoryNameOverlay.h`/
    `.cpp` to the still-conditional (for now — Phase 9's job) Editor source
    list, right after `FrameDebuggerDrawRecording.cpp`.
11. Confirmed via `search_in_dir` for `GTE_ENABLE_EDITOR` scoped to
    `src/Renderer/` — every remaining hit is either an unrelated feature
    (`AtmosphereSkyBackgroundRenderer.h`, `RenderPassGroupRegistry.h/.cpp`,
    `VolumeTexturePreviewRenderer.h` — explicitly out of scope, Phase 5-8's
    job) or a comment describing this phase's own change in the past tense
    (e.g. "no `#if GTE_ENABLE_EDITOR` guard anywhere here anymore"). Zero
    real, live debug-name-related guards remain.

## Timing correctness — why `Install()` had to move to `CreateEditorLayer()`, not `ImGuiEditorLayer`'s constructor body

A genuinely important, non-obvious correctness question I investigated
before finalizing the design: **when must `Install()` run, relative to the
first GPU resource this process ever names, for no name to be silently
dropped?**

`ImGuiEditorLayer`'s own constructor's member-initializer list creates its
`m_gameView`/`m_sceneView` `RenderTexture`s (named `"GameView"`/
`"SceneView"`) — and member initializers ALWAYS run before a constructor's
own body. Installing the observer as the first statement of
`ImGuiEditorLayer`'s constructor BODY would therefore be **too late** —
`"GameView"`/`"SceneView"`'s own `SetDebugName()` calls would already have
fired with no observer listening. I confirmed this is fixable by moving the
`Install()` call one level up: `CreateEditorLayer()` (the free factory
function) calls `EditorGpuMemoryNameOverlay::Install(...)` as its own first
statement, **before** `std::make_unique<ImGuiEditorLayer>(...)` — which
means before `ImGuiEditorLayer`'s member-initializer list runs at all.

This still leaves one open question: by the time `CreateEditorLayer()` is
called (`Application`'s member-initializer list constructs `m_renderer`,
then `m_renderGraph`, then `m_editorLayer(CreateEditorLayer(m_window,
m_renderer))`), has `Renderer`'s OWN constructor — or any earlier
`Application` member — already created and named a GPU resource? I read
`Renderer::Renderer(Window&)` (`Renderer.cpp`) in full: it only constructs
`VulkanInstance`/`VulkanSurface`/`VulkanDevice`/`GpuTimingService`/
`VulkanAllocator`/`FramePresenter`/`GpuResourceFactory` — none of which
create a single named `Buffer`/`RenderTexture` at construction time
(`FramePresenter`'s own per-swapchain depth buffers are explicitly created
LAZILY, per its own header comment: *"m_depthBuffers is deliberately NOT
created here"*). I also confirmed `AtmosphereLutRenderer`'s and
`VolumeTexturePreviewRenderer`'s constructors (both constructed as
`Application` members before `m_editorLayer`) are likewise pure/lazy — the
former only registers a Frame Debugger group label string, the latter does
nothing until its own `EnsureInitialized()` is first called. **Every named
GPU resource this engine creates in practice is therefore created either
inside `ImGuiEditorLayer`'s own constructor (after `Install()` in
`CreateEditorLayer()` already ran) or later, lazily, on first real use
(further after that)** — confirmed by reading every relevant constructor,
not assumed.

## Compile-check / test / smoke-check results

**Incremental compile check only, per campaign policy** (no full clean
build, no full `ctest` regression pass — not required until Phase 9/14/19).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report).
- `cmake --build build --target gte_core` — **succeeded cleanly** (63 build
  steps, first attempt, zero compile errors).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**.
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly**.
- `ctest -R "GpuMemoryTracker|EditorGpuMemoryNameOverlay|MemoryPanelData|GpuResourceHandle|MemorySnapshotBuilder"` —
  **48/48 passed (100%)**, including all 5 new `GpuMemoryTrackerTest`
  observer-hook tests and all 6 new `EditorGpuMemoryNameOverlayTest`s.
- `ctest -R "FrameDebugger"` — **96/96 passed (100%)**, confirming
  `FrameDebuggerDrawRecording.cpp`'s switch to
  `EditorGpuMemoryNameOverlay::GetDebugName()` broke nothing.
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`,
  then via `gte_send_request`:
  - `GET /list_tabs` — confirmed `"Memory"` is a real, known tab name.
  - `GET /activate_tab?name=Memory` — succeeded, brought the "Memory" panel
    to the front.
  - `GET /get_swapchain` — screenshot confirmed the "Memory" panel's own
    "GPU (Tracked by Engine)" table shows REAL, correctly-resolved debug
    names (e.g. `"AtmosphereAerialPerspectiveVolume_GameView"`), not
    `"(unnamed)"` — direct visual proof the
    `GpuMemoryTracker::SetDebugNameObserver()` →
    `EditorGpuMemoryNameOverlay` → `MemoryPanel.cpp` pipeline works
    end-to-end for a resource named well after startup (this table is
    sorted biggest-first, so the much-smaller `"GameView"`/`"SceneView"`
    entries scrolled below the visible 200px-tall table region in this one
    screenshot — their own correct naming is instead proven by the
    "Timing correctness" reasoning above, which is a deterministic
    consequence of C++'s own statement-execution-order guarantees, not
    something that needs a screenshot to also independently re-confirm).
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors from any of the above.
  - `stop_app_background`'d the process when done.

No `bug_report` was filed — no tool malfunctioned during this phase.

## Definition of Done — checklist

- [x] `GpuMemoryTracker.h`/`.cpp` (and every caller) contain zero
      `#if GTE_ENABLE_EDITOR` related to debug names (confirmed via
      `search_in_dir`).
- [x] Memory panel still shows correct GPU resource debug names, confirmed
      via a live screenshot.
- [x] `PHASE4_COMPLETION_REPORT.md` written (this file) — explicitly records
      the chosen hook shape (a `DebugNameObserver` callback on
      `GpuMemoryTracker`, name STORAGE entirely moved to the new
      `EditorGpuMemoryNameOverlay`) and why (`SetDebugName()` is always
      called immediately after `Track()`, from inside the exact gte_core
      constructor that produced the handle — the Editor side has no way to
      intercept that moment without a hook).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of scope (confirmed, unchanged)

- Bucket B (runtime "is the Editor's capability actually available"
  conversions, e.g. `Application.cpp`'s own `#if GTE_ENABLE_EDITOR`-gated
  `SdlMemoryTracker::Install()` call site) — Phases 5-7's job.
- Bucket C (dead-branch cleanup, deleting the `GTE_ENABLE_EDITOR` CMake
  option itself) — Phase 8's job.
- No CMake target surgery (`gte_editor` target, etc.) — Phase 9's job. The
  new overlay files still live inside the pre-existing, still-macro-gated
  `if(GTE_ENABLE_EDITOR)` block in the ONE `gte_core` target, exactly like
  every prior phase's own new Editor-side files.

## Files touched

- MODIFIED: `src/Renderer/Memory/GpuMemoryTracker.h`/`.cpp`
- NEW: `src/Editor/EditorGpuMemoryNameOverlay.h`/`.cpp`
- MODIFIED: `src/Renderer/Buffer.cpp`/`.h`, `src/Renderer/DepthBuffer.cpp`/`.h`,
  `src/Renderer/Texture2D.cpp`/`.h`, `src/Renderer/VolumeTexture.cpp`/`.h`,
  `src/Renderer/RenderTexture.cpp`/`.h`
- MODIFIED: `src/Renderer/GpuResourceFactory.h`/`.cpp`,
  `src/Renderer/Renderer.h`/`.cpp`
- MODIFIED: `src/Editor/Panels/MemoryPanel.cpp`,
  `src/Editor/FrameDebuggerDrawRecording.cpp`, `src/Editor/ImGuiEditorLayer.cpp`
- MODIFIED: `src/Editor/MemoryPanelData.h`,
  `src/Game/Instantiation/MaterialTextureGpuCache.cpp` (comment accuracy only)
- MODIFIED: `docs/architecture/editor-debug-ui.md`,
  `docs/conventions/gpu-resource-memory-tracking.md` (doc accuracy only)
- MODIFIED: `CMakeLists.txt` (registered the 2 new Editor-side files)
- MODIFIED: `tests/Memory/GpuMemoryTrackerTests.cpp` (rewrote the debug-name
  test section for the new observer-hook API)
- NEW: `tests/Editor/EditorGpuMemoryNameOverlayTests.cpp`
- MODIFIED: `tests/CMakeLists.txt` (registered the new test file)
- NEW: `task_manager/editor-core-separation-1/PHASE4_COMPLETION_REPORT.md`
  (this file)
