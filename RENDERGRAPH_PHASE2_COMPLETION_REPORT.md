# RENDERGRAPH_PHASE2_COMPLETION_REPORT.md

Session report for **Phase 2 — Let Developers Describe Drawing Jobs**, the
second implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md` — nothing beyond that
document's own "Step 3: The Plan" was implemented, per its own "Step 4:
What We Will NOT Do".

## What shipped

Two new, additively-compiled files, one new test file, plus a small,
anticipated growth of two Phase 1 types (`ResourceUsage`/`PassRecord`) —
nothing else in the engine was touched, and nothing outside
`src/Renderer/RenderGraph/` includes any of it yet:

- **`src/Renderer/RenderGraph/RenderGraphBuilder.h`/`.cpp`** — the
  declarative "describe a frame's drawing jobs" authoring API:
  - **`RenderGraphBuilder`** — owns one frame's whole in-progress
    description: parallel `TextureDesc`/name/`TextureImportInfo` tables and
    `BufferDesc`/name tables (dense-indexed, one entry per handle minted
    this frame — nothing is ever removed mid-frame, so a plain `std::vector`
    per table was enough; no removal/generation-reuse semantics were
    needed, so `ResourcePool<T, HandleT>` itself was deliberately NOT
    reused here despite the strategy doc's "ResourcePool-style dense
    indexing" phrasing — a literal `ResourcePool` would have added
    remove/generation-guard machinery this one-frame-lifetime object never
    needs), plus a `std::vector<PassRecord>`.
    - `CreateTexture(name, desc)` / `CreateBuffer(name, desc)` — mint a
      brand-new TRANSIENT resource handle every call, even for an
      identical `desc` (handle identity and physical-resource identity are
      deliberately different concepts — Phase 4 is where two
      identical-`desc` requests might later get pooled to the same
      physical resource).
    - `ImportTexture(name, externalTarget, currentLayout)` — wraps an
      already-live, externally-owned `RenderTarget` (the swapchain image,
      or a persistent Editor `RenderTexture`) as a graph resource, tagged
      `TextureImportInfo::isImported = true` alongside the caller's
      **required, no-default** `currentLayout`. The resulting handle is
      indistinguishable, from a pass author's point of view, from a
      `CreateTexture()`-minted one — usable identically in
      `ReadTexture()`/`WriteColorAttachment()`/
      `WriteDepthStencilAttachment()`. A `TextureDesc` mirroring the
      external target's own real width/height/format/has-depth shape is
      also recorded (for Phase 8's future debug display only — Phase 4
      will never pool-match against an imported resource's desc, since it
      never allocates/frees one).
    - **`RenderGraphBuilder::PassBuilder`** (nested class) —
      `ReadTexture()`/`WriteColorAttachment()`/
      `WriteDepthStencilAttachment()` plus symmetric
      `ReadBuffer()`/`WriteBuffer()` (added now, alongside the texture
      methods, purely so `CreateBuffer()`'s `BufferHandle` has *some* way
      to be used inside a pass at all — no real Phase 7 pass needs these
      yet; they exist for the same "complete the API surface Phase 1
      already started" reason `CreateBuffer()` itself does, and are
      explicitly flagged in the strategy doc as "for a future compute
      pass"). Exposes nothing beyond "declare a read/write" — no live
      `VkCommandBuffer`, no `Renderer&`, no `GpuResourceFactory&`, per the
      strategy doc's own Step 4.
    - `AddPass(name, setup, execute)` (template) — `setup` runs
      **synchronously, exactly once, at the call site**, handed a
      `PassBuilder&` to declare this pass's reads/writes; `execute` is
      type-erased into `PassRecord::execute`
      (`std::function<void(PassContext&)>`) and stored, **never invoked**
      by `AddPass()`/`Finish()` themselves — only Phase 6's
      `RenderGraph::Execute()` will ever call it, and only for a pass that
      survives Phase 3's culling.
    - `Finish()` — consumes the builder, handing everything over as a
      plain `CompiledGraphInput` value (not yet "compiled" in any real
      sense — no ordering/culling has happened; Phase 3 is where that
      name earns its meaning).
  - **`CompiledGraphInput`** / **`TextureImportInfo`** — the plain
    hand-off structs described above; `CompiledGraphInput` is the natural
    Tier-1 test seam this phase's tests build against directly, with no
    Phase 3 code needing to exist yet.
- **`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`** — 25 new
  Tier-1 tests (19 ordinary + 6 death tests), entirely hand-constructed,
  zero live device, following the strategy doc's own Step 3.4 coverage
  list exactly:
  - `CreateTexture()`/`CreateBuffer()` mint distinct handles even for an
    identical `desc` (2 tests).
  - **The direct Phase 1→2 regression test**: two `CreateTexture()`/
    `CreateBuffer()` calls with *different* names but an *identical* desc
    still produce `TextureDesc`/`BufferDesc` values that compare **equal**
    — proving a resource's name has no bearing on Phase 4's future
    pool-matching logic (2 tests).
  - `AddPass()`'s `setup` runs synchronously exactly once; `execute` is
    captured but never invoked by `AddPass()`/`Finish()` (2 tests), plus a
    plain name/not-culled sanity check (1 test).
  - Every `PassBuilder` method (`ReadTexture` with an explicit and a
    defaulted access, `WriteColorAttachment`, `WriteDepthStencilAttachment`,
    `ReadBuffer`, `WriteBuffer`) appends with the exact right
    `ResourceKind`/handle/`ResourceAccess` (6 tests).
  - `ImportTexture()`: produces a handle usable identically to a
    `CreateTexture()`-minted one inside a real `AddPass()` declaration;
    is tagged `isImported` (alongside a sibling transient texture that is
    NOT); its **required `currentLayout`** parameter is recorded and
    exactly retrievable (the v2 regression coverage for the parameter that
    didn't exist at all in v1's own code sketch); its `externalTarget` is
    stored verbatim; its `TextureDesc` mirrors the external target's real
    shape; and imported/transient handles share one contiguous,
    non-colliding handle space (6 tests).
  - **Name-validation guard** (`#ifndef NDEBUG`-guarded `EXPECT_DEATH`
    tests, a new pattern for this codebase — no other test file uses
    death tests yet, but this is the correct, standard GoogleTest
    mechanism for testing an `assert()`-guarded precondition, and the
    strategy doc explicitly calls for this exact regression coverage): a
    `nullptr`/empty `name` is rejected by `CreateTexture()`,
    `CreateBuffer()`, `ImportTexture()`, and `AddPass()` (6 tests, only
    compiled/run in a build where `NDEBUG` is not defined — a Release
    build's `assert()` is a true no-op and would never actually abort, so
    these are skipped there rather than producing a false failure).

### Anticipated growth of Phase 1's `ResourceUsage`/`PassRecord`

Phase 1's own completion report explicitly flagged both of these as
"Phase 2's decision, once the builder API exists to give it context" —
this phase is where that happens, in `RenderGraphTypes.h`:

- **`ResourceUsage`** grew from a texture-only shape into a real
  tagged-union: a new `ResourceKind` enum (`Texture`/`Buffer`), a `kind`
  field, and a second `buffer` handle field alongside the original
  `texture` one — plus two static factory functions,
  `ResourceUsage::ForTexture(handle, access)`/`ForBuffer(handle, access)`,
  which are what every real call site (`PassBuilder`) actually constructs
  one through, so nothing outside `RenderGraphTypes.h`'s own tests needs
  to spell out all four fields by hand. Deliberately a plain "both fields
  present, one tag" struct rather than a `std::variant`, matching this
  codebase's general preference for plain, explicit structs (e.g.
  `TextureDesc`/`BufferDesc` are two separate structs, never one variant)
  over template-heavy machinery it doesn't otherwise use.
- **`PassRecord`** gained an `execute` field
  (`std::function<void(PassContext&)>`), and a new `struct PassContext;`
  forward declaration was added (fully specified only in Phase 6, once
  Phase 4's physical-resource-realization result exists to give it a real
  shape) — `PassRecord::execute` only needs to know it exists as an opaque
  type its `std::function` closes over.
- The **existing** Phase 1 test file,
  `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`, was updated to
  match: the one existing test that hand-constructed a `ResourceUsage` via
  positional aggregate-initialization now uses `ResourceUsage::ForTexture()`
  instead (the old two-argument-positional shape no longer type-checks
  now that `kind` is the struct's first member), a `kind` assertion was
  added alongside the pre-existing `texture`/`access` ones, two brand-new
  tests (`ForTextureSetsKindAndTextureFields`/`ForBufferSetsKindAndBufferFields`)
  cover the factory functions directly, and the default-constructed
  `PassRecord` test now also asserts `execute` starts as an empty (falsy)
  `std::function`. This is exactly the kind of "every change to Tier 1
  code must come with a matching test change" update AGENTS.md calls for
  — modifying a Phase 1 type in Phase 2 was explicitly anticipated by
  Phase 1's own handoff notes, not a scope violation.

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphBuilder.h`/`.cpp` to `gte_core`'s
  source list, right after the existing `RenderGraphTypes.h`/`.cpp` entry.
- `tests/CMakeLists.txt`: added
  `Renderer/RenderGraph/RenderGraphBuilderTests.cpp` to `GTE_TEST_SOURCES`
  (right after `RenderGraphTypesTests.cpp`), plus a matching entry in the
  file's own Tier-1 taxonomy comment block.

## Verification performed

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with zero warnings/errors introduced by the new/changed files.
- Ran the **new** Render Graph tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all **54** pass (the 29 pre-existing
  Phase 1 tests, unchanged in count, plus the 25 new Phase 2 tests
  described above), including all 6 `NDEBUG`-guarded death tests.
- Built the real `GreatTamanaEngine.exe` target too — succeeded cleanly,
  confirming the new files don't break the shipping executable's build.
- Ran the **entire** test suite — **575 tests total**, **574 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test noted in
  `TESTING.md`/`README.md`, unrelated to this change). **Zero
  regressions** — every test that passed before this session still
  passes.

## Acceptance criteria check (against the strategy doc's own Step 3.4)

- ✅ `CreateTexture()`/`CreateBuffer()` mint distinct handles even for an
  identical desc.
- ✅ Two different `name`s with an identical `desc` still produce
  structurally-equal `TextureDesc`/`BufferDesc` values (the direct
  Phase 1 v2 regression test, now also exercised through the builder
  itself, not just the raw struct).
- ✅ `AddPass()`'s `setup` runs synchronously exactly once at the call
  site.
- ✅ `AddPass()`'s `execute` is captured but never invoked by
  `AddPass()`/`Finish()`.
- ✅ Every `PassBuilder` method appends to the right list with the right
  `ResourceAccess`.
- ✅ `ImportTexture()` produces a handle indistinguishable from a
  `CreateTexture()`-minted one from a pass author's point of view, is
  tagged `isImported` (verified via `Finish()`'s `CompiledGraphInput`),
  and its `currentLayout` is exactly recorded/retrievable.
- ✅ A `nullptr`/empty name is rejected (debug-build assertion) for every
  naming call site: `CreateTexture()`, `CreateBuffer()`, `ImportTexture()`,
  `AddPass()`.

## What was deliberately NOT done (per the strategy doc's own Step 4)

- No `RenderGraphCompiler`, `RenderGraphResourcePool`, or `RenderGraph`
  itself — `Finish()` returns a plain, uncompiled `CompiledGraphInput` and
  stops there.
- `setup` never receives a live `VkCommandBuffer`, a `Renderer&`, or a
  `GpuResourceFactory&` — only a `PassBuilder&`.
- No re-declare/overwrite-an-existing-pass-by-name ("upsert") semantics —
  every `AddPass()` call mints a brand-new `PassRecord`; a graph is
  rebuilt, in full, from scratch, every frame.
- No automatic resource-usage validation (e.g. "this pass reads a texture
  nothing ever writes") — that is explicitly Phase 3's job, once the whole
  graph's shape is visible to compile against.
- No name/label field was reintroduced onto `TextureDesc`/`BufferDesc`
  themselves — a resource's name lives exclusively in
  `RenderGraphBuilder`'s own parallel name tables (and, after `Finish()`,
  `CompiledGraphInput::textureNames`/`bufferNames`).
- No convenience overloads beyond what the strategy doc's own Step 3.1
  sketch specified (e.g. no sampler/descriptor-set-binding shortcut on
  `ReadTexture()`) — that is explicitly Phase 6's `PassContext` concern,
  once real physical resources exist to bind.

## Handoff notes for whoever picks up Phase 3

- Phase 3 (`RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md`) is the next
  unit of work in this campaign — dependency resolution, culling,
  topological ordering, and resource lifetimes, consuming exactly the
  `CompiledGraphInput` this phase produces via `RenderGraphBuilder::Finish()`.
- `PassRecord::isCulled` already exists (from Phase 1) and defaults to
  `false` — Phase 3 is the first code that will ever write `true` to it;
  nothing in Phase 1/2 does.
- `PassRecord::execute` already exists and is already populated correctly
  by `AddPass()` — Phase 3 does not need to touch it at all; Phase 6 is
  the first code that will ever call it (and only for a pass whose
  `isCulled == false`).
- `TextureImportInfo`/`CompiledGraphInput::textureImportInfo` (this
  phase's own new types) are what Phase 4 will read to decide "allocate a
  fresh physical resource" (transient, `isImported == false`) vs. "use
  this already-live `RenderTarget` as-is, seeded at `currentLayout`"
  (imported, `isImported == true`) — Phase 3 itself has no reason to
  branch on `isImported` for ordering/culling purposes (an imported
  resource's reads/writes are ordered exactly like a transient one's), but
  should leave both fields untouched as it passes them through.
- `ResourceUsage::kind`/`buffer` (this phase's own tagged-union growth) are
  real and tested, but genuinely UNUSED by any Phases 1-8 real pass so far
  — `ReadBuffer()`/`WriteBuffer()` exist purely so `CreateBuffer()`'s
  `BufferHandle` has *some* consumer; Phase 3's dependency-graph
  computation should still handle the `Buffer` case symmetrically to
  `Texture` (via `IsWriteAccess()`, which already works uniformly across
  both, and `ResourceUsage::kind`/`.buffer` if it needs to distinguish
  which underlying table a `ResourceUsage` refers to), even though no real
  Phase 7 pass will exercise that path yet.
- Do not add a `debugName`/name-like field back onto `TextureDesc`/
  `BufferDesc` — see Phase 1 v2's own standing rule, still fully in force.
  A resource's name lives in `RenderGraphBuilder`'s parallel table (now
  `CompiledGraphInput::textureNames`/`bufferNames` after `Finish()`) and
  nowhere else.
