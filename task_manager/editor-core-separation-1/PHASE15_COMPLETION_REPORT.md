# PHASE15 — COMPLETION REPORT: `EditorHost` — Owns SdlContext/Window, Constructs `Core`

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE14_COMPLETION_REPORT.md` (all fourteen prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE — depends on Phase 14, confirmed already landed (`gte_core`
no longer links `SDL3::SDL3`; `Window.cpp`/`SdlContext`'s own precedent
already relocated into `gte_editor`).

## Step 0 — Re-confirmed the real, current code shape before editing

Per Universal Rule 9, read (not assumed) every file this phase touches or
depends on, fresh, before writing anything:

- `src/Application/Application.h`/`.cpp` (post-Phase-14 state) — confirmed
  the exact constructor initializer-list order (`m_sdlContext` →
  `m_window` → `m_hostServices` → `m_core(m_window, m_hostServices)` →
  `m_renderer`/`m_renderGraph` (bound to `m_core`'s own accessors) →
  `m_editorLayer(CreateEditorLayer(m_window, m_renderer))` → ...), and the
  exact constructor-BODY wiring order (`InstallLogSink()` →
  `SetEditorLayerHook()` → `SetPresentImGuiRecorder()` →
  `SetSceneIOCapability()` → `NetworkServer::Start()` → `GTE_LOG_INFO`).
- `src/Core/Core.h`/`.cpp` — confirmed the real constructor signature
  (`Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices)`),
  `GetRenderer()`'s real return type, and `SetEditorLayerHook(IEditorLayer*)`.
- `src/Editor/EditorLayer.h` — confirmed `CreateEditorLayer(Window&,
  Renderer&)`'s exact current signature (Phase 9's rename already landed —
  this is the ONE definition in the whole link, `ImGuiEditorLayer.cpp`) and
  `CreateNullEditorLayer(Window&, Renderer&)` exists as the documented
  gte_core-only fallback.
- `src/Core/IHostServices.h`/`src/Core/LogSink.h` — confirmed
  `IHostServices::Log(LogLevel, std::string_view)`'s exact signature and
  `InstallLogSink()`/`LogToActiveSink()`'s exact free-function shapes.
- `src/Editor/Logger.h` — confirmed `LoggerLogSink::Instance()`'s exact
  Meyers-singleton accessor shape (the ONE real `ILogSink` this engine
  ships).
- `src/Window/Window.h` — confirmed `Window` already implements
  `ISurfaceProvider` (Phase 10) and its real constructor signature
  (`Window(const std::string&, int, int, bool = true)`).
- `src/main.cpp` — confirmed the exact current `--reimport` CLI branch and
  the `gte::Application app(...); return app.Run();` block this phase
  replaces.
- Root `CMakeLists.txt` — confirmed `gte_editor`'s real, current
  `add_library(gte_editor STATIC ...)` source list (right after
  `Application.cpp`), and that no prior phase had already created an
  `EditorHost`/`EditorHostServices` file (`browse_dir` on `src/Editor/`
  confirmed 81 pre-existing files, none named `EditorHost*`).

No path/shape surprises versus PHASE15's own plan text — every real file
matched exactly what the phase document described.

## What I did

1. **NEW `src/Editor/EditorHostServices.h`/`.cpp`** — the small
   `IHostServices` adapter Core's constructor requires. Resolved the
   phase's own explicitly-flagged open design question ("decide during
   execution whether it should itself BE the registered `ILogSink`, or
   whether it just forwards to the already-registered one") by mirroring
   `Application::ApplicationHostServices`' own already-shipped, already-
   verified precedent EXACTLY: `Log()` forwards straight into
   `LogToActiveSink(level, "Core", message)` — the SAME global sink
   mechanism `GTE_LOG_*` itself already uses, never a second, competing
   logging path. Unlike `ApplicationHostServices` (a private nested struct
   inside `Application.h`, retired alongside it at Phase 17), this is a
   real, permanent, standalone `gte_editor`-owned type with its own
   header/`.cpp` pair, since `EditorHost` is the composition root that
   survives this whole campaign.
2. **NEW `src/Editor/EditorHost.h`/`.cpp`** — the new composition-root
   class. Members, in construction order: `SdlContext m_sdlContext`
   (a private nested struct mirroring `Application::SdlContext`'s exact
   `SDL_Init()`/`SDL_Quit()`/`SdlMemoryTracker::Install()` shape verbatim —
   `Application`'s own version is a private nested type scoped to that
   class, so `EditorHost` needs its own copy of this small RAII guard, not
   a shared one), `Window m_window`, `EditorHostServices m_hostServices`,
   `Core m_core`, `std::unique_ptr<IEditorLayer> m_editorLayer`.
   Constructor initializer list, in the EXACT order PHASE15's own Step 3.3
   specifies:
   - `m_sdlContext()` → `m_window(title, width, height)` →
     `m_hostServices()` → `m_core(m_window, m_hostServices)` (`Window`
     implicitly upcasts to `ISurfaceProvider&`, per Phase 10's own
     established precedent — zero cast needed) →
     `m_editorLayer(CreateEditorLayer(m_window, m_core.GetRenderer()))`.
   - Constructor BODY (in this order): `gte::InstallLogSink(&gte::
     LoggerLogSink::Instance())` (mirrors `Application`'s own identical
     call — this phase's `Run()` stub never starts a `NetworkServer`, so
     there's no earlier subsystem that could log before this runs), then
     a debug-only `assert(m_editorLayer != nullptr && "...")` immediately
     followed by `m_core.SetEditorLayerHook(m_editorLayer.get())` — THE one
     new wiring call this whole campaign exists to add (Locked Design
     Decision #8) — then one `GTE_LOG_INFO("EditorHost", ...)` confirming
     the whole chain completed.
   - `Run()` — a deliberate, TEMPORARY stub (documented extensively in
     both files' own comments): a minimal SDL event pump (`SDL_PollEvent`/
     `SDL_EVENT_QUIT`/`SDL_Delay(16)`) that keeps the just-opened window
     alive and responsive to a real OS close request, WITHOUT calling
     `Core::Update()`/`BuildFrame()`/`Present()` at all, and WITHOUT
     servicing any automation bridge/`NetworkServer` (none exist on this
     class yet — Phase 16's job). `Application` (still fully intact,
     completely unused, per this phase's own "Out of Scope") remains the
     only path that actually renders/simulates a frame today.
3. **`src/main.cpp`** — replaced `#include "Application/Application.h"`
   with `#include "Editor/EditorHost.h"`, and the `try` block's
   `gte::Application app("Great Tamana Engine", 1280, 720); return
   app.Run();` with `gte::EditorHost host("Great Tamana Engine", 1280,
   720); return host.Run();`. The `--reimport` CLI branch (which never
   touches `Application`/`EditorHost`/`Window` at all — pure headless
   `AssetImporter` plumbing) is completely untouched. `Application` itself
   is NOT deleted or modified — it stays fully intact and unused, exactly
   as this phase's own Step 4/"Out of Scope" require (Phase 17 retires it).
4. **`CMakeLists.txt`** — registered all four new files
   (`EditorHostServices.h`/`.cpp`, `EditorHost.h`/`.cpp`) in `gte_editor`'s
   own unconditional `target_sources()` list, right after
   `Application.cpp` (before `Editor/EditorContext.h`), with a comment
   explaining the new composition root and pointing at this phase.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 19; this
phase is not one of the three full-checkpoint phases).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report).
- `cmake --build build --target gte_editor` — **succeeded cleanly on the
  first attempt** (3 build steps: `EditorHostServices.cpp.obj`,
  `EditorHost.cpp.obj` compiled fresh, `libgte_editor.a` relinked).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**
  (2 build steps: `main.cpp.obj` recompiled, executable relinked, every
  `.spv` shader + `SDL3.dll` staged as usual).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly** (relink only — no test source touched by this phase, and
  nothing this phase changed is included by any test translation unit).
- **Live smoke check** (per this phase's own explicit instruction — NOT a
  `run_app_background` + `gte_send_request` screenshot check, since
  `EditorHost` does not own a `NetworkServer` yet this phase, confirmed
  correct/expected below, not a bug):
  - `run_app_background`'d `build/GreatTamanaEngine.exe` (PID 11364).
  - `powershell -Command "Start-Sleep -Seconds 2; Get-Process -Id 11364 |
    Select-Object Id,ProcessName,MainWindowTitle,Responding"` —
    `MainWindowTitle: "Great Tamana Engine"` (the exact title string passed
    into `EditorHost`'s constructor), `Responding: True` — **direct,
    positive proof** the process did not crash, a real OS window was
    created (`SdlContext` → `Window` → `Core` → `CreateEditorLayer()` all
    completed without throwing), and the temporary `Run()` stub's SDL event
    pump is alive and keeping the window responsive (a hung/crashed
    process would show `Responding: False` or no process at all).
  - `GET /get_logs?limit=5` — **connection refused** (`ECONNREFUSED`
    equivalent) — this is the CORRECT, EXPECTED result this phase, not a
    failure: `EditorHost` does not construct a `Network::NetworkServer` at
    all yet (Phase 16's own job, per design doc Section 6.1 — automation
    bridges attach to `EditorHost`, never `Core`, and this phase's own
    scope is construction-order structure only). This confirms the debug
    policy's own "use `gte_send_request` for verification" guidance was
    followed and its result correctly interpreted, not skipped.
  - `stop_app_background`'d the process (PID 11364) cleanly when done.
  - **Regarding the "genuinely non-null `IEditorLayer*` hook" requirement**:
    since no HTTP surface is reachable yet this phase (see above), I could
    not confirm this via `GET /get_logs` as the phase's own Step 6 suggests
    as one option — I used its own explicitly-sanctioned alternative
    instead ("a temporary debug check"): a debug-build `assert(m_editorLayer
    != nullptr && "...")` placed immediately before the
    `SetEditorLayerHook()` call in `EditorHost`'s own constructor body. The
    process booting and running normally (confirmed above, `Responding:
    True`, no crash) is itself live, positive proof this assert did NOT
    fire — a null `m_editorLayer` at that exact point would have aborted
    the process immediately, well before the window could ever become
    responsive.

No `bug_report` was filed — every tool call behaved as expected this phase;
the one "failure" encountered (`GET /get_logs` connection refused) is the
correct, anticipated, documented result of this phase's own explicit scope
boundary (no `NetworkServer` on `EditorHost` yet), not a malfunction.

## A self-inflicted `edit_line` mistake in `main.cpp`, caught and fixed immediately by re-reading the file

My first `edit_line` call meant to replace the single old
`#include "Application/Application.h"` line with the new
`#include "Editor/EditorHost.h"` line accidentally used a `length` that
left BOTH the new and the old include line present (a boundary-adjacent
duplicate the tool's own auto-dedup check did not catch, since the two
lines are not textually identical). Caught immediately by re-reading the
file with `read_file`, fixed with a follow-up `edit_line` (`length=2`)
collapsing both back down to the one correct new include. A second,
similar slip while replacing the `Application app` construction/`Run()`
call left a stray leftover `return app.Run();` line directly after the new
`return host.Run();` line — caught the same way (re-reading the file) and
removed with one more `edit_line` call. Both were self-inflicted line-
counting mistakes on my part, not tool malfunctions — confirmed correct
afterward by a fresh `read_file` and the subsequent clean compile. No
`bug_report` was filed.

## Definition of Done — checklist

- [x] `EditorHost` constructs successfully: `SdlContext`, `Window`, `Core`
      (injected via `ISurfaceProvider&`), and the concrete Editor UI all
      construct without error, in the exact order Step 3.3 specifies
      (confirmed both by direct code review and by the live smoke check —
      the process boots to a real, responsive window with zero crash).
- [x] `Core::SetEditorLayerHook()` is called with a genuinely non-null
      pointer before `main.cpp` ever calls `EditorHost::Run()` — confirmed
      by direct code review (the call happens in the constructor body,
      `Run()` is only ever invoked from `main()` after construction fully
      returns) AND by a debug-build `assert()` that would have aborted the
      process immediately had this been violated (it did not — the process
      ran normally).
- [x] `main.cpp` now constructs `EditorHost` (confirmed via `read_file`).
- [x] `PHASE15_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did **not** implement the real main loop — `EditorHost::Run()` is a
  deliberate, extensively-documented, TEMPORARY stub (no
  `Core::Update()`/`BuildFrame()`/`Present()` calls, no automation bridge,
  no `NetworkServer`) — Phase 16's job.
- Did **not** delete or modify `Application.h`/`.cpp` — both remain fully
  intact and unused (confirmed via `git_status`: neither file appears in
  this phase's diff) — Phase 17's job to retire them.
- Did **not** rename the executable target (`GreatTamanaEngine` stays
  as-is) — Phase 17's job.
- Did **not** attach any of the five automation bridges
  (`EngineCommandBridge`/`FrameCaptureBridge`/`EditorUiCommandBridge`/
  `FrameDebuggerCommandBridge`/`AssetImportCommandBridge`) or
  `Network::NetworkServer` to `EditorHost` — Phase 16's job, per design doc
  Section 6.1 ("Core stays a pure engine facade... these attach to
  EditorHost, never to Core").
- Did **not** call `Core::SetPresentImGuiRecorder()` from `EditorHost` yet —
  that wiring is tightly coupled to the real per-frame render loop
  (Phase 16's job); this phase's `Run()` stub never calls
  `Core::BuildFrame()`/`Present()` at all, so there is nothing for that
  recorder to be invoked from yet.

## Files touched

- NEW: `src/Editor/EditorHostServices.h`/`.cpp`
- NEW: `src/Editor/EditorHost.h`/`.cpp`
- MODIFIED: `src/main.cpp` (constructs `EditorHost` instead of
  `Application`; `#include` swap)
- MODIFIED: `CMakeLists.txt` (registered the four new files in
  `gte_editor`'s unconditional source list)
- NEW: `task_manager/editor-core-separation-1/PHASE15_COMPLETION_REPORT.md`
  (this file)
