# PHASE2 — `IFrameDebuggerCaptureRecorder`: Closing Defects A and B

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it in full first, especially
Locked Design Decision #1 and Defects A/B in Step 2. This is the highest-risk,
highest-value phase in this whole campaign. Take your time; re-read every
file below in full (not just the excerpts quoted here) before editing it —
this strategy's own quoted line numbers may have drifted since it was
written; always re-confirm with `search_in_dir`/`read_file` first.

## Step 1: The Goal (Where are we going?)

Today, two real functions exist ONLY in `gte_editor.a` and are called BY
NAME from `gte_core`-tier code:
- `gte::RecordFrameDebuggerDraws(FrameDebuggerCaptureContext&, ...)` —
  declared in `src/Game/RenderSystem.h`, defined in
  `src/Editor/FrameDebuggerDrawRecording.cpp`, called from
  `src/Game/RenderSystem.cpp`.
- `gte::AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder&, Game&, ...,
  FrameDebuggerCaptureContext&)` — declared in
  `src/Application/RenderPasses.h`, defined in
  `src/Editor/FrameDebuggerReplayPasses.cpp`, called directly from
  `src/Core/Core.cpp` (`Core::BuildFrame()`'s own body — NOT through
  `RenderPasses.cpp`; see the important discrepancy note below).

Both make `gte_core.a` fail to link standalone (a real `undefined reference`
each). The fix: introduce a new abstract interface, `IFrameDebuggerCaptureRecorder`
(pure-virtual, `gte_core`-owned), have `FrameDebuggerCaptureContext`
(`gte_editor`-owned) implement it, and change the STATIC TYPE of every bare
pointer to it that flows through `gte_core`-tier code, from the concrete
`FrameDebuggerCaptureContext*` to the abstract `IFrameDebuggerCaptureRecorder*`.

There are TWO independent real pointer flows to convert, not one single
linear chain — confirm both yourself with `read_line`/`search_in_dir` before
editing, since the real, current code does NOT match a simple 4-level chain:

- **Defect A's real flow (3 call-levels deep, through `Core.cpp`'s own inline
  provider, NOT through `RenderPasses.cpp`)**: `IEditorLayer::
  PrepareFrameDebuggerCaptureContext()`'s result is stored in
  `Core::BuildFrame()`'s local `frameDebuggerCapture`, copied every frame into
  `Core`'s own `m_currentFrameDebuggerCaptureForOffscreenPipeline` member
  (inside `Core::RegisterOffscreenRenderPipelineProviders()`'s own INLINE
  `"RenderOpaque"` provider lambda in `Core.cpp` — **not**
  `src/Application/RenderPasses.cpp`'s `AddRenderOpaquePass()`), forwarded
  straight into a direct `m_game.Render(...)` call from that SAME lambda, then
  `Game::Render()` → `RenderSystem::Draw()` → `RecordFrameDebuggerDraws(*capture, ...)`.

  **IMPORTANT, CONFIRMED discrepancy versus this campaign's own
  `PHASE0_MASTER_STRATEGY.md` narrative**: `src/Application/RenderPasses.cpp`'s
  `AddRenderOpaquePass()` and `AddDrawSkyBackgroundPass()` functions are, as of
  this writing, CONFIRMED (by `search_in_dir`) to have **ZERO real call sites**
  anywhere in `src/` or `tests/` — re-run
  `search_in_dir(src, "AddRenderOpaquePass(")` /
  `search_in_dir(src, "AddDrawSkyBackgroundPass(")` /
  `search_in_dir(tests, "AddRenderOpaquePass(")` yourself; every match you get
  back will be the function's own declaration/definition/doc-comment mentions,
  never an actual call expression. They are dead, orphaned code today,
  superseded by `Core::RegisterOffscreenRenderPipelineProviders()`'s own
  inline `"RenderOpaque"`/`"DrawSkyBackground"` `RenderPipeline` providers
  (added by the `editor-core-separation-1` campaign's own PHASE13 relocation,
  which reimplemented this per-frame pass-declaration logic directly inside
  `Core.cpp` rather than continuing to call into `RenderPasses.cpp`'s
  exported, standalone functions). PHASE0's own Locked Design Decision #1
  text, describing the flow as "`Core.cpp` → `RenderPasses.cpp` → `Game.cpp`
  → `RenderSystem.cpp`", is therefore factually wrong for Defect A on the
  real, current codebase. Do not be surprised or confused when converting
  `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`'s own
  `FrameDebuggerCaptureContext*` parameter (Step 2 below, file-list items
  11-12) turns out to touch code that is never actually exercised at runtime
  — convert them anyway (see the paragraph below for exactly why), just do
  not expect Step 4's own runtime smoke test to exercise them; it can't,
  since nothing calls them today.

- **Defect B's real flow (1 call-level deep, directly inside `Core::BuildFrame()`
  itself — this part of PHASE0's narrative IS accurate)**: `Core::BuildFrame()`
  has its OWN local `frameDebuggerCapture` variable (a SEPARATE local from the
  one described above, scoped to `BuildFrame()` itself, not to
  `RegisterOffscreenRenderPipelineProviders()`), which is dereferenced
  directly at its own `AddFrameDebuggerReplayPasses(..., *frameDebuggerCapture)`
  call site, further down the same function body. See Step 2's file list,
  item 10, for the exact real code.

`AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()` (`src/Application/
RenderPasses.h`/`.cpp`) STILL MUST have their `FrameDebuggerCaptureContext*`
parameter's static type converted as part of this phase (Step 2's file list,
items 11-12, is otherwise correct and complete) — not because doing so fixes
any live undefined-reference hazard (it doesn't; these two functions never
call `RecordFrameDebuggerDraws()`/`AddFrameDebuggerReplayPasses()` themselves,
only ever forward the pointer into `game.Render(...)`, which is already fixed
independently by `Game.h`/`.cpp`'s own conversion, items 13-14), but because
Locked Design Decision #1 (`PHASE0_MASTER_STRATEGY.md`) requires EVERY
`gte_core`-tier file to drop its concrete `FrameDebuggerCaptureContext*`
type/forward-declaration entirely, with zero exception for currently-dead
code — `RenderPasses.cpp` is compiled into `gte_core` unconditionally (root
`CMakeLists.txt`'s `add_library(gte_core STATIC ...)` source list already
lists it), so a stray concrete-type forward declaration left there would
still be a real, live violation of that rule even though it causes no link
error today. (This dead-code situation is a pre-existing fact about the
codebase, not something this phase is responsible for cleaning up beyond the
type conversion — deleting the orphaned functions outright is explicitly OUT
OF SCOPE for this phase; if a future maintainer wants to delete them, that is
a separate, independent cleanup with its own risk profile, not part of
closing Defects A/B.)

Every one of the pass-through files in Step 2's file list already only ever
holds/forwards a bare pointer (never dereferences it) — this is proven true
by direct reading of every one of them during this strategy's own research;
re-confirm it yourself for each file before editing, but expect no surprises.
The only two REAL dereferences (`RecordFrameDebuggerDraws(*capture, ...)` in
`RenderSystem.cpp`, and `AddFrameDebuggerReplayPasses(..., *frameDebuggerCapture)`
in `Core.cpp`) become virtual method calls through the pointer instead of
free-function calls by name — this is what removes the undefined-reference
hazard: a virtual call through a pointer to a COMPLETE abstract-interface type
needs only a vtable read at runtime (satisfied by whatever concrete object the
pointer actually points at, which only ever exists inside `gte_editor.a`'s
own compiled code) — it requires ZERO link-time symbol in `gte_core.a`
itself, unlike a direct call to a free function by name.

## Step 2: The Situation (Where are we now?) — confirmed file inventory

Re-run every one of these `search_in_dir` queries yourself before editing
anything, to catch any drift from this document's own snapshot:
- `search_in_dir(src, "FrameDebuggerCaptureContext")` — expect ~86 matches in
  ~27 files (confirmed exact as of this writing); most are `gte_editor`-tier
  files that need ZERO changes (they keep using the concrete type directly —
  see the explicit "files that do NOT change" list at the end of this Step).
- `search_in_dir(src, "RecordFrameDebuggerDraws")`
- `search_in_dir(src, "AddFrameDebuggerReplayPasses")`
- `search_in_dir(src, "PrepareFrameDebuggerCaptureContext")`
- `search_in_dir(src, "AddRenderOpaquePass(")` and
  `search_in_dir(src, "AddDrawSkyBackgroundPass(")` (also re-run against
  `tests/`) — confirm for yourself, fresh, that these two functions still
  have zero real call sites anywhere (see Step 1's own discrepancy note
  above) before you convert their signatures, so you go in with accurate
  expectations about what Step 4's runtime smoke test can and cannot prove.

### Files that DO change (the exhaustive list)

1. **`src/Core/FrameDebuggerCaptureRecorder.h` — NEW FILE.** The interface
   itself. See Step 3.1 for its exact required shape.
2. **`src/Editor/FrameDebuggerCapture.h`** — `FrameDebuggerCaptureContext`
   gains `: public IFrameDebuggerCaptureRecorder` and two new `override`
   method declarations.
3. **`src/Editor/FrameDebuggerCapture.cpp`** — needs the new `#include` of
   the interface header (transitively already available via
   `FrameDebuggerCapture.h`, but confirm).
4. **`src/Editor/FrameDebuggerDrawRecording.cpp`** — the free function
   `RecordFrameDebuggerDraws()` becomes the member-function DEFINITION
   `FrameDebuggerCaptureContext::RecordFrameDebuggerDraw()`.
5. **`src/Editor/FrameDebuggerReplayPasses.cpp`** — the free function
   `AddFrameDebuggerReplayPasses()` becomes the member-function DEFINITION
   `FrameDebuggerCaptureContext::AddReplayPasses()`.
6. **`src/Editor/EditorLayer.h`** — `PrepareFrameDebuggerCaptureContext()`'s
   return type changes from `FrameDebuggerCaptureContext*` to
   `IFrameDebuggerCaptureRecorder*`; its forward declaration
   (`class FrameDebuggerCaptureContext;`) is replaced/supplemented with an
   `#include "../Core/FrameDebuggerCaptureRecorder.h"` (this header is
   `gte_core`-owned and ImGui/SDL-free, so `EditorLayer.h` — the one
   documented `gte_core`-visible exception file — including it is
   completely safe and mirrors how `EditorLayer.h` already includes other
   `gte_core` headers it needs).
7. **`src/Editor/ImGuiEditorLayer.cpp`** — its `override` of
   `PrepareFrameDebuggerCaptureContext()` changes return type to
   `IFrameDebuggerCaptureRecorder*` (the body itself,
   `return m_frameDebuggerPanel.PrepareCaptureContextForThisFrame(m_ctx);`,
   is UNCHANGED — `PrepareCaptureContextForThisFrame()` still returns the
   concrete `FrameDebuggerCaptureContext*`, which implicitly upcasts to
   `IFrameDebuggerCaptureRecorder*` at the `return` statement, legally,
   because `FrameDebuggerCapture.h`'s complete definition IS visible in this
   translation unit).
8. **`src/Editor/NullEditorLayer.cpp`** — its `override` of
   `PrepareFrameDebuggerCaptureContext()` changes return type to
   `IFrameDebuggerCaptureRecorder*` (body stays `{ return nullptr; }`).
9. **`src/Core/Core.h`** — `m_currentFrameDebuggerCaptureForOffscreenPipeline`'s
   declared type changes from `FrameDebuggerCaptureContext*` to
   `IFrameDebuggerCaptureRecorder*`; its own forward declaration
   (`class FrameDebuggerCaptureContext;`) is replaced with an `#include`
   of the new interface header (or a forward declaration of the interface
   class, your choice — including the header directly is simpler here since
   `Core.h` already needs `EditorLayer.h`'s declarations transitively and the
   new header is tiny/dependency-free).
10. **`src/Core/Core.cpp`** — TWO SEPARATE local variables both need this
    same type change (do not miss the second one — see Step 1's own "two
    independent flows" note above):
    - The local `frameDebuggerCapture` declared near the top of
      `Core::BuildFrame()` (fed from `m_editorLayer->PrepareFrameDebuggerCaptureContext()`)
      changes from `FrameDebuggerCaptureContext*` to
      `IFrameDebuggerCaptureRecorder*`. Its own call site
      `AddFrameDebuggerReplayPasses(b, m_game, m_renderer, gameAspectForReplay,
      objectCount, gpuSkinningBuffersForReplay, recordGameSkyBackground,
      *gameTarget, *frameDebuggerCapture)` becomes
      `frameDebuggerCapture->AddReplayPasses(b, m_game, m_renderer,
      gameAspectForReplay, objectCount, gpuSkinningBuffersForReplay,
      recordGameSkyBackground, *gameTarget)` (drop the trailing
      `*frameDebuggerCapture` argument — it becomes the implicit `this` of
      the method call instead). The surrounding `if (gameTarget != nullptr &&
      frameDebuggerCapture != nullptr && m_editorLayer != nullptr && ...)`
      guard is UNCHANGED.
    - The SEPARATE local `frameDebuggerCapture` declared inside
      `Core::RegisterOffscreenRenderPipelineProviders()`'s own inline
      `"RenderOpaque"` provider lambda (`FrameDebuggerCaptureContext*
      frameDebuggerCapture = isGameView ? m_currentFrameDebuggerCaptureForOffscreenPipeline
      : nullptr;`) also changes from `FrameDebuggerCaptureContext*` to
      `IFrameDebuggerCaptureRecorder*` — this one is NEVER dereferenced here
      (only forwarded straight into `m_game.Render(...)`), so this is a pure,
      mechanical type-only change, consistent with `m_currentFrameDebuggerCaptureForOffscreenPipeline`'s
      own type change (item 9) it is assigned from/to.
11. **`src/Application/RenderPasses.h`** — `AddRenderOpaquePass()`'s and
    `AddDrawSkyBackgroundPass()`'s `frameDebuggerCapture` parameter type
    changes to `IFrameDebuggerCaptureRecorder*`; its own forward declaration
    (`class FrameDebuggerCaptureContext;`) is replaced with an `#include` of
    the new interface header. **`AddFrameDebuggerReplayPasses()`'s own
    DECLARATION is DELETED entirely from this header** — it is no longer a
    free function at all; `Core.cpp` now calls it as a method (see item 10).
    Reminder (Step 1): `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`
    are confirmed dead code (zero real call sites) today — convert their
    signatures anyway, per Locked Design Decision #1; do not expect this
    change to be observable at runtime.
12. **`src/Application/RenderPasses.cpp`** — `AddRenderOpaquePass()`'s and
    `AddDrawSkyBackgroundPass()`'s parameter type changes (their BODIES are
    otherwise unchanged — they only ever forward the pointer onward, never
    dereference it, confirmed during this strategy's own research). The
    header-comment block explaining "AddFrameDebuggerReplayPasses() used to
    be defined here..." should be updated/removed since it now describes a
    function that no longer exists as a free function anywhere. As noted in
    Step 1, both converted functions remain confirmed dead code (zero real
    call sites) even after this change — this is expected, not a sign
    something else was missed.
13. **`src/Game/Game.h`** — `Render()`'s `frameDebuggerCapture` parameter
    type changes to `IFrameDebuggerCaptureRecorder*`; its own forward
    declaration (`class FrameDebuggerCaptureContext;`) is replaced with an
    `#include` of the new interface header.
14. **`src/Game/Game.cpp`** — `Game::Render()`'s parameter type changes
    (body unchanged — only ever forwards the pointer into
    `m_renderSystem.Draw(...)`).
15. **`src/Game/RenderSystem.h`** — BOTH `Draw()` overloads' `capture`
    parameter type changes from `FrameDebuggerCaptureContext*` to
    `IFrameDebuggerCaptureRecorder*`; its own forward declaration
    (`class FrameDebuggerCaptureContext;`) is replaced with an `#include` of
    the new interface header. **`RecordFrameDebuggerDraws()`'s own free
    function DECLARATION is DELETED entirely from this header.**
16. **`src/Game/RenderSystem.cpp`** — the call site
    `RecordFrameDebuggerDraws(*capture, registry, renderer, command.entity,
    *mesh, *pipeline, materialTexture, viewProjection)` becomes
    `capture->RecordFrameDebuggerDraw(registry, renderer, command.entity,
    *mesh, *pipeline, materialTexture, viewProjection)` (drop the leading
    `*capture` argument — it becomes the implicit `this` of the method call
    instead; the surrounding `if (capture != nullptr) { ... }` guard is
    UNCHANGED). Its own header comment explaining the free-function
    workaround should be updated to describe the new interface-based
    mechanism instead (do not leave the OLD "genuine link hazard... declared
    here, defined only in a new Editor-side file" explanation standing
    unmodified next to code that no longer works that way).

### Files that do NOT change (confirm this yourself, do not just trust this list blindly)

Every file that consumes `FrameDebuggerCaptureContext`'s FULL, concrete API
(`Reset()`, `DrawRecords()`, `PipelineDebugNames()`,
`MaterialTextureDebugNames()`, `SetReplayStepPreviews()`/
`ReplayStepPreviews()`, `LastViewProjection()`, `DrawCallCount()`) stays
completely unchanged, since these all live entirely inside `gte_editor` and
never cross the `gte_core`/`gte_editor` boundary as a bare pointer:
`src/Editor/FrameDebuggerData.h/.cpp`, `src/Editor/FrameDebuggerHistory.h/.cpp`,
`src/Editor/Panels/FrameDebuggerPanel.h/.cpp` (its own
`PrepareCaptureContextForThisFrame()` method KEEPS returning the concrete
`FrameDebuggerCaptureContext*` — only `ImGuiEditorLayer.cpp`'s call site,
one level up, upcasts it when returning from the `IEditorLayer` override),
and every test file under `tests/Editor/` that constructs a
`FrameDebuggerCaptureContext` directly and calls `.RecordDraw()`/
`.RecordEntityDraw()`/etc. on it AS THE CONCRETE TYPE (re-confirm via
`search_in_dir(tests, "FrameDebuggerCaptureContext")` that none of them call
the two OLD free functions by name — this strategy's own research found
none across the three files that reference the type
(`FrameDebuggerCaptureTests.cpp`, `FrameDebuggerDataTests.cpp`,
`FrameDebuggerSnapshotBuilderTests.cpp`), re-confirm).

Also unaffected: `src/Core/EditorCapabilities.h`, `src/Core/LogSink.h`,
`src/Game/DrawCommand.h`, `src/Game/Instantiation/MaterialTextureGpuCache.cpp`,
`src/Renderer/Pipeline.h`, `src/Renderer/Renderer.h` — each of these merely
MENTIONS `FrameDebuggerCaptureContext` inside a doc comment (prose, not code),
so none of them need any edit at all.

## Step 3: The Plan — exact steps

### Step 3.1 — Write the new interface header

Create `src/Core/FrameDebuggerCaptureRecorder.h`. Required shape (adjust
types/includes to match whatever `RenderSystem.h`'s current real signatures
are — re-read `RenderSystem.h`/`RenderPasses.h` in full immediately before
writing this, to copy every parameter type exactly, including
`const std::function<void(VkCommandBuffer)>&` and
`std::vector<rg::TextureHandle>` return type, AND every forward-declared
type's exact `class`/`struct` tag — see the `MaterialTexture` note below,
this is not a hypothetical risk, it was a real mistake in an earlier draft
of this exact document):

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
// (MSVC's own C4099 warning, or -Wmismatched-tags on Clang) - confirm this
// yourself against the real header before writing this line.
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

Cross-check every include/forward-declaration above against the REAL,
current `RenderSystem.h`/`RenderPasses.h`/`MaterialTexture.h` before
finalizing — if any signature (or forward-declared type's own `class`/
`struct` tag) has drifted (e.g. a new parameter added by an unrelated
campaign since this strategy was written, or a struct that became a class),
match the REAL current code, not this document's snapshot.

### Step 3.2 — `FrameDebuggerCaptureContext` implements the interface

In `src/Editor/FrameDebuggerCapture.h`:
- `#include "../Core/FrameDebuggerCaptureRecorder.h"`.
- Change `class FrameDebuggerCaptureContext {` to
  `class FrameDebuggerCaptureContext : public IFrameDebuggerCaptureRecorder {`.
- Add two new `override` method DECLARATIONS (bodies live in the two `.cpp`
  files below), matching the interface exactly, e.g.:
  ```cpp
  void RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, Entity entity, const Mesh& mesh,
      const Pipeline& pipeline, const MaterialTexture* materialTexture, const Mat4& viewProjection) override;

  std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
      float aspectWidthOverHeight, std::size_t objectCount,
      const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
      const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget) override;
  ```
- You will need additional forward declarations/`#include`s in this header
  for `Registry`, `Renderer`, `Mesh`, `Pipeline`, `MaterialTexture` (as
  `struct MaterialTexture;` — see Step 3.1's own note on why the tag
  matters), `Game`, `rg::RenderGraphBuilder` (bare forward declarations are
  enough for a declaration-only header — check what `RenderSystem.h`/
  `RenderPasses.h` already do for the exact same types and mirror it; note
  `RenderTexture` needs NO new forward declaration here, since
  `FrameDebuggerCapture.h` already `#include`s the real, complete
  `../Renderer/RenderTexture.h` for its own pre-existing
  `m_replayStepPreviews` member).

### Step 3.3 — Move the two free function BODIES into member-function definitions

In `src/Editor/FrameDebuggerDrawRecording.cpp`:
- Change `void RecordFrameDebuggerDraws(FrameDebuggerCaptureContext& capture, Registry& registry, ...)`
  to `void FrameDebuggerCaptureContext::RecordFrameDebuggerDraw(Registry& registry, Renderer& renderer, ...)`.
- Every place the OLD body referred to `capture.RecordDraw(...)`/
  `capture.RecordEntityDraw(...)` becomes `RecordDraw(...)`/
  `RecordEntityDraw(...)` (an implicit `this->`, since this is now a member
  function of the very class those methods belong to).
- Update the file's own top-of-file header comment — it currently explains
  "this file is the NEW home for the real body of RenderSystem::Draw()'s own
  former `#if GTE_ENABLE_EDITOR` block... RecordFrameDebuggerDraws() is
  DECLARED in RenderSystem.h... DEFINED for real only here" — rewrite this to
  describe the NEW reality: this file now defines
  `FrameDebuggerCaptureContext::RecordFrameDebuggerDraw()`, the concrete
  implementation of `IFrameDebuggerCaptureRecorder::RecordFrameDebuggerDraw()`
  (`src/Core/FrameDebuggerCaptureRecorder.h`), reachable from
  `gte_core`-tier `RenderSystem::Draw()` only through a virtual call on a
  null-checked interface pointer, never a gte_editor-only free-function
  symbol.

In `src/Editor/FrameDebuggerReplayPasses.cpp`:
- Change `std::vector<rg::TextureHandle> AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game, ..., FrameDebuggerCaptureContext& capture)`
  to `std::vector<rg::TextureHandle> FrameDebuggerCaptureContext::AddReplayPasses(rg::RenderGraphBuilder& builder, Game& game, ...)`
  (drop the trailing `FrameDebuggerCaptureContext& capture` parameter
  entirely).
- The one line at the very end of the old body,
  `capture.SetReplayStepPreviews(std::move(destinations));`, becomes
  `SetReplayStepPreviews(std::move(destinations));` (implicit `this->`).
- Update this file's own top-of-file header comment the same way as above.

### Step 3.4 — Convert every pass-through call site

Work through items 6-16 of Step 2's file list above, in that order (each one
only touches types/signatures, never logic, except items 10 and 16's real
call-site conversions — remember item 10 has TWO separate local variables to
convert, not one, see its own text above). After each file, do a quick
sanity read to confirm you haven't left a stray `FrameDebuggerCaptureContext*`/
`class FrameDebuggerCaptureContext;` anywhere in a `gte_core`-tier file —
run `search_in_dir(src, "FrameDebuggerCaptureContext")` again at the end of
this whole phase and confirm every remaining match is inside a
`src/Editor/`-tree file (or a test file that legitimately constructs the
concrete type directly).

### Step 3.5 — CMakeLists.txt

No `target_sources()` changes are needed for this phase — every file touched
already exists in its own target's source list (`src/Core/`-tree files are
already implicitly `gte_core`-owned by directory convention, but this NEW
header, `src/Core/FrameDebuggerCaptureRecorder.h`, must be explicitly ADDED
to `gte_core`'s `add_library(gte_core STATIC ...)` source list in the root
`CMakeLists.txt` — CMake does not auto-discover headers, every header must
be explicitly listed for IDE/dependency-tracking purposes, matching every
other `src/Core/*.h` entry already there, e.g. right after
`src/Core/EditorCapabilities.h`, confirmed today at that block's own
`src/Core/EditorCapabilities.h` line — re-confirm the exact line with
`search_in_dir` since it will have drifted by the time you implement this).

## Step 4: Compile check

1. `cmake --build build --target gte_editor` first (it depends on
   `gte_core` — building it will also rebuild `gte_core` if needed). Expect
   this to be the FIRST real compile check to catch any signature mismatch
   between the interface and its implementation. Fix any error before
   proceeding.
2. Do NOT yet expect `cmake --build build --target GreatTamanaEditor` to
   prove anything new about the link-hazard fix itself — the
   `$<LINK_GROUP:RESCAN,...>` workaround is STILL in the CMakeLists.txt
   through this phase (PHASE4 removes it), so the final executable link
   will keep succeeding via the rescan mechanism regardless of whether this
   phase's fix is correct or not. Still run it anyway as a basic sanity
   check that nothing else broke, but do not treat its success as proof of
   this phase's own correctness — that proof is PHASE4's job.
3. `run_app_background` the built executable, `gte_send_request` a
   `GET /get_swapchain` (confirm the Editor still renders), then arm the
   Frame Debugger end-to-end exactly like `editor-core-separation-1`'s own
   Phase 19 smoke test did:
   `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` →
   `GET /frame_debugger/capture` → confirm `hasCapturedFrame:true` and a
   real, non-zero `totalEventCount` in the response → `GET /get_swapchain`
   again and visually confirm the Frame Debugger tree still shows real
   "RenderOpaque"/draw entries (use `load_image` on a screenshot if
   `gte_send_request` returns an image, or inspect the JSON state via
   `GET /frame_debugger/state`). This is the ONE runtime behavior this whole
   phase touches (the Frame Debugger's per-draw recording AND its replay-step
   preview generation) — a regression here would be silent at compile time
   but very visible here. This smoke test genuinely exercises BOTH real
   flows described in Step 1 (Defect A's `Core.cpp` inline-provider ->
   `Game.cpp` -> `RenderSystem.cpp` chain, and Defect B's direct
   `Core::BuildFrame()` call site) — it does NOT, and cannot, exercise
   `RenderPasses.cpp`'s `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`,
   since those two remain confirmed dead code (see Step 1); their own
   correctness is proven only by Step 4.1's compile check, never by this
   runtime test. `stop_app_background` when done.

## Step 5: Wrap-up

- Write `PHASE2_COMPLETION_REPORT.md`: the exact final file list touched,
  the interface's final real shape (paste it), the compile-check result,
  and the full Frame Debugger smoke-test result (paste the JSON responses).
  If anything in Step 2's predicted file list turned out to be wrong (a file
  that needed no change, or an extra file this plan missed), say so
  explicitly and honestly — do not silently correct it without a note.
- `git_add` + `git_commit`.
- If you hit a genuine ambiguity (e.g. a signature this plan didn't
  anticipate, or a file this plan's inventory missed), call `ask_questions`
  before guessing.
