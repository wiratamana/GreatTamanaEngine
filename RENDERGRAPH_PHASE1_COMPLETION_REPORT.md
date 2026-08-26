# RENDERGRAPH_PHASE1_COMPLETION_REPORT.md

Session report for **Phase 1 — Teach the Engine New Words**, the first
implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md` — nothing beyond that
document's own "Step 3: The Plan" was implemented, per its own "Step 4:
What We Will NOT Do".

## What shipped

Two new, additively-compiled files plus one test file — nothing else in the
engine was touched:

- **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — the render graph's
  entire vocabulary, living in the nested namespace `gte::rg` (mirroring
  `gte::Profiling`'s own nested-namespace precedent):
  - **Handles**: `TextureHandle`, `BufferHandle`, `PassHandle` — three
    distinct, cheap, generational `{index, generation}` PODs, mirroring
    `gte::Entity`/`gte::GpuResourceHandle` exactly (same index+generation
    shape, `IsValid()`, `operator==`). Deliberately three separate structs,
    never one shared template, so the compiler catches a
    `TextureHandle`-where-a-`BufferHandle`-was-expected mistake at compile
    time (same reasoning as `MeshHandle`/`PipelineHandle`/`TextureHandle`
    already being distinct types).
  - **`ResourceAccess`** enum (`ColorAttachmentWrite`,
    `DepthStencilAttachmentReadWrite`, `ShaderRead`, `TransferSrc`,
    `TransferDst`) — deliberately scoped to exactly what the Phases 1-8
    graphics-only MVP needs, not one value larger (compute-shader-only
    access kinds are explicitly Phase 9 backlog).
  - **`TextureDesc`/`BufferDesc`** — plain, structurally-comparable resource
    descriptors (`operator== = default`). **Critically, neither struct
    carries a `debugName`/name field of any kind** — this is the direct,
    deliberate fix for the real correctness bug the v2 revision of the
    strategy doc called out (a v1 `TextureDesc::operator==` that would have
    silently compared `debugName`'s raw pointer identity instead of the
    resource's actual physical shape, defeating Phase 4's future resource
    pooling for any two logically-identical requests issued from different
    call sites). A resource's human-readable name is documented as a
    parameter to be threaded separately wherever a later phase needs it
    (`CreateTexture(name, desc)`, `AcquireTexture(desc, debugName)`), never
    baked into the comparable desc struct itself — the file's own "standing
    rule" comment spells out the exact question ("does this field change
    whether two requests can share one physical allocation?") that must be
    asked before any future field is added to either struct.
  - **`ResourceUsage`/`PassRecord`** — the pure records Phase 2's builder
    API will fill in and Phase 3's compiler will read (declared reads/
    writes, cull flag). `PassRecord::name` deliberately follows the same
    "string literal / static storage, never compared for equality" rule
    `GTE_PROFILE_SCOPE` already established.
  - Every enum comes with pure free-function helpers
    (`IsWriteAccess(ResourceAccess)`, `ToString(ResourceAccess)`), both
    written as an exhaustive `switch` with **no `default:` case** — a
    future `ResourceAccess` enumerator added without updating both
    functions will trigger this toolchain's `-Wswitch`-class warning at the
    exact two call sites that need to know about it, rather than silently
    picking a wrong default.
  - Deliberately Vulkan-header-**present**-but-Vulkan-**call**-free: only
    `VkFormat`/`VkDeviceSize`/`VkBufferUsageFlags` (via a minimal
    `<volk.h>` include, exactly like `FrameRecorder.h`'s own `DrawItem`)
    appear anywhere in the file — no live `VkDevice`, no `Renderer`, no
    `Registry` dependency anywhere. This is a deliberate, documented
    difference from `GpuTiming.h` (which has zero Vulkan-header dependency
    at all), explained inline in the header's own top comment.
- **`src/Renderer/RenderGraph/RenderGraphTypes.cpp`** — the two non-trivial
  pure functions named above (`IsWriteAccess`/`ToString`), split out of the
  header the same way `GpuTiming.h`/`GpuTiming.cpp` already split
  declarations from logic.
- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — 27 new
  Tier-1 tests, entirely hand-constructed, zero live device, following
  `tests/Renderer/DrawStatsTests.cpp`'s own table-driven style:
  - Handle default-invalid / explicit-valid / equality-compares-both-fields
    coverage for all three handle types (9 tests).
  - `TextureDesc`/`BufferDesc` value-equality coverage: identical descs
    compare equal, and changing any single field (width/height/format/
    hasDepth for `TextureDesc`; size/usage for `BufferDesc`) makes them
    compare unequal (8 tests) — plus **one explicit v2 regression test**
    (`TextureDescsBuiltFromSeparateRuntimeInputsStillCompareEqualWhenStructurallyIdentical`)
    that builds two structurally-identical `TextureDesc` values from
    entirely separate, runtime-computed (never compile-time-folded) inputs
    and asserts they still compare equal — the direct regression test for
    the exact bug this phase's "no debugName field" fix addresses.
  - `IsWriteAccess()` — one case per `ResourceAccess` enumerator (5 tests).
  - `ToString()` — a non-null/non-empty check across every enumerator,
    plus an exact-string check per enumerator (2 tests).
  - `PassRecord`/`ResourceUsage` — basic plain-data shape sanity (default
    state, independent reads/writes appending) (2 tests).

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphTypes.h`/`.cpp` to `gte_core`'s
  source list (right after `Vulkan/VulkanQueryPool.h`).
- `tests/CMakeLists.txt`: added
  `Renderer/RenderGraph/RenderGraphTypesTests.cpp` to `GTE_TEST_SOURCES`,
  plus a matching entry in the file's own Tier-1 taxonomy comment block
  (mirroring every other entry's format).

Both changes follow the exact shape `GpuTiming.h/.cpp` +
`tests/Renderer/GpuTimingTests.cpp` were added in, per the strategy
document's own acceptance criteria (Step 3.4).

## Verification performed

- Configured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built `GreatTamanaEngineTests` from a clean, incremental build — compiled
  with zero warnings/errors introduced by the new files.
- Ran the **new** tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all 27 pass.
- Ran the **entire** test suite (548 tests total) — **547 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  a pre-existing machine-gated smoke test unrelated to this change, skipped
  because the referenced real MMD model file isn't present on this
  machine — expected and documented in `TESTING.md`/`README.md`). **Zero
  regressions.**
- Built the real `GreatTamanaEngine.exe` target too (not just the test
  binary) to confirm the new files don't break the shipping executable's
  build — succeeded cleanly.

## Acceptance criteria check (against the strategy doc's own Step 3.4)

- ✅ `RenderGraphTypesTests.cpp` compiles and passes with zero other
  `src/Renderer/RenderGraph/` files existing besides `Types.h/.cpp`.
- ✅ No existing file anywhere in `src/` includes
  `RenderGraph/RenderGraphTypes.h` yet (grep-confirmed) — nothing consumes
  this until Phase 2, as intended.
- ✅ `CMakeLists.txt`/`tests/CMakeLists.txt` each gained exactly one new
  source-pair/test-file entry, added the same way `GpuTiming.h/.cpp` +
  its test file were.
- ✅ `TextureDesc`/`BufferDesc` contain no field that isn't a genuine
  determinant of physical-resource shareability (reviewed against the
  "standing rule" in the strategy doc's Step 3.1 before considering this
  phase done).

## What was deliberately NOT done (per the strategy doc's own Step 4)

- No `RenderGraphBuilder`, `RenderGraph`, or any class with actual
  behavior — Phase 2's job.
- No decision on the shape of a pass's execute callback
  (`std::function<void(PassContext&)>` vs. anything else) — Phase 2's
  decision, once the builder API exists to give it context.
- No `ResourceAccess` values beyond what the Phases 1-8 MVP needs (no
  storage-image/buffer read-write, no present-src-as-first-class-access) —
  Phase 9 backlog.
- No field on `TextureDesc`/`BufferDesc` that exists purely for human/debug
  consumption — see the "standing rule" above.

## Handoff notes for whoever picks up Phase 2

- Phase 2 (`RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md`) is the next
  unit of work in this campaign — the declarative `AddPass()`/
  `CreateTexture()`/`ImportTexture()` authoring API built directly on top
  of the types this phase shipped.
- `ResourceUsage` in this phase is deliberately simplified (a single
  `TextureHandle` field) — Phase 2's own strategy document is where it
  grows into the real tagged-union shape (texture OR buffer) that actual
  pass declarations need. This was called out explicitly in-line in
  `RenderGraphTypes.h`'s own comment on `ResourceUsage` so it isn't
  mistaken for an oversight.
- Do not add a `debugName`/name-like field to `TextureDesc`/`BufferDesc`
  without first asking (and answering, in a code comment) the "standing
  rule" question in `RenderGraphTypes.h`'s own header comment: "does this
  field change whether two requests can share one physical allocation?"
  Every future field on either struct must be justified against that
  question before it's added.
