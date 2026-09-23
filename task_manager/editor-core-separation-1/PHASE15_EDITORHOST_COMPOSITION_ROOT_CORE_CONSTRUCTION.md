# PHASE15 — `EditorHost`: Owns SdlContext/Window, Constructs `Core`

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 14. **The design doc flags
`EditorHost`/`Application` retirement as "genuine surgery" — split across
this phase, Phase 16, and Phase 17, exactly as `PHASE0` planned.**

## Step 1: The Goal

A new top-level class, `EditorHost`, exists (in `gte_editor`), replacing
`Application`'s composition-root role. This phase builds its STRUCTURE
ONLY: `EditorHost` owns `SdlContext` + its own `Window`, constructs `Core`
(injecting `Window` as `ISurfaceProvider&` and a real `IHostServices`
implementation that routes into Phase 3's `ILogSink`/`InstallLogSink`
mechanism), constructs the concrete Editor UI/state (`ImGuiEditorLayer` or
equivalent) via `CreateEditorLayer()`, and wires it into `Core` via
`Core::SetEditorLayerHook()` (Phase 12/13). It does NOT yet own the main
loop or the automation bridges — that is Phase 16.

## Step 2: The Situation / The Problem

Read `src/Application/Application.h`/`.cpp` in full, current (post-Phase-14)
state. Read `src/Editor/EditorLayer.h`
(`IEditorLayer`/`CreateEditorLayer()`/`CreateNullEditorLayer()`) in full —
this already-correct pattern (design doc Section 2.4, `PHASE0`'s Locked
Design Decisions #8 and #9) is what `EditorHost` constructs the concrete
Editor UI through. Confirm Phase 9 already renamed `NullEditorLayer.cpp`'s
own factory to `CreateNullEditorLayer()` — after that rename,
`gte::CreateEditorLayer()` has exactly ONE definition in the whole link
(`ImGuiEditorLayer.cpp`), so `EditorHost` calling it here is unambiguous;
if that rename somehow never landed, STOP and go fix Phase 9 first rather
than working around it here.

## Step 3: The Plan

1. Create `src/Editor/EditorHost.h`/`.cpp` (new, `gte_editor`-owned):
   ```cpp
   namespace gte {
   class EditorHost {
   public:
       EditorHost();
       void Run(); // Phase 16 fills this in for real
   private:
       SdlContext m_sdlContext;
       Window m_window;
       // A real IHostServices implementation routing Log() into
       // gte::InstallLogSink()'s registered sink (Phase 3) - or directly
       // constructing/holding the Logger instance and registering it here.
       EditorHostServices m_hostServices; // new small adapter type
       Core m_core;
       std::unique_ptr<IEditorLayer> m_editorLayer;
   };
   }
   ```
2. Create the small `EditorHostServices` adapter (implements
   `IHostServices`, forwards `Log()` calls appropriately — decide during
   execution whether it should itself BE the registered `ILogSink`, or
   whether it just forwards to the already-registered one from Phase 3;
   avoid double-registration).
3. `EditorHost`'s constructor, in this exact order: construct `SdlContext`
   first (RAII), then `Window`, then `Core(m_window, m_hostServices)`, then
   `CreateEditorLayer(m_window, m_core.GetRenderer())` (matching
   `IEditorLayer`'s existing real constructor signature — confirm exact
   current signature by reading `EditorLayer.h` fresh, adjust if it takes
   different/additional parameters today), THEN — the one new wiring step
   Locked Design Decision #8 requires that did not exist before this
   campaign — call `m_core.SetEditorLayerHook(m_editorLayer.get())` so
   `Core::BuildFrame()`'s own render-graph-frame-building hooks (Phase 13)
   have a real, non-null target from this point onward. Get this ordering
   right: `m_editorLayer` must already be fully constructed before this
   call (it is, per the order above), and this call must happen before
   `Core::BuildFrame()` is ever invoked for the first time (Phase 16's
   `Run()` loop naturally guarantees this, since construction always
   finishes before the loop starts).
4. Update `src/main.cpp`: construct `EditorHost` instead of `Application`,
   call `.Run()`. Leave `Application` itself still fully intact and
   unused-but-present for now (Phase 17 retires it) — this phase proves
   `EditorHost` CAN be constructed correctly side-by-side, without yet
   deleting the old path, minimizing risk of an irreversible half-broken
   state if something goes wrong.
5. For NOW, `EditorHost::Run()` can be a minimal stub (e.g. directly
   delegating to a temporary internal `Application`-style loop, or even
   just proving construction succeeds and exiting) — Phase 16 is where the
   REAL main loop + automation bridges land. Document this clearly as a
   deliberate, temporary stub.
6. Compile-check: incremental build. Live smoke check: `run_app_background`
   using the NEW `EditorHost`-constructing executable, confirm it at least
   boots to a window and constructs `Core` without crashing (even if
   `Run()`'s loop itself is still a stub at this point — full rendering
   confirmation happens once Phase 16 completes the real loop). Confirm via
   `GET /get_logs` (once reachable) or a temporary debug check that
   `m_core`'s `m_editorLayer` hook is genuinely non-null after construction
   — a silent, still-null hook here would make Phase 16's later blur/GBuffer
   validation smoke check fail confusingly far downstream of the real bug.

## Files Touched

- NEW `src/Editor/EditorHost.h`/`.cpp`, NEW `src/Editor/EditorHostServices.h`/`.cpp`
- `src/main.cpp`
- `CMakeLists.txt` (add new files to `gte_editor`'s source list)

## Definition of Done

- `EditorHost` constructs successfully: `SdlContext`, `Window`, `Core`
  (injected via `ISurfaceProvider&`), and the concrete Editor UI all
  construct without error, in the exact order in Step 3.3.
- `Core::SetEditorLayerHook()` is called with a genuinely non-null pointer
  before `main.cpp` ever calls `EditorHost::Run()`.
- `main.cpp` now constructs `EditorHost`.
- `PHASE15_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not implement the real main loop yet (Phase 16). Do not delete
`Application` yet (Phase 17).
