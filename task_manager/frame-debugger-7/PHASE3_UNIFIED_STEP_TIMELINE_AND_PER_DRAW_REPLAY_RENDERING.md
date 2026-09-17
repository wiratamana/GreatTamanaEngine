# PHASE3 — Per-object accumulated replay rendering (heaviest phase)

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. Builds on Phase 1
(`FrameDebuggerCurrentCapture`) and Phase 2 (deferred capture trigger)._

**This is the highest-risk, heaviest-context phase in the whole campaign.**
Per the master strategy's own orchestration notes, get this phase's
completed work DOUBLE-CHECKED on its own (a dedicated `delegate_task`, only
for this one file's implementation) before the wider, all-files
double-check happens. Whoever double-checks this phase must also be told
(via the delegating prompt) to use `ask_questions` for anything genuinely
ambiguous, and to tell any further delegation to do the same.

> **Review note (this revision):** this document was deeply double-checked
> against the REAL current source of `RenderGraphBuilder.h`/`RenderGraph.h`/
> `RenderTexture.h`/`RenderTarget.h`/`RenderPasses.cpp`/`RenderSystem.h/.cpp`/
> `Game.h/.cpp`/`FrameDebuggerCapture.h/.cpp`/`EditorLayer.h`/
> `RenderGraphNameSlotTable.h`/`RenderGraphTypes.h` before this revision was
> written. Every concrete technical claim below (PassContext truly has no
> `VkImage` accessor; the real `"GameView"` pass truly draws the sky ONCE,
> after every object; `RenderTexture`'s color+depth pairing) was independently
> re-confirmed against those files, not just carried over from the original
> draft. One genuinely serious bug was found and fixed in this revision: the
> original draft's own placeholder for the per-index replay pass NAME
> (`/* per-index static name */`, with a comment pointing at a "snprintf into
> a stack buffer" idiom used elsewhere in this codebase) would have compiled
> and even *appeared* to work on the very first capture, but produces a real,
> confirmed dangling-pointer / use-after-free the moment a SECOND capture ever
> happens (see Step 3.3's new "per-index pass name storage" subsection below
> for the full, concrete fix, now spelled out instead of left as an exercise).
> The depth-target question ("does a RenderTexture already carry a paired
> depth image?") is also now answered definitively (yes — see Step 3.3). The
> two-bool pending/serviced handshake (Step 3.0) is now fully specified rather
> than left for the implementer to "work out". Everything else in this
> document was found to be accurate and is preserved as-is.

## Step 1: The Goal

For every real step in one captured frame — every per-object draw inside
`"GameView"`, AND every compute pass before/after it — produce a REAL GPU
image showing exactly what the Game View looked like right after that one
step finished, so the Frame Debugger's preview box can show it (wiring that
image into the UI is Phase 4's job; this phase only has to PRODUCE the
images correctly and safely).

## Step 2: The Situation

Re-read `PHASE0_MASTER_STRATEGY.md`'s Step 2 "The technical opportunity"
section — the short version: the real `"GameView"` pass
(`Application/RenderPasses.cpp`'s `AddGameViewPass()`) draws every object
into ONE shared target with no chance today to grab an intermediate result,
and nothing may ever modify that real pass (Locked Design Decision #4).

### The chosen approach: self-contained "redraw from scratch, i objects at a
time" replay passes — NOT a shared-target-plus-mid-pass-copy scheme

Investigation during strategy design confirmed
`RenderGraphBuilder::PassBuilder::WriteColorAttachment()`/
`WriteDepthStencilAttachment()` (`src/Renderer/RenderGraph/RenderGraphBuilder.h`)
support a `std::nullopt` clear value ("LOAD, keep existing contents"), which
would have allowed a shared, incrementally-accumulated scratch target — BUT
`PassContext` (`src/Renderer/RenderGraph/RenderGraph.h`) only ever hands an
`execute` lambda a resolved `VkImageView`/`VkSampler` pair
(`resolveTexture()`/`resolveReadTexture()`), a resolved `VkBuffer`
(`resolveBuffer()`), or a resolved volume-texture `VkImageView`
(`resolveVolumeTexture()`) — **re-confirmed line-by-line against the real
`PassContext` struct in this revision: it genuinely has no member, method, or
any other way to obtain a raw `VkImage` at all.** So doing a raw
`vkCmdCopyImage` FROM that shared scratch target INTO N separate retained
textures, from inside a pass's own `execute` lambda, would need either a new
`PassContext` capability (touching the shared Render Graph type used by
every other pass in the engine — higher risk) or an awkward second, external
`ImmediateSubmit()` pass done in between graph passes (fragile ordering).
This part of the original design investigation holds up completely — no
correction needed here.

**Instead, use the ALREADY-PROVEN, ALREADY-SHIPPED "own a `RenderTexture`
outside the graph, `ImportTexture()` it in" pattern** every other per-view
render target in this engine already uses (see `Application.cpp`'s
`b.ImportTexture("GameView", gameTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);`,
and `RenderTexture::Target()`, `src/Renderer/RenderTexture.h`): create N
**completely independent** destination `RenderTexture`s up front (one per
real object draw this frame), import each one as its OWN distinct
`TextureHandle`, and add N **completely independent** Render Graph passes,
where pass `i`:

1. Clears its own destination texture (`WriteColorAttachment(dest_i,
   kGameClearColor)`/`WriteDepthStencilAttachment(dest_i, kGameClearDepth)`
   — always CLEAR, never LOAD, since each pass is fully self-contained).
2. Declares the SAME `DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers)`
   the real `"GameView"` pass already declares (a GPU-skinned mesh's vertex
   buffer must still be correctly synchronized against the earlier
   GPU-skinning compute passes this same frame).
3. In its `execute` lambda: redraws objects `[0 .. i]` (inclusive, i.e. `i+1`
   objects) FROM SCRATCH into its own destination texture, using the exact
   same camera/view-projection resolution the real pass uses.
4. Pass `i == N-1` (the LAST object) additionally invokes the SAME
   `recordSkyBackground` callback the real `"GameView"` pass invokes
   AFTER its own object draws (see 3.3 below for why this one detail
   matters for correctness).

This is deliberately **O(N²) total draw calls** across all N replay passes
(pass 0 draws 1 object, pass 1 draws 2, ..., pass N-1 draws N) instead of an
O(N) shared-target scheme. This trade is **intentional and LOCKED** for this
phase: it needs ZERO new Render Graph/`PassContext`/`Renderer` API surface,
it can never corrupt the real `"GameView"` pass, and it only ever runs once
per explicit capture trigger on an Editor/debug path (Locked Design Decision
#7, `PHASE0`) — for the scene sizes this tool is meant for (an artist/
programmer inspecting a specific frame, not a shipping build), this is a
perfectly acceptable cost. If a future engineer ever finds this too slow for
a very large scene, that is a well-isolated, separate future optimization
(swap this phase's passes for the shared-target + widened-`PassContext`
scheme) — not something to solve now. (Also re-confirmed this revision: `N`
destination `RenderTexture`s each carry their OWN full-resolution color+depth
allocation — see Step 3.3's depth note — so total VRAM cost is genuinely
`O(N)` extra render targets at Game View resolution. This is an accepted,
LOCKED cost per Design Decision #2 ("no artificial per-object safety cap"),
not a new gap; just be aware the number is real GPU memory, not just draw
calls.)

### 3.0 — Widen the deferred-trigger mechanism to ALL THREE capture triggers

Phase 2 fixed the Enable checkbox's own false→true edge (the literally
reported bug) by deferring its `TriggerCapture()` call to the next properly-
armed frame. **This phase's new replay passes have the exact same timing
constraint for EVERY trigger, not just Enable**: the decision "please add N
replay passes this frame" must be known BEFORE `Game::Render()` runs (i.e.
before `Application::Run()`'s `build` lambda executes), but a Step/Capture-
button click is only detected LATE in the frame, inside `Build()` — same
problem Bug 1 had, one mechanism deeper.

**Generalize** Phase 2's `m_pendingCaptureAfterEnable` bool into ONE unified
`bool m_pendingCaptureTrigger` (rename it) that is set to `true` by ALL
THREE call sites: the Enable false→true edge, the Step-consumption branch,
and the "Capture" button click (`BuildToolbarRow()`). None of the three
call `TriggerCapture()` synchronously anymore.

**The exact two-bool handshake (fully specified — do not re-derive this):**

```cpp
// Panels/FrameDebuggerPanel.h
bool m_pendingCaptureTrigger = false;   // renamed from m_pendingCaptureAfterEnable (Phase 2)
bool m_replayServicedThisFrame = false; // NEW this phase - see below.
```

- **Frame N, LATE** (inside `Build()`/`BuildToolbarRow()`, whichever of the
  three trigger sites fires): set `m_pendingCaptureTrigger = true;` only.
  Nothing else changes yet, and no capture happens this frame (exactly like
  Phase 2, just widened to 3 call sites instead of 1).
- **Frame N+1, EARLY** (`Application::Run()`'s `build` lambda, BEFORE
  `Game::Render()`'s Game-View branch runs — the exact same point
  `PrepareFrameDebuggerCaptureContext()` is already called from): call the
  new `IEditorLayer::ConsumePendingFrameDebuggerReplayRequest()`, which
  forwards straight into a new
  `bool FrameDebuggerPanel::ConsumePendingReplayRequest()`:
  ```cpp
  bool FrameDebuggerPanel::ConsumePendingReplayRequest()
  {
      if (!m_pendingCaptureTrigger) {
          return false;
      }
      m_pendingCaptureTrigger = false;    // clear the ORIGINAL flag, exactly
                                          // once, right here - never cleared
                                          // anywhere else.
      m_replayServicedThisFrame = true;   // leave a same-frame "receipt" so
                                          // Build(), running LATER this SAME
                                          // frame, knows the replay passes
                                          // were genuinely declared+executed
                                          // this frame and it is now safe
                                          // (and correct) to call
                                          // TriggerCapture().
      return true;
  }
  ```
  `Application::Run()` uses the `true`/`false` return value to decide whether
  to call `AddFrameDebuggerReplayPasses()` this frame at all (see 3.5).
- **Frame N+1, LATE** (top of `Build()`, same frame N+1 — AFTER this frame's
  `Game::Render()`/replay passes have already executed, since `Build()` is
  called from `ImGuiEditorLayer::BuildUI()` which always runs after
  `RenderGraph::Execute()` for the offscreen regime has returned):
  ```cpp
  if (m_replayServicedThisFrame) {
      m_replayServicedThisFrame = false;
      TriggerCapture();
  }
  ```

Why two separate bools and not one: the SAME underlying "a trigger happened"
fact needs to be read at two DIFFERENT points **within the same eventual
frame** — once EARLY (to decide whether to declare N extra Render Graph
passes, before any rendering happens) and once LATE (to decide whether to
call `TriggerCapture()`, after rendering already happened) — and the EARLY
read must CONSUME the original request (so a stray extra frame never
declares a second, redundant batch of replay passes) while still leaving
behind an unambiguous, same-frame-only signal for the LATE read to act on.
A single bool cleared by the early read would leave the late read with
nothing to check; a single bool cleared by the late read would leave the
early read unable to tell "should I declare passes this frame" from "already
declared, don't declare again".

**Known, accepted edge case (inherited from Phase 2, not new):** if the
Frame Debugger window is closed in the same frame the trigger fired (before
frame N+1 begins), `PrepareFrameDebuggerCaptureContext()` returns `nullptr`
on frame N+1 (see `IEditorLayer`'s own contract: nullptr whenever
`ctx.frameDebuggerWindowOpen` is false), so the guarded call
`frameDebuggerCapture != nullptr && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()`
(see 3.5) short-circuits and never actually consumes `m_pendingCaptureTrigger`
that frame. Both flags simply stay pending, harmlessly, until a future frame
where the window is open and armed again — at which point BOTH the replay
passes and the deferred `TriggerCapture()` fire together, atomically, on
that eventual frame. This mirrors Phase 2's own already-accepted "the flag
just waits for `Build()` to run again" behavior for the exact same scenario
(a `Build()` call that never happens while the window is closed) — it is
not a NEW problem this phase introduces, just the same accepted quirk one
mechanism deeper. No fix needed; just don't be surprised by it during
Phase 7's live verification.

Add a new `IEditorLayer` virtual (`src/Editor/EditorLayer.h`,
implemented by `ImGuiEditorLayer`/`NullEditorLayer`, mirroring
`PrepareFrameDebuggerCaptureContext()`'s own existing shape):

```cpp
// Read-and-clear. True for exactly the one frame following a real capture
// trigger (Enable-edge / Step / Capture button) - see FrameDebuggerPanel::
// ConsumePendingReplayRequest()'s own doc comment.
virtual bool ConsumePendingFrameDebuggerReplayRequest() = 0;
```

`NullEditorLayer` returns `false` unconditionally (no Editor at all),
matching every other method in that class (see `NullEditorLayer.cpp`'s own
existing "every method is a no-op" pattern).

### 3.1 — Let `Application::Run()` know how many objects exist, before any
pass is declared

Add a small new method to `Game` (`src/Game/Game.h/.cpp`), mirroring the
EXISTING precedent `Game::CollectGpuSkinningDispatchRequests()` already
establishes for "let `Application::Run()`'s `build` lambda peek at
something before passes are declared":

```cpp
// Mirrors CollectGpuSkinningDispatchRequests()'s own "safe to call at
// build-time, before any pass executes" precedent - RenderSystem::
// CollectRenderables() only reads already-frozen ECS component data (Update()
// already ran this frame and will not run again until the next real frame),
// so this is safe to call synchronously while declaring this frame's Render
// Graph passes, with no live VkCommandBuffer/Renderer needed at all.
std::size_t Game::CountGameViewDrawCommandsThisFrame()
{
    return RenderSystem::CollectRenderables(m_registry).size();
}
```

(`RenderSystem::CollectRenderables()` is a `static` method — call it as
`RenderSystem::CollectRenderables(m_registry)`, not through an instance;
either compiles, but the static form is clearer about what's actually
happening. Confirmed accessible/callable exactly this way from `Game` — it
is already called from within `RenderSystem::Draw()` itself, in
`src/Game/RenderSystem.cpp` — reuse it directly, do not reimplement it.)

**IMPORTANT, previously-undocumented assumption — write this down explicitly
in the completion report, do not silently rely on it:** `objectCount` here
is `CollectRenderables(m_registry).size()` — the number of entities that
HAVE a `MeshRenderer` component. This is **not automatically identical** to
the number of entities that actually end up issuing a real
`renderer.Submit()` call inside `RenderSystem::Draw()` — that function's own
loop silently skips a `DrawCommand` whose `mesh`/`pipeline` handle no longer
resolves (see `RenderSystem.cpp`'s own comment: "silently skipped rather
than asserting - draws are inherently best-effort against whatever is
currently loaded"), and only a SUCCESSFULLY-resolved draw calls
`capture->RecordEntityDraw()` (i.e. only successful draws end up in
`capture.DrawRecords()`, which is what Phase 4's per-entity tree leaves and
this phase's own Definition of Done are both keyed on). In every scenario
this engine actually exercises today, every `MeshRenderer`'s handles resolve
successfully (mesh/pipeline pools are never partially unregistered mid-
session), so `objectCount == capture.DrawRecords().size()` holds in
practice — but if that ever stops being true (e.g. a future feature
unregisters a mesh while entities still reference it), the `i+1`-th replay
pass's `maxDrawCount` cutoff (Step 3.2) would silently stop lining up 1:1
with the `i`-th entry of `capture.DrawRecords()`, from the first skipped
entity onward. This phase does not need to fix that (it is not reachable
with today's engine), but MUST record this assumption explicitly in
`PHASE3_COMPLETION_REPORT.md` so a future maintainer who ever adds mesh/
pipeline unregistration knows exactly where to look.

### 3.2 — Add an optional draw-count cutoff to `RenderSystem::Draw()`/`Game::Render()`

`src/Game/RenderSystem.h/.cpp`: add a new, LAST, defaulted parameter to
BOTH existing `Draw()` overloads:

```cpp
void Draw(Registry&, Renderer&, float aspectWidthOverHeight,
    FrameDebuggerCaptureContext* capture = nullptr,
    std::optional<std::size_t> maxDrawCount = std::nullopt);
void Draw(Registry&, Renderer&, const Mat4& viewProjection,
    FrameDebuggerCaptureContext* capture = nullptr,
    std::optional<std::size_t> maxDrawCount = std::nullopt);
```

Inside the shared draw loop (the float-aspect overload just forwards into
the `Mat4&` overload, which owns the real loop over
`CollectRenderables(registry)` — see `RenderSystem.cpp`'s current body),
stop iterating once `maxDrawCount` COMMANDS (i.e. `DrawCommand` entries
yielded by `CollectRenderables()`, the loop's own iteration count — NOT a
count of only the ones that successfully resolve a mesh+pipeline, see 3.1's
own assumption note above) have been considered, when it has a value —
`break` out of the `for` loop early, after the `maxDrawCount`-th iteration.
This is purely additive — every existing call site keeps compiling
completely unchanged, since the new parameter defaults to `std::nullopt`
everywhere it isn't explicitly passed.

`src/Game/Game.h/.cpp`: thread the SAME new optional parameter through
`Game::Render(Renderer&, float aspectWidthOverHeight, const Mat4*
viewProjectionOverride = nullptr, FrameDebuggerCaptureContext*
frameDebuggerCapture = nullptr, std::optional<std::size_t> maxDrawCount =
std::nullopt)`, forwarding it to whichever internal `m_renderSystem.Draw()`
overload it already calls (see `Game::Render()`'s current body — it picks
the `Mat4&` overload when `viewProjectionOverride != nullptr`, the
float-aspect overload otherwise; forward `maxDrawCount` unchanged into
whichever branch runs).

### 3.3 — New replay-pass builder function

Add directly inside `src/Application/RenderPasses.cpp` (NOT a new file — it
needs `kGameClearColor`/`kGameClearDepth`/`DeclareGpuSkinningReads()`, all
`namespace { }`-local to this file today; keep them local, do not export
them just for this), declared in `RenderPasses.h`:

```cpp
// frame-debugger-7 campaign, PHASE3 - adds N debug-only, self-contained
// Render Graph passes (one per real object this frame's "GameView" pass
// will draw), each redrawing objects [0..i] FROM SCRATCH into its OWN
// dedicated destination texture - see PHASE3's own doc for why this is
// deliberately O(N^2) draws rather than a shared-target + mid-pass-copy
// scheme. Only ever called when a capture trigger was just serviced (see
// IEditorLayer::ConsumePendingFrameDebuggerReplayRequest()) - a genuine
// no-op (adds zero passes) whenever `objectCount == 0`. NEVER touches the
// real "GameView" pass/target in any way. `capture` receives the resulting
// N retained RenderTexture objects via capture->SetReplayStepPreviews(...)
// (or equivalent - see FrameDebuggerCapture.h's own new field) - populated
// here (pass declaration time, where a live Renderer& already exists),
// filled with FRESH (garbage/uninitialized) content until each pass's own
// execute lambda actually runs later this same Execute() call.
void AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground,
    FrameDebuggerCaptureContext& capture);
```

#### 3.3a — RESOLVED: does a `RenderTexture` already carry a paired depth
image, or does this need a second array?

**Yes — a `RenderTexture` already carries its own companion `DepthBuffer` for
its entire lifetime (`RenderTexture.h`'s own `m_depthBuffer` member, created
in the same constructor call as the color image), and `RenderTexture::
Target()` returns a SINGLE `RenderTarget` that already bundles BOTH the color
image/view AND the depth image/view together** (`RenderTarget.h`: `image`/
`imageView`/`format` for color, `depthImage`/`depthImageView`/`depthFormat`/
`depthHasStencil` for depth, all in the one struct). Confirmed by how the
REAL `"GameView"` pass already does exactly this today
(`RenderPasses.cpp`'s `AddGameViewPass()`):

```cpp
pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
```

— both calls take the **exact same `TextureHandle`** (`gameViewTarget`,
itself produced by `builder.ImportTexture("GameView", gameTarget->Target(),
...)`), because that one imported handle's underlying `PhysicalTexture`
(`RenderGraph.h`'s private struct) already carries both `target.image` AND
`target.depthImage` from the single `RenderTarget` it was imported from.

**Therefore: do NOT invent a second array of depth `RenderTexture`s.** Each
destination `RenderTexture` you create (see below) already has its own
paired depth buffer automatically, as long as you construct it via
`renderer.CreateRenderTexture(width, height, format, debugName,
depthDebugName, ...)` (which always builds its depth side against
`Renderer::DepthFormat()` internally — see `GpuResourceFactory::
CreateRenderTexture()`, which unconditionally passes `m_depthFormat`).
`ImportTexture()` ONE `TextureHandle` per destination (from that single
`RenderTexture::Target()`), and call BOTH `WriteColorAttachment(destHandle,
...)` and `WriteDepthStencilAttachment(destHandle, ...)` against that SAME
handle — exactly mirroring `AddGameViewPass()`'s own two-calls-one-handle
pattern above.

#### 3.3b — RESOLVED: the per-index pass NAME must come from PERMANENT
(session-lifetime) storage — a stack-buffer/`snprintf` name is a confirmed,
serious bug here, not just a style nitpick

**This is the single most important correction in this revision.** The
original draft of this document left the pass `name` argument as a bare
placeholder comment (`/* per-index static name */`) and pointed at
"`FrameDebuggerHistory.cpp`'s own snprintf-into-stack-buffer pattern" as a
precedent to copy. That precedent is the WRONG one to copy here, and copying
it produces a real, confirmed dangling-pointer bug:

- `RenderGraphBuilder::AddPass()`'s own `name` parameter, and the
  `PassRecord::name` field it's stored into
  (`src/Renderer/RenderGraph/RenderGraphTypes.h`), are BOTH documented, in
  the real source, as: *"Must be a string literal / static-storage-duration
  const char* ... this is never owned/copied, just compared/displayed, so a
  temporary/stack-lifetime string would be a use-after-free risk for zero
  benefit."* `RenderGraphBuilder::AddPass()`'s own runtime `assert()` only
  checks non-null/non-empty — it CANNOT enforce storage duration at
  runtime, so a stack buffer compiles fine and even appears to work at
  first.
- Worse: `RenderGraph`'s own `RenderGraphNameSlotTable`
  (`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`) is explicitly
  documented as **"a small, PERSISTENT (across many `Execute()` calls - NOT
  rebuilt every frame) name -> slot-index table"**, used for this engine's
  real GPU-timing bookkeeping (`RenderGraph::m_lastKnownStats`/
  `m_synchronousTimingSlots`). It stores the RAW POINTER it was first given
  for a name **forever** ("once assigned, a name's slot never changes ...
  for this table's entire lifetime"), and looks up an incoming name by
  `m_names[i] == name || std::strcmp(m_names[i], name) == 0` — i.e. it WILL
  `strcmp()` against whatever pointer it stored last time, on every single
  future `Execute()` call, for as long as the process runs.
- `FrameDebuggerHistory.cpp`'s own `snprintf`-into-a-stack-buffer usage is
  safe ONLY because it feeds `RenderTexture`'s `debugName`/`depthDebugName`
  constructor parameters, which are (a) only ever read again by a future
  `Resize()` call, and (b) those particular `FrameDebuggerHistorySlotN`
  `RenderTexture`s are explicitly documented as "Always freshly (re)created,
  never `Resize()`d in place" — so the dangling pointer that pattern produces
  is never actually dereferenced again. That safety condition does NOT hold
  for a render-graph pass `name` — `RenderGraphNameSlotTable`/
  `m_lastKnownStats` WILL dereference it again, on a LATER frame, guaranteed.

**Concrete failure mode if this is implemented as a stack buffer (or as a
`std::vector<std::string>` local to `AddFrameDebuggerReplayPasses()` itself,
which is equally wrong — that function returns, and its locals are
destroyed, well before `RenderGraph::ExecuteCompiledGraph()` finishes
actually recording/naming these passes' work later the SAME `Execute()`
call):** the very next time ANY capture happens (the second, third, ...
capture this session, or even the FIRST capture combined with GPU-timing
capture already being on), `RenderGraphNameSlotTable::AssignOrGetSlot()`
(or `RenderGraph`'s own `strcmp()` fallback at `RenderGraph.cpp` lines
~628/644/658) will call `std::strcmp()` against a pointer into memory that
has already been freed (a destroyed stack frame, or a destroyed/reallocated
`std::string`/`std::vector` buffer) — undefined behavior, up to and
including a crash, and in the best case silently comparing garbage bytes.

**The concrete, correct fix (mirrors this engine's OWN existing precedent
for a genuinely dynamic, per-index/per-model pass name):** look at
`AnimationSystem::GpuSkinningDispatchRequest::name`
(`src/Game/Animation/AnimationSystem.h`) — its own doc comment says exactly
this: *"A stable, persistent (never a per-frame temporary) name ... this
MUST NOT be a freshly-built `std::string` each frame."* Its value
(`group.debugName.c_str()`) comes from a `std::string` owned by
`GpuSkinningRigCache::OutputGroup`, a long-lived object created ONCE at
model-registration time and never rebuilt per frame — that is exactly why
its pointer is safe to hand to `AddComputePass()` every single frame without
ever dangling.

Do the same thing here: maintain an **ever-growing, NEVER-shrinking,
NEVER-reallocating-in-a-way-that-moves-existing-entries** pool of formatted
name strings, local to `RenderPasses.cpp`'s own anonymous namespace (same
place `kGameClearColor`/`DeclareGpuSkinningReads()` already live), sized
lazily up to however many replay steps have ever been needed THIS SESSION —
never shrunk, so it satisfies Locked Design Decision #2 ("no artificial
per-object safety cap") exactly as well as the `destinations`/pass-count
logic itself does:

```cpp
namespace {
// ... existing kGameClearColor / kGameClearDepth / DeclareGpuSkinningReads ...

// frame-debugger-7 campaign, PHASE3 - permanent (whole-process-lifetime),
// ever-growing pool of "FrameDebuggerReplayStepN" pass names. MUST NOT be a
// per-frame/per-capture temporary - see this file's own AddFrameDebugger
// ReplayPasses() doc comment for the full reasoning (RenderGraphBuilder::
// AddPass()'s `name` parameter, and RenderGraphNameSlotTable's own
// persistent-across-Execute()-calls name table, both require a name that
// is valid for the rest of this process's lifetime, never just "this
// frame"). std::deque (never std::vector) so growing this pool NEVER moves
// an already-handed-out std::string's own character storage - a
// std::vector<std::string> growing/reallocating would invalidate every
// c_str() pointer already stored inside a PREVIOUSLY-declared PassRecord::
// name, which is exactly the same class of bug this whole section exists
// to avoid.
std::deque<std::string>& ReplayStepPassNamePool()
{
    static std::deque<std::string> pool;
    return pool;
}

// Returns a STABLE, permanent const char* naming replay step `index` -
// lazily grows the pool the first time `index` is ever requested, then
// reuses the SAME std::string (and therefore the SAME pointer) for that
// index forever afterwards, across every future capture this session.
const char* ReplayStepPassName(std::size_t index)
{
    std::deque<std::string>& pool = ReplayStepPassNamePool();
    while (pool.size() <= index) {
        pool.push_back("FrameDebuggerReplayStep" + std::to_string(pool.size()));
    }
    return pool[index].c_str();
}
} // namespace
```

Use `ReplayStepPassName(i)` as the `name` argument to `builder.AddPass()`
for replay pass `i`. This is genuinely session-permanent (a function-local
`static` lives for the whole process, exactly like a string literal would),
grows without bound (no cap imposed, matching Locked Design Decision #2),
and — as a pleasant side effect — means the SAME pointer is reused across
every future capture, so `RenderGraphNameSlotTable`'s fast pointer-equality
path (`m_names[i] == name`) actually hits from the second capture onward,
instead of always falling through to `strcmp()`.

(The destination `RenderTexture`s' own `debugName`/`depthDebugName`
constructor arguments are a COMPLETELY SEPARATE, unrelated concern — those
ARE safe to build with a per-call stack `snprintf` buffer, exactly like
`FrameDebuggerHistory.cpp` already does, PROVIDED these `RenderTexture`s are
likewise never `Resize()`d in place — state that explicitly in a comment at
the call site so a future reader doesn't misapply this section's fix to the
wrong parameter.)

Full body (fill in exact types/calls by reading the real headers as you go
where this still paraphrases anything — but the depth/name questions above
are now fully resolved, do not re-open them):

```cpp
void AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground,
    FrameDebuggerCaptureContext& capture)
{
    if (objectCount == 0) { return; }

    std::vector<RenderTexture> destinations;
    destinations.reserve(objectCount); // ESSENTIAL - see the comment below;
                                        // never let this vector reallocate
                                        // after this point.
    for (std::size_t i = 0; i < objectCount; ++i) {
        char debugNameBuffer[48];
        std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerReplayStep%zuColor", i);
        char depthDebugNameBuffer[48];
        std::snprintf(depthDebugNameBuffer, sizeof(depthDebugNameBuffer), "FrameDebuggerReplayStep%zuDepth", i);
        // Same width/height/format as the real GameView target - the caller
        // (Application.cpp) already has `gameTarget` on hand; pass its
        // Extent()/Format() straight through. Depth is created automatically
        // (Renderer::DepthFormat() internally) - see Step 3.3a above, no
        // second array needed.
        destinations.push_back(renderer.CreateRenderTexture(
            gameTargetWidth, gameTargetHeight, gameTargetFormat, debugNameBuffer, depthDebugNameBuffer));
    }

    // `destinations` must never reallocate from this point on (every
    // destHandle below imports a POINTER-STABLE Target() from it) - the
    // reserve() above already guarantees this as long as nothing else ever
    // push_back()s into `destinations` again.
    for (std::size_t i = 0; i < objectCount; ++i) {
        const char* passName = ReplayStepPassName(i); // see 3.3b above - NEVER a per-call temporary.

        const rg::TextureHandle destHandle =
            builder.ImportTexture(passName, destinations[i].Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        builder.AddPass(passName, rg::ViewScope::GameView,
            [destHandle, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            },
            [&game, &renderer, aspectWidthOverHeight, i, objectCount, recordSkyBackground](rg::PassContext& ctx) {
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // CORRECTNESS-CRITICAL, easy to get wrong by copy-pasting
                // AddGameViewPass()'s own call: `frameDebuggerCapture` here
                // is ALWAYS nullptr, NEVER the real, armed capture context.
                // FrameDebuggerCaptureContext::RecordDraw()/RecordEntityDraw()
                // are NOT idempotent/deduplicated by draw identity (only the
                // two NAME lists are deduplicated - RecordEntityDraw() always
                // appends unconditionally, see FrameDebuggerCapture.cpp). If
                // a real capture pointer were passed into these N replay
                // passes' own game.Render() calls, capture.DrawRecords()
                // would balloon to O(N^2) duplicated entries (this phase's
                // own replay draws would re-record themselves on top of the
                // real GameView pass's already-correct records), silently
                // corrupting the exact data Phase 4's per-entity tree AND
                // this phase's own Definition of Done both depend on.
                game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr,
                    /*maxDrawCount=*/ i + 1);
                renderer.EndGraphPassRecording();
                // Only the VERY LAST replay pass also draws the sky
                // background, exactly mirroring AddGameViewPass()'s own real
                // ordering (sky is drawn AFTER every object, once, not
                // per-object) - re-confirmed against RenderPasses.cpp's real
                // AddGameViewPass() body this revision: recordSkyBackground
                // is invoked strictly AFTER renderer.EndGraphPassRecording()
                // there too. This makes replay step N-1's own image
                // pixel-identical to the existing entry.preview snapshot,
                // a good internal cross-check to assert in this phase's own
                // manual verification.
                if (i + 1 == objectCount && recordSkyBackground) {
                    recordSkyBackground(ctx.cmd);
                }
            });
    }

    capture.SetReplayStepPreviews(std::move(destinations));
}
```

(`gameTargetWidth`/`gameTargetHeight`/`gameTargetFormat` above are
placeholders for whatever the real call site already has on hand — see 3.5,
which shows `gameTarget->Extent()` is already resolved into `extent` right
there; thread `gameTarget->Format()` through too, or pass `gameTarget`
itself into this function instead of separate width/height/format — either
is fine, implementer's choice, just don't hardcode a format literal, per
`AGENTS.md`'s "Render Target Format Matching".)

### 3.4 — Storage on `FrameDebuggerCaptureContext`

`src/Editor/FrameDebuggerCapture.h/.cpp`: add
`std::vector<RenderTexture> m_replayStepPreviews;` (mirrors
`FrameDebuggerComputePassPreview`'s own move-only-vector precedent) plus
`SetReplayStepPreviews(std::vector<RenderTexture>&&)` and
`ReplayStepPreviews()` accessors. `Reset()` must `.clear()` this vector too
(RAII-destroys last frame's leftover GPU textures — `AGENTS.md`).

This is the right home for it: `FrameDebuggerCaptureContext` is already the
object `TriggerCapture()` reads from LATER the SAME frame (see 3.0's
handshake) to build the long-lived `FrameDebuggerCurrentCapture`/
`FrameDebuggerHistoryEntry` (Phase 1/Phase 4's job to actually move/copy
this vector out into that longer-lived storage) — confirmed this revision
that `FrameDebuggerCaptureContext` is `Reset()` at the top of every ARMED
frame (per `EditorLayer.h`'s own `PrepareFrameDebuggerCaptureContext()` doc
comment: "returns a real, non-null pointer (already Reset() for this fresh
frame)"), so `m_replayStepPreviews` here is correctly emptied on every
subsequent armed frame that ISN'T itself a capture-trigger frame — exactly
the right "scratch, per-frame, only meaningfully populated on a trigger
frame" lifetime for this phase's own scope. Phase 4 is responsible for
actually reading/moving `capture.ReplayStepPreviews()` into permanent
storage before this same frame ends (i.e. inside `TriggerCapture()`, which
runs later this SAME frame per the 3.0 handshake, strictly before the NEXT
frame's `Reset()` call) — note this explicit ordering requirement in this
phase's own completion report so Phase 4's author doesn't have to
rediscover it.

`FrameDebuggerHistoryEntry::computePassPreviews`/
`FrameDebuggerComputePassPreview` stay untouched for now (Phase 4 removes
them) — do not touch that logic in this phase.

### 3.5 — Wire the call site into `Application::Run()`

Inside the offscreen `build` lambda (`Application.cpp`), the real call site
is nested inside `if (gameTarget != nullptr) { ... }` (see that file — the
`aspect`/`recordGameSkyBackground`/`gpuSkinningBuffers`/`h` local variables
below are the REAL names used there today, not paraphrased placeholders).
Add the new call right after the existing
`AddGameViewPass(b, m_game, m_renderer, h, aspect, gpuSkinningBuffers,
recordGameSkyBackground, frameDebuggerCapture);` / `outputs.push_back(h);`
lines, still inside that same `if (gameTarget != nullptr)` block (it needs
`aspect`/`recordGameSkyBackground`/`gpuSkinningBuffers`/`gameTarget`, all of
which only exist inside that block):

```cpp
if (frameDebuggerCapture != nullptr && m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()) {
    const std::size_t objectCount = m_game.CountGameViewDrawCommandsThisFrame();
    AddFrameDebuggerReplayPasses(b, m_game, m_renderer, aspect, objectCount,
        gpuSkinningBuffers, recordGameSkyBackground, *frameDebuggerCapture);
}
```

This can go either before or after the "3.3 - the Aerial Perspective
Composite pass" block that follows `AddGameViewPass()` in the real file —
the two are fully independent (no shared resource dependency), so ordering
between them doesn't matter; placing it immediately after `AddGameViewPass()`
keeps the two Game-View-pass-adjacent additions visually grouped together
for a future reader.

Read the surrounding real code carefully (confirm the exact local variable
names still match by the time this phase is actually implemented — this
document paraphrases the SURROUNDING logic, not the exact names, which were
re-confirmed accurate as of this revision) before writing this.

### 3.6 — What Phase 3 does NOT do

- Does NOT touch `ChooseFrameDebuggerPreviewSource()`, the event tree, or
  the panel UI at all — that is Phase 4. This phase only has to prove the
  N images exist, are correct, and are retrievable from
  `m_captureContext.ReplayStepPreviews()` by the time `TriggerCapture()`
  runs. **Re-confirmed this revision, by reading `FrameDebuggerData.cpp`'s
  header (`FrameDebuggerData.h`):** `BuildRealFrameDebuggerSnapshot()` only
  ever generically discovers passes flagged `isComputePass == true` (the
  "Compute Dispatches (Pre/Post-GameView)" groups) — these new replay
  passes are declared via plain `AddPass()` (`isComputePass == false` by
  construction), so they will NOT spontaneously appear anywhere in the
  Frame Debugger's own tree the moment this phase lands, even before Phase 4
  wires anything up. This phase's own claim ("does not touch the event
  tree") is therefore not just an intention but a confirmed, structural
  fact. (They WILL passively show up in the separate, unrelated "Render
  Graph" panel — `Panels/RenderGraphPanel.cpp` — which already generically
  lists every pass the graph declares each frame; this is expected,
  harmless, and not a bug.)
- Does NOT remove the old `computePassPreviews`/`CollectComputePassTextureWrites()`
  machinery yet — Phase 4 does that, once the new mechanism is proven to
  work standalone.

## Step 4: Verification for this phase (compile + a manual visual spot-check)

1. Quick compile check.
2. Because this is real rendering-pipeline surgery, do an INCREMENTAL,
   manual visual sanity check before calling this phase done (allowed by
   `AGENTS.md`'s Tier 2 guidance and this campaign's own Note 5): temporarily
   log/assert `capture.ReplayStepPreviews().size()` matches the real object
   count, and — if convenient — use `run_app_background` +
   `gte_send_request` against a temporary debug endpoint or a quick manual
   probe to eyeball at least one retained replay texture and confirm it is
   NOT garbage/black/uninitialized. This does not need to be full UI-wired
   yet (Phase 4/5 do that) — just prove the pixels are real and correctly
   ordered (step 0 shows only the first object, the last step visually
   matches the existing `entry.preview`).
3. **New this revision — a cheap, extra confidence check specifically for
   the pass-name fix (3.3b):** trigger at least TWO captures in the same
   session (e.g. click "Capture" twice) during this manual spot-check, not
   just one. A dangling/stack-lifetime pass name would very plausibly still
   "work" (or fail silently/intermittently) on the FIRST capture and only
   misbehave (crash, garbage comparison, or a GPU-timing mismatch) on the
   SECOND — a single-capture smoke test would not have caught the bug this
   revision fixed.
4. Do **not** run a full build or the full regression suite yet (Phase 7).

## Step 5: Definition of Done

- `capture.ReplayStepPreviews()` holds one real, correct, garbage-free
  `RenderTexture` per object drawn this frame, in the same order as
  `capture.DrawRecords()` (see 3.1's own documented assumption about when
  this ordering could theoretically diverge), produced only on an explicit
  capture-trigger frame, with zero effect on the real `"GameView"` pass's
  own output.
- Every replay pass's own `name` argument is backed by permanent,
  session-lifetime storage (3.3b) — verified by triggering at least two
  captures during manual spot-checking (Step 4.3), not just one.
- `PHASE3_COMPLETION_REPORT.md` written (including the two-bool pending/
  serviced handshake write-up — now already fully specified in this
  document, so the report only needs to confirm it was implemented exactly
  as written here, or explain and justify any deviation), code committed.

Use `ask_questions` for any genuine ambiguity — the depth-target handling
and the static-storage-duration per-index pass name mechanics that the
ORIGINAL draft of this document left open are now both fully resolved above;
if implementation still surfaces some other genuine ambiguity this document
doesn't cover, ask a human rather than guessing — and require the same from
anything this phase further delegates.
