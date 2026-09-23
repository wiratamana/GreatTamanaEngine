# PHASE12 — `Core` Class: Skeleton, Construction, Accessors

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 10 (`ISurfaceProvider` must
exist) and Phase 3 (`IHostServices`-style logging exists — actually
`IHostServices` itself is declared HERE, this phase, since it's part of
`Core`'s own public contract; the logging plumbing from Phase 3 is a
separate, lower-level mechanism `IHostServices::Log` can optionally
delegate to, or it can be independent — decide during execution and record
the choice). See also Locked Design Decision #8 (the `IEditorLayer*` hook
this phase must declare) — read it before starting.

## Step 1: The Goal

A new class, `gte::Core`, exists inside `gte_core`'s target: the engine's
public facade, constructed by injecting `ISurfaceProvider&` and
`IHostServices&`. This phase builds ONLY the skeleton — members, the
constructor, the plain accessors (`GetRenderer()`, `GetRegistry()`,
`GetGame()`, `GetRenderGraph()`, `GetEngineContext()`, `GetTime()`,
`GetFrameStats()`), AND one more required member per Locked Design Decision
#8: a nullable `IEditorLayer* m_editorLayer = nullptr;` hook, with a setter.
This phase does NOT yet move the actual per-frame orchestration logic (that
is Phase 13's job — called out separately because the design doc itself
flags it as "genuine surgery, not mechanical") and does NOT yet call any
`IEditorLayer` method (Phase 13 does that too) — it only declares the hook
and proves it compiles/constructs safely as `nullptr`.

## Step 2: The Situation / The Problem

Read `src/Application/Application.h`/`.cpp` in full, current state. List
every member that conceptually belongs to "Core"-ish stuff per the design
doc's own Section 2.1 ownership graph: `Renderer`, `rg::RenderGraph`, two
`rg::RenderPipeline` instances, `Game`, `EngineContext`/`Time`. Confirm this
list against the REAL current member list of `Application` (it may have
grown/changed since the design doc was written).

Also read `src/Editor/EditorLayer.h` in full, current state — specifically
`IEditorLayer`'s method list and the `FrameDebuggerCaptureContext` forward
declaration it already carries. This header is the ONE documented exception
to "`gte_core` never includes anything under `src/Editor/`" (design doc
Section 10's own checklist, and `PHASE0`'s Locked Design Decision #8) — `Core`
is allowed to `#include "../Editor/EditorLayer.h"` and hold a bare,
nullable `IEditorLayer*` pointer, exactly the way `RenderPasses.h`/
`RenderSystem.h` already hold a bare, nullable `FrameDebuggerCaptureContext*`
today (design doc Section 2.4).

## Step 3: The Plan

1. Design doc Section 5.2's exact public contract (reproduce faithfully,
   adjust only if a real, concrete blocker is found reading the actual
   code), EXTENDED with the `IEditorLayer*` hook Locked Design Decision #8
   requires:
   ```cpp
   // gte_core public header - no SDL, no full Vulkan loader beyond bare
   // handle typedefs. The ONE exception to "no Editor types" is
   // IEditorLayer itself (forward-declared here, real header included in
   // Core.cpp only) - see PHASE0's Locked Design Decision #8.
   struct InputFrame { /* buttons, axes, text input, mouse delta, ... */ };

   class IHostServices {
   public:
       virtual ~IHostServices() = default;
       virtual void Log(LogLevel level, std::string_view message) = 0;
   };

   class IEditorLayer; // src/Editor/EditorLayer.h - forward-declared only.

   class Core {
   public:
       Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices);

       void Update(const InputFrame& input, float deltaTime);
       void BuildFrame();
       void Present();

       Renderer&        GetRenderer();
       Registry&        GetRegistry();
       Game&            GetGame();
       rg::RenderGraph& GetRenderGraph();
       EngineContext&   GetEngineContext();
       Time&            GetTime();
       const FrameStats& GetFrameStats() const;

       // Locked Design Decision #8 (PHASE0): the ONE nullable, "big" opaque
       // hook mirroring FrameDebuggerCaptureContext*'s own already-proven
       // small-scale pattern. EditorHost (gte_editor) supplies the real
       // ImGuiEditorLayer instance after constructing it; a Player host
       // never calls this, leaving it nullptr forever. Core::BuildFrame()
       // (Phase 13) calls through this pointer, ALWAYS null-checked, for
       // ONLY the render-graph-frame-building subset of IEditorLayer's
       // methods enumerated in PHASE0 - never for UI-building/input-routing
       // methods, which stay a host-level (Application/EditorHost) concern
       // and never reach Core at all.
       void SetEditorLayerHook(IEditorLayer* editorLayer) noexcept;

   private:
       IEditorLayer* m_editorLayer = nullptr;
       // ... other members below ...
   };
   ```
   Create this in `src/Core/Core.h`/`.cpp` (new files, next to the
   already-existing `Time.h`/`EngineContext.h`).
2. Also create `src/Core/InputFrame.h` and `src/Core/IHostServices.h` as
   separate small headers if that keeps `Core.h` itself cleaner — decide
   based on what already exists in `src/Core/` (mirror its existing
   file-per-concept granularity). `Core.h` itself only needs a forward
   declaration of `IEditorLayer` (`class IEditorLayer;` inside
   `namespace gte`) — `Core.cpp` is the one place that
   `#include "../Editor/EditorLayer.h"` for real (needed once Phase 13
   actually calls through the pointer; harmless to include a frame early,
   in this phase, even before Phase 13 adds real call sites).
3. Move `m_renderer`, `m_renderGraph`, both `RenderPipeline` instances,
   `m_game`, the `EngineContext`/`Time` instance OUT of `Application` and
   INTO `Core` as its own members. `Application`'s own member list shrinks
   correspondingly — `Application` temporarily now HOLDS a `Core` instance
   (constructed with its own `Window` cast as `ISurfaceProvider&`, and some
   temporary `IHostServices` implementation — even a trivial one that just
   calls `GTE_LOG_*`/the new `LogToActiveSink` from Phase 3 is fine as a
   placeholder; `EditorHost`, Phase 15, is the REAL long-term owner of a
   proper `IHostServices` implementation). Immediately after constructing
   `m_core`, `Application`'s constructor calls
   `m_core.SetEditorLayerHook(m_editorLayer.get())` — `m_editorLayer` is
   already constructed by this point in `Application`'s existing member
   initializer order (confirm via reading `Application.h`'s real member
   declaration order — `m_editorLayer` must be declared/constructed BEFORE
   `m_core` for this to be safe, or the wiring call must move to the first
   line of the constructor BODY instead of another member's initializer;
   verify and pick whichever keeps this safe).
4. Implement `Core`'s accessors as plain forwarding methods to the moved
   members. Implement `SetEditorLayerHook()` as a trivial one-line pointer
   assignment. Do NOT implement `Update()`/`BuildFrame()`/`Present()`'s real
   bodies yet in this phase — stub them as empty or as a direct passthrough
   to whatever `Application::Run()` still does today (Phase 13 does the
   real extraction, including the FIRST real call through `m_editorLayer`).
   Document clearly in code comments that these three methods are
   placeholders pending Phase 13.
5. Update every `Application`-internal call site that used to reach
   `m_renderer`/`m_game`/etc directly to instead go through
   `m_core.GetRenderer()`/`m_core.GetGame()`/etc. Do NOT touch any of
   `Application`'s existing ~30 `m_editorLayer->...()` call sites yet
   (Phase 13 decides, per PHASE0's Locked Design Decision #8, exactly which
   of those move behind `m_core`'s new hook and which stay directly on
   `Application`'s own `m_editorLayer`).
6. Compile-check: incremental build. This phase should NOT change any
   runtime behavior at all (pure member relocation + forwarding + a
   not-yet-called hook) — live smoke check: `run_app_background`, confirm
   the engine boots and renders identically to before this phase.

## Files Touched

- NEW `src/Core/Core.h`/`.cpp`, `src/Core/InputFrame.h`,
  `src/Core/IHostServices.h`
- `src/Application/Application.h`/`.cpp` (shrink, add `Core m_core` member,
  wire `SetEditorLayerHook()`)
- `CMakeLists.txt` (add new `src/Core/` files to `gte_core`'s source list)

## Definition of Done

- `Core` class exists with the exact public contract above (accessors +
  `SetEditorLayerHook()`), or a documented, justified deviation.
- `Core.h` contains only a forward declaration of `IEditorLayer` — zero
  `#include` of `EditorLayer.h` itself at the header level (that stays
  confined to `Core.cpp`, mirroring how `RenderSystem.h` already forward-
  declares `FrameDebuggerCaptureContext` today).
- Zero behavior change confirmed via live boot/render smoke check.
- `PHASE12_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do NOT move the actual per-frame orchestration logic
(`Update`/`BuildFrame`/`Present`'s real bodies, or any real call through
`m_editorLayer`) — that is Phase 13, deliberately kept separate since the
design doc itself warns this is real, carefully-sequenced surgery (offscreen
regime, present regime, GPU-skinning dispatch requests, per-view data,
Atmosphere passes, GPU-driven batch culling readback, PLUS the ~30
`IEditorLayer` call sites this strategy's own double-check pass found
threaded through the same body), not a mechanical cut-and-paste.
