# PHASE3 — Per-Draw-Call Entity Attribution Capture Infrastructure

_Read `PHASE0_MASTER_STRATEGY.md` in full first (Workstream B). This phase is
independent of PHASE1/PHASE2's `ViewScope` work — it touches almost entirely
different files (`RenderSystem`/`FrameDebuggerCapture`, not
`RenderGraph`/`Application`). It is Workstream B's FOUNDATION — it adds new
CAPTURE data but changes NO Frame-Debugger tree/display logic yet (that is
PHASE4). After this phase, the Frame Debugger's visible tree/UI must be
UNCHANGED from PHASE2's end state — only new, not-yet-consumed data exists._

## Step 1: The Goal (Where are we going?)

Give `FrameDebuggerCaptureContext` a real, ordered list of "what did each
individual real draw call this frame actually draw" records — which ECS
`Entity` issued it (index/generation), its resolved display name (the same
kind of name the Hierarchy panel already shows, e.g. `"terrain"`), which real
Pipeline/MaterialTexture it used, and its own real triangle count (not the
whole pass's aggregate) — with the exact same "zero overhead when disarmed"
guarantee every other piece of Frame Debugger capture data already has.

## Step 2: The Situation (Where are we now?)

- `src/Game/RenderSystem.h`'s `DrawCommand` struct is:
  ```cpp
  struct DrawCommand {
      MeshHandle mesh;
      PipelineHandle pipeline;
      TextureHandle texture;
      Mat4 model = Mat4::Identity();
  };
  ```
  It has NO `Entity` field — `RenderSystem::CollectRenderables()` reads
  `renderers.EntityAt(i)` into a local `entity` variable purely to compute
  `ComputeWorldMatrix(registry, entity)`, then discards it.
- `RenderSystem::Draw()` iterates `CollectRenderables(registry)` and, only
  when `capture != nullptr`, calls
  `capture->RecordDraw(pipeline->DebugName(), materialTextureDebugName,
  viewProjection);` — this is the ONLY existing call site of `RecordDraw()`.
- `FrameDebuggerCaptureContext` (`src/Editor/FrameDebuggerCapture.h/.cpp`)
  holds deduplicated name lists plus a bare counter — no per-draw record list
  at all today.
- `src/ECS/Entity.h`'s `Entity` struct: `std::uint32_t index`,
  `std::uint32_t generation`, `bool IsValid() const`.
- `src/ECS/Components/Name.h`'s `Name` struct: `std::string value` — OPTIONAL
  per entity; `Editor/Panels/HierarchyPanel.cpp::BuildEntityLabel()` already
  has the canonical "fall back to a synthesized label when no Name component
  exists" logic — locate this function and reuse its EXACT fallback string
  format (do not invent a second, slightly-different fallback format) if it is
  reasonably extractable/callable from `RenderSystem.cpp`; if `HierarchyPanel.cpp`
  is Editor-only and `RenderSystem.cpp` is a CORE always-compiled file that
  cannot depend on it (very likely, per `AGENTS.md`'s Clean Architecture rule
  — `Editor` depends on `Game`, never the reverse), instead duplicate the
  SAME fallback string shape as a small, local, `#if GTE_ENABLE_EDITOR`-guarded
  helper — read `BuildEntityLabel()` first specifically to copy its exact
  output format (e.g. `"Entity %u"` vs `"Entity <index>"` — get the real,
  existing convention right, do not guess).
- `src/Renderer/Mesh.h`'s `Mesh` class: `bool HasIndexBuffer() const`,
  `std::uint32_t IndexCount() const`, `std::uint32_t VertexCount() const`.
  Triangle count for one draw = `mesh->HasIndexBuffer() ?
  (mesh->IndexCount() / 3) : (mesh->VertexCount() / 3)` — this exact formula
  already exists (see `src/Renderer/DrawStats.h::AccumulateDrawStats()`) —
  do not reinvent a different divide-by-3 rule; reuse or mirror that exact
  logic (calling `AccumulateDrawStats()` itself against a throwaway
  `DrawStats` and reading back `.triangleCount` is one clean way to guarantee
  these two counts can never drift apart; a direct inline formula copy is
  also acceptable if simpler at the call site).

## Step 3: The Plan (exact changes)

### 3.1 `RenderSystem.h` — carry the `Entity` through `DrawCommand`

```cpp
struct DrawCommand {
    Entity entity; // frame-debugger-6 campaign, PHASE3 - the ECS entity this
                    // draw call came from, so a capture consumer (see
                    // FrameDebuggerCaptureContext::RecordEntityDraw()) can
                    // attribute this exact draw back to a real, selectable
                    // entity (its own Name, if any) rather than only an
                    // anonymous mesh/pipeline/texture triple.
    MeshHandle mesh;
    PipelineHandle pipeline;
    TextureHandle texture;
    Mat4 model = Mat4::Identity();
};
```

`RenderSystem.h` already `#include`s `ECS/Registry.h` (confirm this transitively
brings in `Entity` — if not, add `#include "ECS/Entity.h"` explicitly).

### 3.2 `RenderSystem.cpp::CollectRenderables()` — populate it

```cpp
commands.push_back(DrawCommand{ entity, meshRenderer.mesh, meshRenderer.pipeline, meshRenderer.texture, model });
```

(Simply add `entity` as the first aggregate-init argument, matching the
struct's new field order — `entity` is already a local variable in this loop,
see Step 2.)

### 3.3 `FrameDebuggerCapture.h` — the new record type + accumulator

Add a new struct (near the top, alongside `FrameDebuggerStandardPipelineState`):

```cpp
// frame-debugger-6 campaign, PHASE3 - one real, individual draw call's own
// attribution facts, in the exact order RecordEntityDraw() was called this
// frame (never deduplicated - unlike PipelineDebugNames()/
// MaterialTextureDebugNames() above, a repeated entity/mesh combination is
// still one entry per real draw, since PHASE4 needs one real, selectable
// tree leaf per real draw, not per distinct name).
struct FrameDebuggerDrawRecord {
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string displayName;           // e.g. "terrain", or the synthesized
                                        // "Entity <index>" fallback - see
                                        // RenderSystem::Draw()'s own resolution
                                        // logic.
    std::string pipelineDebugName;
    std::string materialTextureDebugName; // empty for an untextured draw.
    std::uint32_t triangleCount = 0;
};
```

Add to `FrameDebuggerCaptureContext`:

```cpp
// frame-debugger-6 campaign, PHASE3 - records one real, individual draw
// call's own attribution facts, IN ADDITION to (never instead of) the
// existing deduplicated PipelineDebugNames()/MaterialTextureDebugNames()/
// DrawCallCount() bookkeeping RecordDraw() already performs - call this
// alongside RecordDraw() (or fold RecordDraw()'s own body into this method
// and have RecordDraw() call it internally, whichever keeps RenderSystem::
// Draw()'s own call site simplest - see that method's own doc comment
// below for the recommended shape). `triangleCount` should be computed the
// exact same way DrawStats.h::AccumulateDrawStats() already computes a
// draw's contribution (HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3)
// - never a separately-invented formula.
void RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& displayName,
    const std::string& pipelineDebugName, const std::string& materialTextureDebugName, std::uint32_t triangleCount);

// Every real per-draw attribution record captured since the last Reset(), in
// real draw order (index 0 == first draw issued this frame's Game-View pass).
const std::vector<FrameDebuggerDrawRecord>& DrawRecords() const noexcept { return m_drawRecords; }
```

Decide (and document your choice in the PHASE3 completion note) between:

- **(a) Two separate calls at the `RenderSystem::Draw()` call site** — keep
  calling the existing `RecordDraw(pipelineDebugName, materialTextureDebugName,
  viewProjection)` exactly as today (for the existing dedup lists / draw-call
  counter / last-view-projection bookkeeping), PLUS a new, separate call to
  `RecordEntityDraw(...)` for the new per-draw list. Simpler, more surgical,
  zero risk of subtly changing `RecordDraw()`'s existing, already-tested
  behavior.
- **(b) Fold everything into one call** — extend `RecordDraw()`'s own
  signature to also take the new entity/name/triangle-count arguments and
  have it internally also push a `FrameDebuggerDrawRecord`. Less call-site
  code, but touches an already-existing, already-tested public method's
  signature, forcing every existing test/call site of `RecordDraw()` to be
  updated.

**Recommendation: choose (a).** It keeps this phase's diff small and
strictly additive, matching Locked Design Decision #3's spirit from PHASE0
(new, additive surface area rather than modifying an existing one). Only
switch to (b) if, after actually trying (a), the two calls turn out to be
awkwardly redundant in a way that makes (a) clearly worse — use your own
engineering judgement, but default to (a).

`Reset()` must also clear `m_drawRecords` (`m_drawRecords.clear();`).

### 3.4 `FrameDebuggerCapture.cpp` — implement `RecordEntityDraw()`

```cpp
void FrameDebuggerCaptureContext::RecordEntityDraw(std::uint32_t entityIndex, std::uint32_t entityGeneration,
    const std::string& displayName, const std::string& pipelineDebugName,
    const std::string& materialTextureDebugName, std::uint32_t triangleCount)
{
    FrameDebuggerDrawRecord record;
    record.entityIndex = entityIndex;
    record.entityGeneration = entityGeneration;
    record.displayName = displayName;
    record.pipelineDebugName = pipelineDebugName;
    record.materialTextureDebugName = materialTextureDebugName;
    record.triangleCount = triangleCount;
    m_drawRecords.push_back(std::move(record));
}
```

### 3.5 `RenderSystem.cpp::Draw()` — the new call site

Inside the existing `#if GTE_ENABLE_EDITOR` / `if (capture != nullptr)` block
(right next to the existing `capture->RecordDraw(...)` call), add:

```cpp
const std::uint32_t triangleCount = mesh->HasIndexBuffer()
    ? (mesh->IndexCount() / 3)
    : (mesh->VertexCount() / 3);

std::string displayName;
if (const Name* name = registry.TryGetComponent<Name>(command.entity); name != nullptr && !name->value.empty()) {
    displayName = name->value;
} else {
    displayName = /* the exact same fallback format HierarchyPanel::BuildEntityLabel() uses, e.g. */
        "Entity " + std::to_string(command.entity.index);
}

capture->RecordEntityDraw(command.entity.index, command.entity.generation, displayName, pipeline->DebugName(),
    materialTextureDebugName, triangleCount);
```

Add `#include "ECS/Components/Name.h"` to `RenderSystem.cpp` (a plain,
always-compiled, core ECS component — safe, no layering violation).
`registry` is already a parameter of `Draw()`. Re-verify the EXACT fallback
label format against `HierarchyPanel::BuildEntityLabel()`'s real source before
finalizing this string — do not ship a fallback that visually disagrees with
what the Hierarchy panel already shows for the same entity.

## Step 4: Tests

- `tests/Game/RenderSystemTests.cpp` — `CollectRenderables()` is directly
  Tier-1-tested (per that file's own existing tests and `RenderSystem.h`'s own
  doc comment). Add/update a case asserting `DrawCommand::entity` is now
  populated correctly (matches `ComponentStorage<MeshRenderer>::EntityAt(i)`).
- `tests/Editor/FrameDebuggerCaptureTests.cpp` — add cases for
  `RecordEntityDraw()`/`DrawRecords()`/`Reset()` clearing the new list,
  mirroring the existing `RecordDraw()` test shape in that same file.

## Step 5: Definition of Done for PHASE3

- [ ] `DrawCommand::entity` added and populated.
- [ ] `FrameDebuggerDrawRecord` + `FrameDebuggerCaptureContext::
      RecordEntityDraw()`/`DrawRecords()` added; `Reset()` clears the new list.
- [ ] `RenderSystem::Draw()` resolves a real display name (Name component or a
      verified-matching fallback) and a real per-draw triangle count, and
      calls `RecordEntityDraw()`.
- [ ] `#if GTE_ENABLE_EDITOR` boundaries preserved exactly (a
      `GTE_ENABLE_EDITOR=OFF` build must still compile+link).
- [ ] New/updated Tier-1 tests pass.
- [ ] Incremental build succeeds.
- [ ] Manual sanity check: this phase must NOT change anything visible in the
      Frame Debugger UI yet (the new data is captured but not yet consumed by
      any tree-building code — that's PHASE4). Confirm via the same
      load-scene-and-capture HTTP recipe used in PHASE1/PHASE2's own manual
      checks that the tree still looks exactly like PHASE2's end state.
- [ ] Commit via `git_add`/`git_commit`.
