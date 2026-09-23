# PHASE3 — Completion Report: `PassContext`: `std::function` → Plain Resolver Struct (item 2.7)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE3_PASSCONTEXT_PLAIN_RESOLVERS.md`. Read
`PHASE2_COMPLETION_REPORT.md` first, per that document's own instructions —
this phase edits `RenderGraph::BuildPassContext()`, exactly the method PHASE2
extracted, using its final, real signature:

```cpp
PassContext BuildPassContext(VkCommandBuffer cmd, std::vector<PhysicalTexture>& physicalTextures,
    std::vector<PhysicalBuffer>& physicalBuffers, std::vector<PhysicalVolumeTexture>& physicalVolumeTextures,
    DrawStats& passDrawStats);
```

## What was done

Implemented the plan in `PHASE3_PASSCONTEXT_PLAIN_RESOLVERS.md`, resolving
both of its explicitly-flagged open implementation questions via
`ask_questions` before writing any code, plus one additional genuine
ambiguity discovered mid-implementation (also resolved via `ask_questions`)
that the plan's own text did not anticipate.

### Open Question 1 — private nested-type visibility (LOCKED via `ask_questions`)

`PhysicalTexture`/`PhysicalBuffer`/`PhysicalVolumeTexture` remain **private**
nested types of `RenderGraph`, exactly as before. `PassContext` (a plain,
namespace-scope struct) is granted access via a single, narrow
`friend struct PassContext;` declaration inside `RenderGraph`'s private
section — **not** by moving the three structs out to full `gte::rg`
namespace scope. This was an explicit choice between the two options PHASE3
raised, confirmed by the project owner: the friend grant is the more
encapsulation-preserving option (only `PassContext` gains access; nothing
else in the engine can name/construct these types), whereas moving them to
namespace scope would have made them engine-wide-nameable/constructible for
zero additional benefit.

**Structural consequence** (not itself a design deviation, just the
mechanical requirement this choice implies): C++ requires a nested type to
already be visible via qualified lookup before it can be named from outside
its enclosing class, which is only true once the enclosing class's own
class-body has been fully parsed. Since `PassContext` needs to name
`RenderGraph::PhysicalTexture` etc. directly in its own field declarations,
`PassContext`'s **full definition** was moved from its original position
(before `class RenderGraph`) to **after** the closing brace of
`class RenderGraph`, at the bottom of `RenderGraph.h`. The pre-existing
forward declaration (`struct PassContext;`, in `RenderGraphTypes.h`, already
transitively included by `RenderGraph.h`) is all `class RenderGraph` itself
needs to declare `BuildPassContext()`'s `PassContext`-by-value return type
and `PassRecord::execute`'s `std::function<void(PassContext&)>` — neither
requires a complete type at their own declaration point, so this reordering
required no other header changes.

### Open Question 2 — field-naming convention (LOCKED by direct evidence)

Resolved without needing `ask_questions`: read `RenderGraphTypes.h`'s own
plain data structs (`ResourceUsage`, `ColorAttachmentDesc`, `TextureDesc`,
`PassRecord`, etc.) before finalizing, per the plan's own instruction — none
of them use an `m_` prefix. `PassContext`'s new pointer members are named
`textures`/`buffers`/`volumeTextures` (no `m_` prefix), matching this file's
own sibling-struct convention exactly, rather than `RenderGraph`'s own
class-member `m_`-prefixed convention (`PassContext` is a plain value
struct, not a class hiding real behavior).

### Additional discovery — `recordDraw`/`recordIndirectDraw` cannot become bare member functions (LOCKED via `ask_questions`)

PHASE3's own Step 3.4 ("audit every real call site") found real production
call sites the plan's text did not anticipate:

- **~20 sites** (`Application.cpp` ×6, `RenderPasses.cpp` ×5,
  `ComputeBlurValidation.cpp` ×1, `GBufferValidation.cpp` ×2,
  `AtmosphereLutRenderer.cpp` ×6) do
  `m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);` — passing
  the bare field as a **value** into `Renderer::BeginGraphPassRecording()`'s
  own `std::function<void(bool, std::uint32_t, std::uint32_t)>`-typed
  parameter, never calling it with `()`.
- `Application.cpp` does `if (ctx.recordIndirectDraw) { ctx.recordIndirectDraw(); }`
  — a **truthiness check** before calling.

A non-static member function name cannot be used, unqualified, as a value or
in a boolean context in C++ (only called with `()`, or address-of'd via
`&PassContext::recordDraw` and bound to an object) — converting
`recordDraw`/`recordIndirectDraw` to bare ordinary member functions (as the
plan's Step 3.1 literally described for all six) would therefore have been a
hard compile error at every one of these ~21 sites, directly violating this
same phase's own harder "zero call-site change, whole engine compiles
unmodified" requirement.

Confirmed via `ask_questions`: `resolveReadTexture`/`resolveTexture`/
`resolveBuffer`/`resolveVolumeTexture` became plain ordinary member functions
exactly as planned (every one of their call sites is a direct
`ctx.resolveX(handle)` call — safe). `recordDraw`/`recordIndirectDraw`
instead became small, non-owning **callable struct fields**
(`PassContext::RecordDrawFn`/`PassContext::RecordIndirectDrawFn`, each
holding only a `DrawStats* drawStats`, with `operator()` and, for
`RecordIndirectDrawFn`, an explicit `operator bool()`) — implicitly
convertible to `std::function<...>` via their own `operator()` (exactly like
a lambda would be) and boolean-testable, so every one of the ~21 call sites
above keeps compiling and behaving identically, while still holding zero
lambda captures, zero heap allocation of their own, and zero
`std::function` stored inside `PassContext` itself.

### 1. `RenderGraph.h`

- Added `friend struct PassContext;` inside `RenderGraph`'s private section,
  immediately before the `PhysicalTexture`/`PhysicalBuffer`/
  `PhysicalVolumeTexture` struct definitions, with a doc comment explaining
  the locked decision above.
- `BuildPassContext()`'s own doc comment updated to describe the new
  pointer-assignment implementation instead of six lambda constructions.
- Moved `PassContext`'s **full definition** to the bottom of the file,
  after `class RenderGraph`'s closing brace, replacing:
  - `resolveReadTexture`/`resolveTexture`/`resolveBuffer`/`resolveVolumeTexture`:
    now ordinary `const noexcept` member functions (declarations only;
    `resolveTexture` stays a one-line inline forwarder, exactly as
    originally planned).
  - `const std::vector<RenderGraph::PhysicalTexture>* textures = nullptr;`
    (and the `buffers`/`volumeTextures` siblings) replace the old
    `std::function` fields.
  - `recordDraw`/`recordIndirectDraw`: now fields of two small nested
    callable struct types, `RecordDrawFn`/`RecordIndirectDrawFn` (each just
    `DrawStats* drawStats = nullptr;` plus `operator()`/`operator bool()`),
    per the additional discovery above.
- Every doc comment from the original six `std::function` fields was
  preserved (content-wise) at its corresponding new member's declaration.

### 2. `RenderGraph.cpp`

- `BuildPassContext()` rewritten from six lambda constructions to a small,
  obviously-correct block of pointer assignments:
  ```cpp
  PassContext ctx;
  ctx.cmd = cmd;
  ctx.textures = &physicalTextures;
  ctx.buffers = &physicalBuffers;
  ctx.volumeTextures = &physicalVolumeTextures;
  ctx.recordDraw.drawStats = &passDrawStats;
  ctx.recordIndirectDraw.drawStats = &passDrawStats;
  return ctx;
  ```
- Added out-of-line definitions for `PassContext::resolveReadTexture()`,
  `PassContext::resolveBuffer()`, `PassContext::resolveVolumeTexture()`,
  `PassContext::RecordDrawFn::operator()()`, and
  `PassContext::RecordIndirectDrawFn::operator()()` — each mirrors the exact
  logic (same bounds check, same `resolved` check, same returned value) the
  removed lambda bodies used to implement, now reading through the new
  pointer members instead of a captured local reference. All defensively
  null-check their pointer member before dereferencing, mirroring this
  codebase's general "cheap defensive check even when a real call site
  already guarantees it won't happen" style.

### 3. Audit of every real call site (Step 3.4 — performed, not assumed)

Grepped the whole `src/` tree for every one of the six method/field names.
Confirmed:
- `resolveReadTexture`/`resolveTexture`/`resolveBuffer`/`resolveVolumeTexture`:
  every real call site is a direct `ctx.resolveX(handle)` call — zero
  truthiness checks, zero pass-by-value usage anywhere. Safe as bare member
  functions.
- `recordDraw`: **zero** direct-call-syntax production call sites at all —
  every real usage passes `ctx.recordDraw` by value into
  `Renderer::BeginGraphPassRecording()` (`Application.cpp` ×6,
  `RenderPasses.cpp` ×5, `ComputeBlurValidation.cpp` ×1,
  `GBufferValidation.cpp` ×2, `AtmosphereLutRenderer.cpp` ×6). Handled by the
  `RecordDrawFn` callable-struct design above.
- `recordIndirectDraw`: one production call site
  (`Application.cpp`, GPU-driven indirect draw), using both a truthiness
  check and a direct call — handled by `RecordIndirectDrawFn`'s
  `operator bool()`/`operator()`.

### 4. Tests (Step 3.5)

**Consciously skipped**, documented here per the plan's own explicit
allowance: a new `RenderGraphPassContextTests.cpp` would need to construct
fake `RenderGraph::PhysicalTexture`/`PhysicalBuffer`/`PhysicalVolumeTexture`
vectors to exercise `PassContext`'s resolve methods — but Locked Design
Decision above deliberately scoped the `friend struct PassContext;` grant to
`PassContext` alone, not to any test type, so an external test file cannot
name or construct these types without a further, separate friend grant that
was not part of the confirmed decision. Extending friendship to a
test-only type was considered and rejected as unnecessary scope creep for
this phase — the live smoke test below (a real `RenderOpaque` draw showing
`Draws=1, Tris=12` in the Editor's "Render Graph" panel) already gives real,
production-path evidence that `PassContext::resolveReadTexture()`/
`RecordDrawFn::operator()()` correctly read/write through the new pointer
members end-to-end. If a future phase needs pure unit coverage of these
methods specifically, the cleanest path would be a small,
`RenderGraph`-internal self-check (mirroring how other private
implementation details in this codebase are verified), not a widening of
this friend grant.

## Verification

- Fast, targeted incremental compile check (no full build, per Locked Design
  Decision 7), but scoped to the WHOLE engine target as this task requires,
  since this phase touches the hottest per-frame path and every pass
  author's call site across the codebase must keep compiling unmodified:
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings.
  - `cmake --build build` (default target — `GreatTamanaEngine.exe` +
    `GreatTamanaEngineTests.exe`) — succeeds, 0 errors/warnings. Confirms
    every real pass `execute` callback across `Application.cpp`,
    `RenderPasses.cpp`, `ComputeBlurValidation.cpp`, `GBufferValidation.cpp`,
    `AtmosphereLutRenderer.cpp`, and GPU Skinning compiles completely
    unmodified, and every existing test file referencing `PassContext&`
    (`RenderGraphBuilderTests.cpp`, `RenderGraphCompilerTests.cpp`,
    `RenderGraphSnapshotTests.cpp`, `RenderPassTests.cpp`,
    `RenderPipelineTests.cpp`) still compiles.
- Live sanity check (`run_app_background` + `gte_send_request`):
  - `GET /get_swapchain` — Editor renders normally, sky/atmosphere gradient
    visible in both Scene and Game panels, correct docked layout, no
    black/corrupted frame.
  - `GET /get_logs?min_level=Warning` → `{"count":0,...}` both before and
    after the test below — confirms nothing regressed silently.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` — the
    "Render Graph" panel shows every real pass with correct, real per-pass
    GPU timing (e.g. `AtmosphereMultiScatteringLut` at ~2.2 ms) and correct
    Reads/Writes columns, byte-identical in shape to PHASE2's own report.
  - `POST /instantiate_primitive` (`{"shape":"Cube","name":"Phase3TestCube"}`)
    spawned a real cube entity — `GET /get_swapchain` confirms it renders
    correctly in both Scene and Game views, **and** the "Render Graph" panel
    now shows `RenderOpaque` with `Draws=1, Tris=12` — direct, real-path
    proof that `PassContext::recordDraw` (the new `RecordDrawFn` callable
    struct, writing through a plain `DrawStats*`) correctly accumulates
    per-pass draw stats through `Renderer::BeginGraphPassRecording()`'s
    stored callback, exactly as before this phase.
  - Cleaned up via `POST /delete_entity` and `stop_app_background`.

## What was NOT touched

- No pass author's `execute` callback body anywhere in `src/Application/`,
  `src/Editor/`, or any Renderer-layer pass declaration file — every one
  compiles completely unmodified (confirmed above).
- `RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` — untouched (PHASE4/
  PHASE5's job).
- `Renderer.h`/`Renderer.cpp`'s `BeginGraphPassRecording()` — untouched;
  its `std::function<void(bool, std::uint32_t, std::uint32_t)>` parameter
  type is exactly why `recordDraw` had to stay a callable-struct field
  rather than become a bare member function (see above).
- The names `resolveReadTexture`/`resolveTexture`/`resolveBuffer`/
  `resolveVolumeTexture`/`recordDraw`/`recordIndirectDraw` — unchanged.

## Next phase

`PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md` (item 2.3) — the
`RenderGraphCompiler::Compile()` algorithmic rewrite. No blocker or open
question was found that would change PHASE4's plan. One note worth carrying
forward: PHASE4/PHASE5/PHASE6 should double-check whether any future
`RenderGraphCompiler.cpp`/`RenderGraphBuilder.h` change ever needs to
construct/inspect a `PassContext` directly (unlikely, given those files
operate purely on `PassRecord`/`CompiledGraphInput`, never `PassContext`) —
if so, the same private-nested-type-visibility question this phase resolved
would need to be revisited for whatever new type is involved.
