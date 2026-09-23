# PHASE12 — COMPLETION REPORT: `Core` Class Skeleton, Construction, Accessors

## Parent
`PHASE0_MASTER_STRATEGY.md`, plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` through
`PHASE11_COMPLETION_REPORT.md` (all eleven prior completion reports in this
campaign folder) read in full for continuation clues. Also re-read
`README.md`/`AGENTS.md` (repo root) per task instructions.

## Status: DONE

## Genuine ambiguity found, resolved via `ask_questions` (user: "leave the
decision-making up to you")

Before editing anything, I read `src/Application/Application.h`/`.cpp` (2480
lines) in full, current state, per this phase's own Step 2 instruction, and
found two real, concrete conflicts between PHASE12's own literal plan and the
actual current code shape — both architecturally significant enough to
warrant `ask_questions` rather than guessing silently (Universal Rule 7):

1. **PHASE12's own Step 5 literally says**: "Update every
   Application-internal call site that used to reach
   `m_renderer`/`m_game`/etc directly to instead go through
   `m_core.GetRenderer()`/`m_core.GetGame()`/etc." But `Application::Run()`
   (2480 lines) and its two `Register*Provider()` helper methods (~700
   lines, capturing `this` in dozens of lambdas) — none of which move until
   PHASE13 — reference these members throughout. Rewriting every one of
   those call sites in THIS phase would be a large, high-risk mechanical
   diff across code that PHASE13 is about to physically relocate anyway
   (at which point it naturally becomes plain member access again, needing
   zero `m_core.GetX()` indirection).
2. **Core's own FROZEN public contract** (design doc Section 5.2, reproduced
   verbatim in PHASE12's own Step 1) exposes accessors for
   `Renderer&`/`Registry&`/`Game&`/`rg::RenderGraph&`/`EngineContext&`/
   `Time&`/`const FrameStats&` ONLY — it has **no accessor for either
   `rg::RenderPipeline` instance** (`m_offscreenRenderPipeline`/
   `m_presentRenderPipeline`), even though the design doc's own Section 2.1
   ownership graph lists "two `rg::RenderPipeline` instances" as Core-owned.
   Their only real consumers (`RegisterOffscreenRenderPipelineProviders()`/
   `RegisterPresentRenderPipelineProvider()`/`Run()`) do not move until
   PHASE13 — moving them into Core now would leave `Application` with
   literally no way to reach them.

I called `ask_questions` with two concrete questions (each with 2 answer
choices). The user's response: *"user is not at office, leave the decision
making up to you."* I resolved both in favor of the **lower-risk, more
architecturally sound option**, documented explicitly in code:

1. **Reference-alias deviation** (not the full literal rewrite): `Application`
   keeps private REFERENCE members with the exact same names
   (`Renderer& m_renderer;`, `rg::RenderGraph& m_renderGraph;`,
   `Game& m_game;`, `EngineContext& m_engineContext;`), bound to `m_core`'s
   own real, owned instances at construction time. Every one of Run()'s/the
   `Register*Provider()` methods' existing `m_renderer.`/`m_game.`/etc call
   sites (and every lambda that captures `this` and reads them) keeps
   compiling and behaving BYTE-FOR-BYTE UNCHANGED this phase. The literal
   "go through `m_core.GetX()`" rewrite happens naturally in PHASE13, once
   that code physically moves into `Core`'s own methods (where it becomes
   ordinary, direct member access again, not an accessor call at all).
2. **Defer the two `RenderPipeline` instances to PHASE13**: they stay
   Application-owned VALUE members, completely untouched this phase — they
   move together with their only real consumer (the per-frame orchestration
   logic) when PHASE13 does the real extraction. `Core` itself never
   mentions `rg::RenderPipeline` at all in this phase.

Both deviations are documented in-place (`Core.h`'s own class comment,
`Application.h`'s own `m_renderer`/`m_core` member comments) so a future
reader (including PHASE13's own implementer) understands exactly why, without
needing to re-read this report.

## What I did

1. Read `src/Editor/EditorLayer.h` in full (confirmed `IEditorLayer`'s method
   list and the pre-existing `FrameDebuggerCaptureContext` forward
   declaration this file already carries — Locked Design Decision #8's own
   precedent). Confirmed `src/Core/`'s current file list
   (`EditorCapabilities.h`, `EngineContext.h`, `ISurfaceProvider.h`,
   `Logging.h`, `LogSink.h/.cpp`, `Time.h/.cpp`) via `browse_dir` before
   deciding where the new files should live (mirroring existing granularity).
   Confirmed `Renderer`'s real constructor (`explicit Renderer(ISurfaceProvider&
   surfaceProvider);`, PHASE10), `rg::RenderGraph`'s real constructor
   (`explicit RenderGraph(Renderer& renderer);`), `Game`'s real constructor
   (`Game() = default;`, `Registry& GetRegistry() noexcept`), and
   `EngineContext`'s real shape (`struct EngineContext { Time time; };`) by
   reading each real header, rather than guessing signatures.
2. Confirmed no `FrameStats` type exists anywhere in the codebase yet
   (`search_in_dir`, zero matches) — Core's own frozen public contract commits
   to `GetFrameStats()` regardless. Added a deliberately EMPTY placeholder
   `struct FrameStats { };` inside `Core.h` itself, documented as never
   written to by anything in this 19-phase campaign (no later phase in the
   roadmap wires real `Profiling::FrameProfiler`/`DrawStats` data into it —
   confirmed via `search_in_dir` across every `PHASEn_*.md` file for
   "FrameStats", only this phase's own file mentions it).
3. **NEW `src/Core/InputFrame.h`** — `struct InputFrame { };`, a deliberate,
   documented placeholder (PHASE13 threads a real, populated value through
   `Core::Update()`'s signature; this phase only needs the type to exist).
4. **NEW `src/Core/IHostServices.h`** — `class IHostServices` with one pure
   virtual method, `Log(LogLevel, std::string_view)`, per the design doc's
   Section 5.2 contract. Uses `Core/Logging.h`'s already-existing, unconditional
   `LogLevel` (PHASE3) — no new logging type invented.
5. **NEW `src/Core/Core.h`/`.cpp`** — the `gte::Core` class, exactly matching
   PHASE12's own Step 1 code (accessors + `SetEditorLayerHook()`), with the
   two documented deviations above:
   - `Core.h` forward-declares `class IEditorLayer;` only (zero `#include` of
     `EditorLayer.h` at the header level) — `Core.cpp` is the one place that
     `#include`s the real `../Editor/EditorLayer.h` header, exactly mirroring
     `RenderSystem.h`'s pre-existing `FrameDebuggerCaptureContext*`
     forward-declaration precedent (Locked Design Decision #8).
   - `m_editorLayer` (nullable `IEditorLayer*`, default `nullptr`),
     `m_renderer`/`m_renderGraph`/`m_game`/`m_engineContext` (owned VALUE
     members — the real instances, physically relocated here from
     `Application`), and `m_frameStats` (the placeholder above).
   - `Update()`/`BuildFrame()`/`Present()` are empty stub bodies with a
     comment explaining PHASE13 gives them their real content — confirmed
     nothing calls them yet.
   - `SetEditorLayerHook()` is a trivial one-line pointer assignment.
6. **`CMakeLists.txt`** — registered the four new files
   (`InputFrame.h`/`IHostServices.h`/`Core.h`/`Core.cpp`) in `gte_core`'s
   unconditional source list, right after `ISurfaceProvider.h`.
7. **`src/Application/Application.h`**:
   - Added `#include "../Core/Core.h"`.
   - Added a private nested `struct ApplicationHostServices : IHostServices`
     (a trivial, TEMPORARY adapter routing `Log()` into the existing global
     `LogToActiveSink()` mechanism, `Core/LogSink.h` — NOT a second,
     competing logging path) plus a `m_hostServices` member — needed only
     because `Core`'s constructor requires a real `IHostServices&`.
     Documented as PHASE16's job to replace with `EditorHost`'s own,
     permanent implementation.
   - Added `Core m_core;`, declared right after `m_window`/`m_hostServices`
     (its own two constructor dependencies).
   - Converted `Renderer m_renderer;`/`rg::RenderGraph m_renderGraph;`/
     `Game m_game;`/`EngineContext m_engineContext;` from owned VALUE members
     into same-named REFERENCE members (`Renderer& m_renderer;`, etc.), each
     with an in-place doc comment explaining the deviation and pointing at
     this report.
   - `m_offscreenRenderPipeline`/`m_presentRenderPipeline` (the two
     `rg::RenderPipeline` instances) and every other existing member are
     completely UNCHANGED.
8. **`src/Application/Application.cpp`**:
   - Constructor initializer list: added `m_hostServices()` and
     `m_core(m_window, m_hostServices)`, then rebound
     `m_renderer`/`m_renderGraph`/`m_game`/`m_engineContext` to
     `m_core.GetRenderer()`/`m_core.GetRenderGraph()`/`m_core.GetGame()`/
     `m_core.GetEngineContext()` respectively (previously,
     `m_renderer(m_window)`/`m_renderGraph(m_renderer)`/`m_game()` were
     direct constructions; `m_engineContext` was never explicitly listed
     before — a reference member MUST be in the initializer list, so it is
     now explicit). `m_editorLayer(CreateEditorLayer(m_window, m_renderer))`
     is otherwise UNCHANGED (still constructs against the same `Renderer&`,
     now sourced from `m_core` instead of being a direct sibling member).
   - Added `m_core.SetEditorLayerHook(m_editorLayer.get());` as the second
     statement of the constructor BODY (right after the existing
     `InstallLogSink()` call, before `SetSceneIOCapability()`) — called from
     the body (never the initializer list) specifically because
     `m_editorLayer` must already be fully constructed first; safe
     regardless of `m_core`'s/`m_editorLayer`'s relative declaration order
     since the body only runs after every member's own constructor has
     already completed.
   - Confirmed via `git_status`/direct read that NOTHING ELSE in this
     2480-line file changed — every `m_renderer.`/`m_game.`/`m_renderGraph.`/
     `m_engineContext.` call site (including every lambda capturing `this`
     inside `RegisterOffscreenRenderPipelineProviders()`/
     `RegisterPresentRenderPipelineProvider()`/`Run()`) is byte-for-byte
     identical text to before this phase.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until PHASE14/19).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report).
- `cmake --build build --target gte_core` — **succeeded cleanly**, both
  BEFORE (`Core.cpp` compiling standalone, confirming the new files were
  self-consistent before touching `Application`) and AFTER the
  `Application.h`/`.cpp` edits.
- `cmake --build build --target gte_editor` — **succeeded cleanly** (no
  rebuild needed — `gte_editor`'s own sources don't transitively depend on
  anything this phase changed at the ABI/header level it includes).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**,
  full executable relinked with zero linker issues (unlike PHASE9's own
  `$<LINK_GROUP:RESCAN,...>` wrinkle — this phase does not touch which
  archive any function is defined in, only where data physically lives
  inside `gte_core`'s own two classes).
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly** (relink only — no test source touched by this phase).
- `ctest -R "SdlLinkageRegression|LoggerTest"` — **13/13 passed (100%)**,
  confirming this phase introduced no regression in either module (a quick,
  beyond-minimum spot-check, not the full suite).
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`
  (PID 17232), then via `gte_send_request`:
  - `GET /get_swapchain` — screenshot confirmed the Editor renders **exactly**
    as every prior phase's own documented baseline: docked Hierarchy/Scene/
    Game/Inspector panels, the Memory/Profiler/Render Graph/Atmosphere/Jobs/
    Log/Project tab bar, the Pause/Step toolbar, and the identical
    sky-gradient rendering in both Scene and Game panels — this phase changes
    zero rendering behavior (pure member relocation + reference-alias
    forwarding + a not-yet-called hook), and the screenshot confirms exactly
    that.
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors from boot.
  - `stop_app_background`'d the process (PID 17232) when done.

No `bug_report` was filed — every tool call behaved as expected this phase;
no anomaly was encountered.

## Definition of Done — checklist

- [x] `Core` class exists with the exact public contract PHASE12's own Step 1
      specifies (accessors + `SetEditorLayerHook()`), with two documented,
      justified deviations (reference-alias members in `Application` instead
      of a literal `m_core.GetX()` rewrite; the two `RenderPipeline`
      instances deferred to PHASE13) — both confirmed via `ask_questions`,
      user deferred the decision to me, both resolved in favor of the
      lower-risk option.
- [x] `Core.h` contains only a forward declaration of `IEditorLayer` — zero
      `#include` of `EditorLayer.h` itself at the header level (confined to
      `Core.cpp`, mirroring `RenderSystem.h`'s own precedent).
- [x] Zero behavior change confirmed via live boot/render smoke check
      (screenshot identical to every prior phase's own baseline) and via a
      targeted `ctest` spot-check.
- [x] `PHASE12_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of Scope (confirmed, unchanged)

- Did **not** move the actual per-frame orchestration logic
  (`Update()`/`BuildFrame()`/`Present()`'s real bodies) — both are empty
  stubs; `Application::Run()` still owns and drives the ENTIRE real
  per-frame loop, completely unmodified. That is PHASE13's job.
- Did **not** call any real `IEditorLayer` method through `m_core`'s new
  hook — `SetEditorLayerHook()` is called (wiring the pointer), but nothing
  inside `Core` ever dereferences `m_editorLayer` yet (PHASE13 does that,
  per Locked Design Decision #8's own call-site table).
- Did **not** move the two `rg::RenderPipeline` instances
  (`m_offscreenRenderPipeline`/`m_presentRenderPipeline`) into `Core` — see
  the "Genuine ambiguity" section above; deferred to PHASE13, which moves
  them together with their only real consumer.
- Did **not** touch any of `Application`'s ~30 `m_editorLayer->...()` call
  sites — completely untouched, exactly as this phase's own "Out of Scope"
  section requires (PHASE13 decides, per Locked Design Decision #8, which
  move behind `m_core`'s hook and which stay directly on `Application`'s own
  `m_editorLayer`).
- Did **not** wire any real data into `FrameStats`/`Core::GetFrameStats()` —
  a deliberate, documented, permanent-for-this-campaign placeholder (no
  phase in the 19-phase roadmap wires this up — confirmed via
  `search_in_dir`).
- Did **not** touch `GTE_ENABLE_PROJECT_PANEL`, `IEditorLayer` itself, or any
  CMake target-split concern — all out of scope, unchanged.

## Files touched

- NEW: `src/Core/InputFrame.h`
- NEW: `src/Core/IHostServices.h`
- NEW: `src/Core/Core.h`
- NEW: `src/Core/Core.cpp`
- MODIFIED: `CMakeLists.txt` (registered the four new files in `gte_core`'s
  unconditional source list)
- MODIFIED: `src/Application/Application.h` (new `Core.h` include; new
  private `ApplicationHostServices` struct + `m_hostServices` member; new
  `Core m_core` member; `m_renderer`/`m_renderGraph`/`m_game`/
  `m_engineContext` converted from owned value members to same-named
  reference members, each with a doc comment explaining why)
- MODIFIED: `src/Application/Application.cpp` (constructor initializer list
  updated to construct `m_hostServices`/`m_core` and bind the four reference
  members to `m_core`'s own accessors; added the
  `m_core.SetEditorLayerHook(m_editorLayer.get())` call in the constructor
  body)
- NEW: `task_manager/editor-core-separation-1/PHASE12_COMPLETION_REPORT.md`
  (this file)
