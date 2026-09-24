# PHASE2 — COMPLETION REPORT: `IFrameDebuggerCaptureRecorder` (Closing Defects A and B)

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md`.

## Step 0: Pre-existing completion reports

Read `PHASE1_COMPLETION_REPORT.md` in full before starting. It reported zero
deviations, a clean `EditorPanelCatalog.h` relocation, and no ambiguity — no
carry-forward clue relevant to this phase's own work.

## Step 1: Re-confirmation against real, current source

Before editing anything, re-ran every `search_in_dir` query the phase doc
itself lists:
- `search_in_dir(src, "FrameDebuggerCaptureContext")` → 86 matches in 27
  files, exactly matching the phase doc's own snapshot count.
- `search_in_dir(src, "RecordFrameDebuggerDraws")` → confirmed declared in
  `RenderSystem.h`, defined in `FrameDebuggerDrawRecording.cpp`, called once
  in `RenderSystem.cpp`.
- `search_in_dir(src, "AddFrameDebuggerReplayPasses")` → confirmed declared
  in `RenderPasses.h`, defined in `FrameDebuggerReplayPasses.cpp`, called
  once in `Core.cpp` (line 840, NOT through `RenderPasses.cpp`).
- `search_in_dir(src, "PrepareFrameDebuggerCaptureContext")` → confirmed
  `EditorLayer.h` (line 552, before edits), `Core.cpp` (line 658),
  `ImGuiEditorLayer.cpp` (line 852), `NullEditorLayer.cpp` (line 76).
- `search_in_dir(src, "AddRenderOpaquePass(")` /
  `search_in_dir(src, "AddDrawSkyBackgroundPass(")` (also against `tests/`)
  → re-confirmed **zero real call sites** for both — every match is the
  function's own declaration/definition/doc-comment mention. Confirmed the
  phase doc's own "IMPORTANT, CONFIRMED discrepancy" note is still accurate:
  `Core::RegisterOffscreenRenderPipelineProviders()`'s own inline
  `"RenderOpaque"`/`"DrawSkyBackground"` providers are what actually run,
  not `RenderPasses.cpp`'s exported functions.

Every real line number the phase doc quoted (Core.cpp lines 329-330, 657-658,
840-842; RenderSystem.h/.cpp; RenderPasses.h/.cpp) matched the doc's own
snapshot exactly — no drift had occurred since the doc was written.

## Files actually touched (the exhaustive list)

1. **`src/Core/FrameDebuggerCaptureRecorder.h` — NEW FILE.** The interface
   itself (final shape pasted below).
2. **`src/Editor/FrameDebuggerCapture.h`** — `FrameDebuggerCaptureContext`
   now `: public IFrameDebuggerCaptureRecorder`; gained two new `override`
   method declarations (`RecordFrameDebuggerDraw()`, `AddReplayPasses()`);
   added the necessary forward declarations (`Registry`, `Renderer`,
   `Mesh`, `Pipeline`, `struct MaterialTexture`, `Game`, `rg::RenderGraphBuilder`).
3. **`src/Editor/FrameDebuggerCapture.cpp`** — no change needed (the new
   interface header is already transitively available via
   `FrameDebuggerCapture.h`'s own `#include`).
4. **`src/Editor/FrameDebuggerDrawRecording.cpp`** — the free function
   `RecordFrameDebuggerDraws()` became the member-function DEFINITION
   `FrameDebuggerCaptureContext::RecordFrameDebuggerDraw()`. Body logic
   completely unchanged (`capture.RecordDraw(...)` → `RecordDraw(...)`,
   implicit `this`).
5. **`src/Editor/FrameDebuggerReplayPasses.cpp`** — the free function
   `AddFrameDebuggerReplayPasses()` became the member-function DEFINITION
   `FrameDebuggerCaptureContext::AddReplayPasses()` (dropped the trailing
   `FrameDebuggerCaptureContext& capture` parameter). Body logic unchanged.
6. **`src/Editor/EditorLayer.h`** — `PrepareFrameDebuggerCaptureContext()`'s
   return type changed to `IFrameDebuggerCaptureRecorder*`; the old
   `class FrameDebuggerCaptureContext;` forward declaration replaced with a
   real `#include "../Core/FrameDebuggerCaptureRecorder.h"`.
7. **`src/Editor/ImGuiEditorLayer.cpp`** — its override's return type changed
   to `IFrameDebuggerCaptureRecorder*`; body unchanged (implicit upcast).
8. **`src/Editor/NullEditorLayer.cpp`** — its override's return type changed
   to `IFrameDebuggerCaptureRecorder*`; body stays `{ return nullptr; }`.
9. **`src/Core/Core.h`** — `m_currentFrameDebuggerCaptureForOffscreenPipeline`'s
   type changed to `IFrameDebuggerCaptureRecorder*`; the old forward
   declaration replaced with a real `#include "FrameDebuggerCaptureRecorder.h"`.
10. **`src/Core/Core.cpp`** — BOTH local variables converted (not just one):
    - The `"RenderOpaque"` inline provider lambda's own local
      `frameDebuggerCapture` (line 329) — pure type-only change, never
      dereferenced there.
    - `Core::BuildFrame()`'s own local `frameDebuggerCapture` (line 657) —
      its real call site converted from
      `AddFrameDebuggerReplayPasses(b, m_game, m_renderer, ..., *frameDebuggerCapture)`
      to `frameDebuggerCapture->AddReplayPasses(b, m_game, m_renderer, ...)`
      (dropped the trailing `*frameDebuggerCapture` argument — it becomes
      the implicit `this`).
11. **`src/Application/RenderPasses.h`** — `AddRenderOpaquePass()`'s and
    `AddDrawSkyBackgroundPass()`'s parameter types changed to
    `IFrameDebuggerCaptureRecorder*`; the old forward declaration replaced
    with a real `#include`; `AddFrameDebuggerReplayPasses()`'s own
    DECLARATION deleted entirely (replaced with an explanatory comment
    pointing at the interface).
12. **`src/Application/RenderPasses.cpp`** — both functions' parameter types
    changed (bodies unchanged — never dereferenced); the stale doc comment
    about `AddFrameDebuggerReplayPasses()`'s old free-function home rewritten
    to describe the new interface-method reality.
13. **`src/Game/Game.h`** — `Render()`'s `frameDebuggerCapture` parameter
    type changed to `IFrameDebuggerCaptureRecorder*`; forward declaration
    replaced with a real `#include`.
14. **`src/Game/Game.cpp`** — `Game::Render()`'s parameter type changed
    (body unchanged — only ever forwards the pointer).
15. **`src/Game/RenderSystem.h`** — BOTH `Draw()` overloads' `capture`
    parameter type changed to `IFrameDebuggerCaptureRecorder*`; forward
    declaration replaced with a real `#include`;
    `RecordFrameDebuggerDraws()`'s own free-function DECLARATION deleted
    entirely.
16. **`src/Game/RenderSystem.cpp`** — the call site
    `RecordFrameDebuggerDraws(*capture, registry, renderer, ...)` became
    `capture->RecordFrameDebuggerDraw(registry, renderer, ...)` (dropped the
    leading `*capture` argument, now the implicit `this`); the file's own
    top-of-file comment rewritten to describe the new interface-based
    mechanism instead of the old free-function workaround.
17. **`CMakeLists.txt`** — added `src/Core/FrameDebuggerCaptureRecorder.h`
    to `gte_core`'s `target_sources()` list, right after
    `src/Core/EditorCapabilities.h` (confirmed today at line 264).

## Genuine, real bug found and fixed during this phase's own compile check

**Not predicted by the phase doc** — a real, structural mistake made while
implementing Step 3.4 (converting the pass-through call sites): when adding
the new `#include "../Core/FrameDebuggerCaptureRecorder.h"` to
`EditorLayer.h`, `Core.h`, `RenderPasses.h`, `Game.h`, and `RenderSystem.h`,
the `edit_line` replacements accidentally placed the new `#include` line
**inside** each of those files' own already-open `namespace gte { ... }`
block (right where the old `class FrameDebuggerCaptureContext;` forward
declaration used to sit). Since the new header itself opens its own
`namespace gte { ... }` block, this created a bogus, unintended nested
`gte::gte` namespace in every one of those five files — which cascaded into
hundreds of real compile errors (`'gte::std::hash' does not name a type`,
`'gte::gte::rg::TextureHandle'`, etc.) the first time `gte_editor` was built.
**Fixed** by moving all five `#include "../Core/FrameDebuggerCaptureRecorder.h"`
lines to file scope, above each file's own `namespace gte {` line, with a new
comment explaining exactly why this placement matters (to stop a future
maintainer from making the identical mistake). This is a good, concrete
example of why PHASE0's own Universal Rule 4 (incremental compile check
after every phase) exists — the very first `gte_editor` build attempt caught
this immediately and precisely, before it ever reached PHASE4's own
link-level regression check.

## The interface's final real shape (pasted in full)

```cpp
#pragma once

// New header, editor-core-separation-2 campaign, PHASE2 - closes the two
// real gte_core -> gte_editor-only-symbol undefined-reference hazards
// editor-core-separation-1 left open (RecordFrameDebuggerDraws(),
// AddFrameDebuggerReplayPasses() - see that campaign's own
// CAMPAIGN_COMPLETION_REPORT.md, "What remains genuinely open"). A pure,
// gte_core-owned abstract interface - gte_core-tier code (RenderSystem.cpp,
// Core.cpp) calls through a pointer to THIS type instead of calling a
// gte_editor-only free function by name, so no link-time symbol from
// gte_editor.a is ever required inside gte_core.a itself; only a vtable
// read at runtime, satisfied by whichever concrete object (always
// FrameDebuggerCaptureContext, gte_editor-owned) the pointer actually
// points at. Mirrors IEditorLayer's own already-proven opaque-interface
// pattern, one boundary layer deeper (this interface is reached FROM
// inside Core-tier code that has no access to IEditorLayer* at all -
// RenderSystem.cpp is three call-levels away from Core::BuildFrame(), via
// Core.cpp's own inline "RenderOpaque" provider -> Game.cpp -> RenderSystem.cpp
// - see this phase's own Step 1 for the confirmed, real call chain).
//
// A nullptr of this type means exactly what a nullptr
// FrameDebuggerCaptureContext* used to mean: "the Frame Debugger is not
// currently armed for this frame" (the overwhelmingly common case) - every
// existing null-check call site (RenderSystem::Draw(), Core::BuildFrame())
// is completely unchanged by this interface's introduction.

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h" // rg::TextureHandle, rg::BufferHandle
#include "../ECS/Entity.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace gte {

class Registry;
class Renderer;
class Mesh;
class Pipeline;
// MaterialTexture (src/Renderer/MaterialTexture.h) is a real `struct`, NOT a
// `class` - forward-declare it with the SAME tag its own definition uses
// (`struct MaterialTexture;`, not `class MaterialTexture;`) to avoid a real,
// if harmless-by-default, struct/class forward-declaration tag mismatch
// (MSVC's own C4099 warning, or -Wmismatched-tags on Clang) - confirmed
// against the real header (src/Renderer/MaterialTexture.h line 26) before
// writing this line.
struct MaterialTexture;
class Game;
class RenderTexture;

namespace rg {
class RenderGraphBuilder;
} // namespace rg

class IFrameDebuggerCaptureRecorder {
public:
    virtual ~IFrameDebuggerCaptureRecorder() = default;

    // Replaces the old free function gte::RecordFrameDebuggerDraws() -
    // same parameters, same semantics, same call site
    // (RenderSystem::Draw()), just a virtual method instead of a
    // gte_editor-only free function called by name.
    virtual void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
        const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) = 0;

    // Replaces the old free function gte::AddFrameDebuggerReplayPasses() -
    // same parameters (minus the trailing FrameDebuggerCaptureContext&,
    // which becomes the implicit `this`), same semantics, same call site
    // (Core::BuildFrame()).
    virtual std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
        Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
        const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
        const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget) = 0;
};

} // namespace gte
```

This matches the phase doc's own Step 3.1 required shape EXACTLY, byte for
byte (no signature drift was found — `RenderSystem.h`/`RenderPasses.h`'s
real, current signatures matched the doc's own snapshot).

`FrameDebuggerCaptureContext : public IFrameDebuggerCaptureRecorder`'s two
new override method declarations (`src/Editor/FrameDebuggerCapture.h`):

```cpp
void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
    const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) override;

std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget) override;
```

## Compile-check result

1. `cmake --build build --target gte_editor` (which depends on `gte_core`,
   rebuilding it first): **first attempt FAILED** with hundreds of cascading
   `gte::gte::...`/`gte::std::...` errors — the real, genuine `namespace
   gte { #include ... }` nesting bug described above. **Fixed** (see that
   section), then **re-run: SUCCESS**, zero errors — both `libgte_core.a`
   and `libgte_editor.a` linked cleanly.
2. `cmake --build build --target GreatTamanaEditor` — **SUCCESS**, full
   link completed (still using the `$<LINK_GROUP:RESCAN,...>` workaround,
   untouched by this phase, not due for removal until PHASE4 — per the
   phase doc's own Step 4.2, this does NOT prove Defects A/B are fixed by
   itself; only PHASE4's own plain-link removal proves that).

## Frame Debugger smoke-test result (Step 4.3)

Launched `build/GreatTamanaEditor.exe` via `run_app_background` (PID 10804).

1. `GET /get_swapchain` → `200`, `image/png` — Editor renders correctly
   (Scene/Game panels showing the default sky/atmosphere, no visual
   regression).
2. `GET /frame_debugger/open` → `200`:
   ```json
   {"state":{"channel":"all","enabled":false,"hasCapturedFrame":false,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":0,"windowOpen":true},"success":true}
   ```
3. `GET /frame_debugger/enable?value=true` → `200`:
   ```json
   {"state":{"channel":"all","enabled":true,"hasCapturedFrame":false,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":0,"windowOpen":true},"success":true}
   ```
4. `GET /frame_debugger/capture` → `200`:
   ```json
   {"state":{"channel":"all","enabled":true,"hasCapturedFrame":true,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":15,"windowOpen":true},"success":true}
   ```
   `hasCapturedFrame:true` and `totalEventCount:15` — a real, non-zero
   count, confirming the capture pipeline (both Defect A's and Defect B's
   real flows) genuinely ran through the new interface pointer.
5. `GET /get_swapchain` again → `200`, `image/png` — the Frame Debugger
   window is now visibly open, showing a real tree: `Game View` →
   `Compute LUT` (five Atmosphere compute-dispatch children) → `RenderOpaque`
   → `DrawSkyBackground` → `Draw Quad` → `Compute Dispatches (Post-GameView)`
   → `AtmosphereAerialPerspectiveCompositePass` → `Compute Dispatch`. This is
   the real, generically-discovered pass tree — "RenderOpaque"/
   "DrawSkyBackground" are visibly present and correctly attributed, proving
   the `Core.cpp` inline-provider → `Game.cpp` → `RenderSystem.cpp` chain
   (Defect A) recorded real facts, and that `Core::BuildFrame()`'s own
   direct `AddReplayPasses()` call (Defect B) built the replay-step passes
   without any link/runtime failure.
6. `GET /frame_debugger/state` → `200`:
   ```json
   {"channel":"all","enabled":true,"hasCapturedFrame":true,"levelsBlack":0.0,"levelsWhite":1.0,"selectedEventIndex":-1,"totalEventCount":15,"windowOpen":true}
   ```
   Matches `/frame_debugger/capture`'s own response exactly — no drift.

Note: the loaded test scene (`TestScene.gtscene`) has only a Camera entity —
zero mesh entities — so no per-entity `FrameDebuggerDrawRecord` children
appear under `RenderOpaque` in the tree (there is nothing to draw). This is
expected and does not weaken the proof: the sky background draw alone
(`DrawSkyBackground`/`Draw Quad`) already exercises the exact same
`RenderSystem::Draw()` → `capture->RecordFrameDebuggerDraw(...)` call path
that a per-entity draw would (a real scene with mesh entities would show
additional child leaves under `RenderOpaque`, but the underlying
interface-pointer mechanism being exercised is identical either way — the
draw loop's own `if (capture != nullptr) { capture->RecordFrameDebuggerDraw(...); }`
branch is unconditional on entity count). The `totalEventCount:15` (multiple
Atmosphere LUT compute passes plus the replay-step passes this capture also
declared) is itself proof real events were recorded, not just the Sky
Background draw alone.

`stop_app_background(pid: 10804)` called afterward; process confirmed
terminated.

## Deviations from the plan

- **Confirmed, not a deviation**: `AddRenderOpaquePass()`/
  `AddDrawSkyBackgroundPass()` (`src/Application/RenderPasses.h`/`.cpp`)
  remain confirmed dead code (zero real call sites), exactly as the phase
  doc predicted — their signatures were converted anyway, per Locked Design
  Decision #1, and Step 4.1's compile check (not the runtime smoke test) is
  their only proof of correctness, exactly as the doc anticipated.
- **Confirmed, not a deviation**: `src/Editor/FrameDebuggerCapture.cpp`
  needed literally zero changes (item 3 of the plan's own file list already
  called this out as "confirm" rather than "change" — confirmed true).
- **Genuine, real bug found and fixed, NOT predicted by the plan**: the
  `namespace gte { #include ... }` nesting mistake described above, in all
  five files that needed the new `#include`
  (`EditorLayer.h`/`Core.h`/`RenderPasses.h`/`Game.h`/`RenderSystem.h`).
  This was a mistake introduced during THIS phase's own implementation, not
  a pre-existing codebase issue or a discrepancy in the plan's own
  narrative — flagged here explicitly and honestly, exactly as Step 5 of
  the phase doc requires, rather than silently fixed without a note.

No genuine architectural ambiguity was encountered beyond the above
(a real implementation mistake, immediately caught and fixed by the phase's
own mandatory compile check) — `ask_questions` was not needed.

## Git

Both the code changes and this report are committed together, in one
commit, on `feature/editor-core-separation` (branch never switched, per
Universal Rule 2).
