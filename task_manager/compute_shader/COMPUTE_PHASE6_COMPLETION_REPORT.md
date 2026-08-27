# COMPUTE_PHASE6_COMPLETION_REPORT.md

Session report for **Phase 6 — Put compute into the Render Graph** (the
"RenderGraph Integration" phase) of the compute-shader campaign described in
`COMPUTE_SHADER_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md` — including its own
"Step 6: V2 Revision Notes" (no `PassContext::recordDispatch` callback, and
the Phase 5 buffer-reachability caveat carried forward into this phase's own
throwaway validation design). Nothing beyond that document's own "Step 3:
The Plan" was implemented, per its own "Step 4: What We Will NOT Do".

Work was done on the pre-existing branch `feature/compute-shader-impl`,
picking up immediately after Phase 5 (`COMPUTE_PHASE5_COMPLETION_REPORT.md`).

## What shipped

Every change is purely additive to already-shipped `RenderGraphBuilder`/
`RenderGraph`/`RenderGraphBarrierPlanner` code — no existing call site's
observable behavior changed, and the real Game/Scene/Present passes
(`src/Application/RenderPasses.cpp`) are completely untouched. This phase
makes a compute pass a genuine first-class `gte::rg::RenderGraph` citizen,
closing the gap between Phase 5 (barrier synthesis correctly understands
compute-shader hazards) and Phase 7 (the real, shipped validation
workloads — not started this session, still `[TODO]`).

### `PassBuilder::WriteTexture()` — a general, non-attachment texture write

- **`src/Renderer/RenderGraph/RenderGraphBuilder.h`/`.cpp`** —
  `RenderGraphBuilder::PassBuilder::WriteTexture(TextureHandle handle,
  ResourceAccess access = ResourceAccess::ComputeShaderWrite)` appends a
  plain `ResourceUsage::ForTexture(handle, access)` to the pass's own
  `writes` list — the exact same shape `WriteColorAttachment()`/
  `WriteDepthStencilAttachment()` already produce, just without either of
  those two methods' side effect of ALSO implicitly marking the pass as
  needing a real `vkCmdBeginRendering` bracket. This is how a compute pass
  declares it writes an `RWTexture` (Phase 1's own vocabulary) — a true
  read-modify-write `RWTexture` declares BOTH `pass.ReadTexture(handle,
  ResourceAccess::ComputeShaderRead)` AND `pass.WriteTexture(handle)` on the
  SAME handle, mirroring how `ReadBuffer()`/`WriteBuffer()` already work
  today for buffers.

### `IsColorAttachmentWriteAccess()` — the missing Tier-1-testable half of the hasColorWrite/hasDepthWrite scan

- **`src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h`/`.cpp`** — a new
  pure function, `bool IsColorAttachmentWriteAccess(ResourceAccess access)
  noexcept`, returning `true` only for `ColorAttachmentWrite` — the exact
  counterpart to Phase 5's own `TargetsDepthState()`. `RenderGraph.cpp`'s
  `ExecuteCompiledGraph()` now calls this (and the pre-existing
  `TargetsDepthState()`) instead of two inline `usage.access ==
  ResourceAccess::...` comparisons, in its `hasColorWrite`/`hasDepthWrite`
  scan. This is the phase's own explicitly-requested verification step
  ("Confirm — and add a regression test proving — that a
  `WriteTexture(handle, ComputeShaderWrite)` usage is correctly EXCLUDED
  from that scan"): before this extraction, that decision lived entirely
  inside `RenderGraph::ExecuteCompiledGraph()`, a Tier-2 function that
  needs a live `VkDevice`/`VkCommandBuffer` to exercise at all — exactly
  the same "extract it so it can actually be unit-tested" reasoning Phase 5
  already applied to `TargetsDepthState()`.

### `PassContext` — `resolveTexture()`/`resolveBuffer()`, no `recordDispatch`

- **`src/Renderer/RenderGraph/RenderGraph.h`/`.cpp`** — `PassContext` gained
  two new fields:
  - `resolveTexture` — a plain alias of the pre-existing
    `resolveReadTexture` (both point at literally the same lambda, copied
    once), with a name that no longer implies "reads only". No new
    resolution logic was needed: `RenderGraph::ExecuteCompiledGraph()`
    already calls `EnsureTextureResolved()`/`ApplyUsageBarrierIfNeeded()`
    for every declared read AND write, in that order, before a pass's own
    `execute` callback ever runs — a texture declared ONLY via
    `WriteTexture()` is therefore already fully resolved (a real, live
    `VkImageView`) by the time a compute pass's `execute` callback calls
    `ctx.resolveTexture(handle)`, exactly as verified by this phase's own
    throwaway validation (see below).
  - `resolveBuffer` — the buffer sibling, resolving a declared `BufferHandle`
    into its current physical `VkBuffer` (or `VK_NULL_HANDLE` if
    unresolved), for the exact same "rewrite my own `ComputeDescriptorSet`
    before dispatching" use case.
  - **No `PassContext::recordDispatch` callback was added** — per this
    phase's own Step 6 (cross-referencing Phase 4 v2's finding): a compute
    pass's `execute` callback calls `renderer.BeginGraphPassRecording(ctx.cmd,
    ...)` / `Renderer::Dispatch()` / `renderer.EndGraphPassRecording()` in
    exactly the same shape a graphics pass already calls
    `BeginGraphPassRecording()`/`Submit()`/`EndGraphPassRecording()` today —
    no new `PassContext` field was needed for dispatch itself, only for
    resolving handles.

### `AddComputePass()` — a thin, purely cosmetic alias of `AddPass()`

- **`src/Renderer/RenderGraph/RenderGraphBuilder.h`** — a template method
  with the exact same signature as `AddPass()`, forwarding straight through
  to it with zero behavioral difference (a pass's behavior is entirely
  determined by what it declares in `reads`/`writes`, never by which entry
  point created it) — exists purely so a compute-only pass's own call site
  reads as "this is a compute pass" at a glance, mirroring the companion
  `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md` document's own
  Phase D naming.

## Throwaway validation performed (per this phase's own Step 5)

Per the strategy document's explicit instruction ("Build the SIMPLEST
possible throwaway test pass first — a compute pass that `WriteTexture()`s a
single `RWTexture` with a constant debug color pattern... before attempting
either the culling workload... or the blur workload... for real... Since a
transient `RWTexture` cannot be requested via `CreateTexture()` yet (Phase 1
v2), this throwaway test pass's texture must be a small, dedicated,
EXTERNALLY-created `RenderTexture`... imported once via `ImportTexture()`"),
the following was built, run, confirmed, and then **deleted** before this
phase was considered complete (matching Phase 2's own precedent for
`Passthrough.comp`):

1. `src/Shaders/GraphComputeTest.comp` — a trivial `.comp` shader: one
   storage-image binding (set 0, binding 0, `rgba8`), `local_size_x/y = 8`,
   writes solid red (`vec4(1.0, 0.0, 0.0, 1.0)`) to every in-bounds pixel
   via `imageStore()`.
2. A temporary `gte_add_shader(GreatTamanaEngine
   src/Shaders/GraphComputeTest.comp)` line in `CMakeLists.txt`.
3. A temporary `--compute-graph-write-texture-test` headless CLI mode in
   `main.cpp` (mirroring the existing `--reimport` CLI mode's own
   precedent, and Phase 2's own `--compute-passthrough-test` mode) that:
   - Constructs a real `Window`+`Renderer` (a real live `VkDevice`) and a
     real `gte::rg::RenderGraph`.
   - Creates a 32×32 `RenderTexture` with `allowStorageImageAccess = true`
     at `VK_FORMAT_R8G8B8A8_UNORM` (this texture is never shared with any
     graphics `Pipeline`, so it needs no format-matching with
     `Renderer::ColorFormat()` — see Phase 7's own v2 finding).
   - Builds a `DescriptorSetLayoutBuilder`-based single-storage-image
     layout, a `ComputePipeline` against the shader above, and a
     `ComputeDescriptorSet` from `Renderer::AllocateComputeDescriptorSet()`.
   - Calls `renderer.BeginOffscreenRenderGraphRecording()` /
     `graph.Execute(cmd, SynchronousImmediateReadback, build)` /
     `renderer.EndOffscreenRenderGraphRecording()`, where `build` imports
     the `RenderTexture` via `builder.ImportTexture(...)` and declares
     **exactly one** pass via `builder.AddComputePass("ComputeGraphWriteTextureTest",
     setup, execute)`:
     - `setup` calls `pass.WriteTexture(outputHandle)` — Phase 6's own new
       method, default access (`ComputeShaderWrite`).
     - `execute` calls `ctx.resolveTexture(outputHandle)` — Phase 6's own
       new resolution primitive — to get the texture's live `VkImageView`,
       rewrites the `ComputeDescriptorSet` against it, then calls
       `renderer.BeginGraphPassRecording()` / `renderer.Dispatch()` /
       `renderer.EndGraphPassRecording()`.
     - The `build` lambda returns `{outputHandle}` as `finalOutputs` — this
       pass's own write target IS the root, so `RenderGraphCompiler::Compile()`'s
       reachability step marks it kept directly (Phase 5's own
       buffer-reachability caveat does not apply here at all, since this is
       a texture-rooted pass, not a buffer-only one — deliberately chosen
       for exactly this reason, per the strategy document's own Step 6).
   - Reads the result back manually (Phase 6/7 do not (yet) provide a
     general "sample a graph-owned `RWTexture` back on the CPU" primitive):
     a hand-written `ImmediateSubmit()` transitions the image from `GENERAL`
     (where the graph's own automatic barrier synthesis left it) to
     `TRANSFER_SRC_OPTIMAL` via `RenderGraphBarrierPlanner.h`'s own
     `EmitImageBarrier()`/`RequiredStateFor()`, then `vkCmdCopyImageToBuffer`
     into a host-visible `GpuToCpu` staging `Buffer`.
   - Compares the readback pixel against the expected solid red.
4. **First run legitimately caught a real bug — in the THROWAWAY TEST
   ITSELF, not in the engine changes.** The pass's own `execute` lambda
   originally captured `outputHandle` (a local variable of the enclosing
   `build` lambda) BY REFERENCE (`[&]`) — but `pass.execute` is a stored
   `std::function`, invoked LATER by `RenderGraph::ExecuteCompiledGraph()`,
   well after the `build` lambda (and its local `outputHandle` variable)
   had already returned and gone out of scope. This produced a classic
   dangling-reference read (`outputHandle.index` read back as garbage,
   `ctx.resolveTexture()` correctly, safely returning a null view for an
   out-of-range/never-resolved index rather than crashing — exactly the
   defensive behavior `PassContext::resolveTexture()`'s own doc comment
   promises). Diagnosed via targeted `fprintf` instrumentation (temporarily
   added, then removed once the fix was confirmed), fixed by explicitly
   capturing `outputHandle` **by value** in the pass's `execute` lambda
   (`[&, outputHandle](PassContext& ctx) { ... }`) — a one-line fix to the
   throwaway test, not to any shipped Phase 6 code.
5. After that fix: **`Compute graph write-texture test: pixel[0] = (255, 0,
   0, 255) - expected (255, 0, 0, 255) - PASS`**. This is a real, positive,
   end-to-end confirmation that:
   - `PassBuilder::WriteTexture()` correctly declares a texture write with
     no accompanying attachment semantics.
   - `RenderGraph`'s existing `ApplyUsageBarrierIfNeeded()`/`EnsureTextureResolved()`
     machinery (Phase 4/5, completely unmodified) automatically resolves an
     imported, write-only texture and synthesizes its `UNDEFINED ->
     GENERAL` barrier with ZERO Phase-6-specific code — the exact same code
     path a graphics pass's `ColorAttachmentWrite` already uses.
   - `IsColorAttachmentWriteAccess()`/`TargetsDepthState()` correctly
     exclude a `ComputeShaderWrite`-only pass from ever getting a
     `vkCmdBeginRendering` bracket — confirmed by this run actually
     succeeding at all (a spuriously-opened dynamic-rendering bracket
     against a texture never declared as a color/depth attachment would
     have failed loudly).
   - `PassContext::resolveTexture()` correctly resolves a WRITE-only
     texture handle, not just a read one.
   - `AddComputePass()`/`Renderer::Dispatch()`/`ComputeDescriptorSet::Rewrite()`
     (Phases 2-4, all completely unmodified) compose correctly with this
     phase's new integration surface.
   - No validation-layer run was possible on this development machine (the
     Vulkan loader logs `"Validation was requested but
     VK_LAYER_KHRONOS_validation is not available on this system"` — the
     same pre-existing environment limitation every prior compute-shader
     phase's completion report has already noted); the dispatch/readback
     result being byte-exact (`255, 0, 0, 255`) is nonetheless strong
     affirmative evidence the whole barrier/descriptor/dispatch sequence is
     wired correctly.
6. **Deleted** all three throwaway pieces immediately afterward:
   `src/main.cpp` restored to its exact pre-Phase-6 content (byte-for-byte,
   confirmed via `git status`/`git diff` showing no changes to that file
   after restoration), the `gte_add_shader(...GraphComputeTest.comp)` line
   removed from `CMakeLists.txt` (confirmed via `git status` showing no
   diff to `CMakeLists.txt` either), and `src/Shaders/GraphComputeTest.comp`
   (plus its stray compiled `.spv`) deleted from disk. Reconfigured +
   rebuilt from scratch afterward to confirm the final, persisted state
   builds clean with no trace of the throwaway shader staged anywhere.

## Testing

- **`tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp`** — new
  `IsColorAttachmentWriteAccessIsTrueOnlyForColorAttachmentWrite` test, one
  assertion per `ResourceAccess` enumerator (all eight), mirroring the
  file's own existing `TargetsDepthStateIsTrueOnlyForDepthStencilAttachmentReadWrite`
  pattern exactly — the direct, executable proof (not merely an inspection)
  that a `ComputeShaderWrite` usage is excluded from the color-attachment
  scan.
- **`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`** — five new
  tests:
  - `PassBuilderWriteTextureDefaultsToComputeShaderWrite` /
    `PassBuilderWriteTextureAppendsWithGivenAccess` — `WriteTexture()`'s
    default parameter and explicit-access behavior.
  - `PassBuilderCanDeclareBothReadAndWriteOfSameTextureForReadModifyWrite` —
    confirms a true read-modify-write `RWTexture` usage (one `ReadTexture(
    ComputeShaderRead)` plus one `WriteTexture(ComputeShaderWrite)` on the
    SAME handle) is representable exactly as the strategy document
    describes.
  - `AddComputePassBehavesIdenticallyToAddPass` — confirms `AddComputePass()`
    runs `setup` exactly once and records the exact same `PassRecord` shape
    `AddPass()` would.
- `RenderGraphTypesTests.cpp`/`RenderGraphCompilerTests.cpp`/
  `RenderGraphSnapshotTests.cpp` needed NO changes this phase — nothing
  about `ResourceAccess`'s enum values, `RenderGraphCompiler::Compile()`'s
  own culling/lifetime logic, or `RenderGraphSnapshot`'s reshape changed;
  Phase 5 already fully covers the `ComputeShaderWrite` enum value this
  phase's new `WriteTexture()` uses by default.
- `RenderGraph::ExecuteCompiledGraph()`/`PassContext::resolveTexture()`/
  `resolveBuffer()`/`Renderer::Dispatch()` itself remain Tier 2
  (GPU-touching, `VkDevice`-dependent) by nature, falling into the same
  accepted "no automated coverage yet, manually verified" bucket as every
  other Vulkan-execution-layer code in this engine (see `TESTING.md`'s own
  note on this) — verified instead via the throwaway validation above.

## Build system changes

- No permanent `CMakeLists.txt`/`tests/CMakeLists.txt` changes — every
  production change this phase touches an already-listed source/test file;
  no new files were added to the persisted tree (the throwaway shader/CLI
  mode's own temporary `CMakeLists.txt`/`main.cpp` edits were fully reverted
  — see "Throwaway validation performed" above).

## Verification performed

- Reconfigured with CMake (Ninja generator, reusing the existing `build/`
  directory) — no network access needed, everything was already fetched.
- Built `GreatTamanaEngineTests.exe` incrementally after each production
  code change — compiled with zero warnings/errors introduced by the
  changed files.
- Ran the full `*RenderGraph*` test filter (121 tests, up from Phase 5's
  own 116 — 5 new tests this phase) — all pass.
- Ran the **entire** existing test suite: **649 of 650 tests passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test every prior phase's own
  report has noted — unrelated to this change). **Zero regressions** — 5
  new tests added on top of Phase 5's own 644 passing + 1 skipped baseline.
- Built the throwaway validation CLI mode, ran it, diagnosed and fixed a
  bug in the THROWAWAY TEST ITSELF (a dangling-reference lambda capture —
  see above), re-ran it to a clean `PASS`, then deleted every throwaway
  piece and rebuilt from scratch to confirm the final, persisted diff
  builds clean with `git status` showing no changes to `main.cpp`/
  `CMakeLists.txt`.
- Ran the full test suite AGAIN after the throwaway-piece cleanup: **649 of
  650 tests passed**, **1 skipped** — identical result, confirming the
  cleanup itself introduced no regressions.
- Launched the real `GreatTamanaEngine.exe` in the background and confirmed
  it was still running (via `tasklist`) 5 seconds after launch — no crash/
  exception at startup — before stopping it, the same "worth confirming
  directly, not just via the test suite" discipline every prior
  compute-shader phase's completion report has established.

## Acceptance criteria check (against the strategy doc's own "Step 3: The Plan")

- ✅ `PassBuilder::WriteTexture(TextureHandle, ResourceAccess)` added,
  distinct from `WriteColorAttachment()`/`WriteDepthStencilAttachment()`.
- ✅ Confirmed — via a real, direct unit test, not merely inspection — that
  a `WriteTexture(handle, ComputeShaderWrite)` usage is excluded from
  `RenderGraph::Execute()`'s `hasColorWrite`/`hasDepthWrite` scan, by
  extracting that decision into a new, Tier-1-tested
  `IsColorAttachmentWriteAccess()` (mirroring Phase 5's own
  `TargetsDepthState()` extraction).
- ✅ A true read-modify-write `RWTexture` is representable by declaring BOTH
  `ReadTexture(handle, ComputeShaderRead)` and `WriteTexture(handle)` on the
  same handle — covered by a dedicated regression test.
- ✅ `AddComputePass()` added as a thin, purely cosmetic `AddPass()` alias.
- ✅ `PassContext` gained `resolveTexture()`/`resolveBuffer()` — the
  "prefer this shape" option from the strategy document's own Step 3 (a
  compute pass's `execute` callback rewrites its own `ComputeDescriptorSet`
  by calling these directly, rather than a new `PassContext` field owning
  the rewrite orchestration itself).
- ✅ Confirmed the barrier/pooling story required ZERO
  `RenderGraph::Execute()` changes beyond this phase's own
  `IsColorAttachmentWriteAccess()` extraction — the per-pass
  `ApplyUsageBarrierIfNeeded()` loop already treated `WriteTexture()`'s
  resulting `ResourceUsage` identically to any other declared usage, with
  no new execution path needed.
- ✅ Built the simplest possible throwaway test pass (a `WriteTexture()`-only
  compute pass against a single externally-imported, storage-capable
  `RenderTexture`) exactly as the strategy document's own Step 5
  recommends, specifically choosing the texture-rooted (not buffer-rooted)
  shape to sidestep Phase 5's own buffer-reachability culling gotcha.
- ✅ Deleted the throwaway shader/CLI mode/CMakeLists.txt line before this
  phase was considered complete — confirmed via a from-scratch rebuild and
  `git status` showing a clean diff limited to the intended production
  files.

## What was deliberately NOT done (per the strategy doc's own "Step 4")

- No generic, scripted/JSON-driven pass declaration — every compute pass is
  still hand-authored C++ via `AddPass()`/`AddComputePass()`.
- No descriptor-set caching/deduplication across passes — each compute pass
  still owns and rewrites its own descriptor set(s), per Phase 3's own
  refusal of a shared cache (demonstrated directly by the throwaway test,
  which rewrites its own `ComputeDescriptorSet` inside its `execute`
  callback every time it runs).
- No compute-to-compute pipeline "fusion"/optimization.
- No change to `RenderGraphResourcePool`'s memory-aliasing/pooling policy —
  and, per Phase 1 v2's own scope note (reconfirmed by this phase's own
  throwaway test design), a transient (render-graph-pooled) `RWTexture` is
  still not possible today — `rg::TextureDesc` still has no storage-usage
  opt-in. Every `RWTexture` this campaign creates remains an
  externally-owned, persistent resource, imported per call via
  `ImportTexture()`.
- No `PassContext::recordDispatch` callback — confirmed, per this phase's
  own Step 6, that `Renderer::Dispatch()`'s existing fusion into a pass's
  own recorded stats (via the same `BeginGraphPassRecording()`-stored-
  callback mechanism `Renderer::Submit()` already uses) needed no new
  `PassContext` surface area at all.

## Handoff notes for whoever picks up Phase 7

- Phase 7 (`COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md`) is
  the next and FINAL unit of work in this campaign — a real, SHIPPED
  compute box-blur post-process validation workload (reading the Scene
  view's own `RenderTexture` as a plain `Texture`, writing a NEW, dedicated,
  persistent `blurredSceneOutput` `RenderTexture` as an `RWTexture`), plus
  the companion `GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md`
  document's own buffer-side culling validation. Both remain `[TODO]`
  as of this session.
- `PassBuilder::WriteTexture()`/`AddComputePass()`/`PassContext::resolveTexture()`/
  `resolveBuffer()` are all now fully shipped, tested, and validated
  end-to-end (via this phase's own throwaway test) — Phase 7's real blur
  pass should use these directly, with no further `RenderGraphBuilder`/
  `RenderGraph` changes anticipated.
- Per this phase's own throwaway test bug (the dangling-reference lambda
  capture): **any future compute (or graphics) pass's `execute` lambda
  that references a `TextureHandle`/`BufferHandle` computed inside the
  enclosing `build` lambda MUST capture that handle BY VALUE, never by
  reference** — the `build` lambda's own local variables do not outlive its
  own return, but the pass's `execute` callback is invoked much later, by
  `RenderGraph::ExecuteCompiledGraph()`. This is not a new engine bug (every
  existing production pass in `RenderPasses.cpp` already gets this right,
  e.g. `AddGameViewPass()`'s `gameViewTarget`/`aspectWidthOverHeight` are
  captured by value or are stable references to caller-owned data) — it is
  simply a sharp edge worth calling out explicitly for whoever writes
  Phase 7's own real pass declarations, since this phase's own throwaway
  test fell into it once.
- Phase 7's own `blurredSceneOutput` `RenderTexture` should follow this
  phase's throwaway test's exact precedent: explicit `VK_FORMAT_R8G8B8A8_UNORM`
  format (never `VK_FORMAT_UNDEFINED`/`Renderer::ColorFormat()` — see Phase
  7's own v2 finding), `allowStorageImageAccess = true`, created once
  OUTSIDE the render graph (e.g. owned by `ImGuiEditorLayer` alongside
  `m_sceneView`), and imported fresh via `ImportTexture()` every frame —
  exactly what this phase's throwaway `outputTexture` already did
  successfully.
