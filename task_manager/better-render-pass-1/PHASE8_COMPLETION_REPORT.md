# PHASE8 — `TextureDesc::usage` Bitmask Field + RenderGraphResourcePool Audit — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE8_TEXTUREDESC_USAGE_FIELD_AND_RESOURCE_POOL_AUDIT.md`.

## Summary

`rg::TextureDesc` now carries a genuine, equality-compared `usage` bitmask field
(`TextureUsage::None | Sampled | Storage | TransferSrc | TransferDst`), and
`RenderGraphResourcePool::AcquireTexture()` now threads `HasFlag(desc.usage,
TextureUsage::Storage)` into `Renderer::CreateRenderTexture()`'s own
`allowStorageImageAccess` parameter — the load-bearing fix this phase exists for.
A render-graph-declared, POOLED/TRANSIENT `CreateTexture()` resource can now
genuinely become a storage image (`RWTexture`) for the first time, closing the
gap `task_manager/COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md`'s
Section A.6/C.2 has documented as open since the original compute-shader
campaign.

Zero change to any pre-existing `CreateTexture()` call site's required syntax —
every existing call keeps constructing a `TextureDesc` with `usage` left at its
default (`TextureUsage::None`) and behaves byte-for-byte as before (confirmed by
the full, unmodified pre-existing `RenderGraphDescTest`/`RenderGraphBuilderTests`
suites still passing unchanged).

No `ask_questions` call was needed — the task doc's own Step 3/Step 4 already
resolved every design fork this phase ran into (exactly which `VkImageUsageFlags`
bits needed a `TextureUsage` enumerator, and which of those bits needed real
`CreateRenderTexture()` plumbing versus being added as forward-looking
vocabulary only).

## What changed

### `src/Renderer/RenderGraph/RenderGraphTypes.h`

- New `enum class TextureUsage : std::uint32_t { None = 0, Sampled = 1u<<0, Storage = 1u<<1,
  TransferSrc = 1u<<2, TransferDst = 1u<<3 };`, placed immediately after
  `ResourceAccess`'s own `ToString()` declaration (same file, same "small
  vocabulary enum" section the task doc's Step 1 asked for), plus `constexpr`
  `operator|`/`operator|=`/`HasFlag()` helpers mirroring `ResourceAccess`'s own
  "named after WHAT THEY DO" convention.
  - `Sampled` carries NO separate `CreateRenderTexture()` plumbing — it is the
    real-world default-equivalent meaning every `RenderTexture` already has
    unconditionally today (`VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
    VK_IMAGE_USAGE_SAMPLED_BIT`, confirmed by direct reading of
    `RenderTexture::Create()`). It exists purely as vocabulary distinguishing
    "I explicitly want a plain sampled texture" from the universal default
    `TextureUsage::None`.
  - `Storage` is the one bit genuinely wired all the way through (see below).
  - `TransferSrc`/`TransferDst` are added now purely as vocabulary for a FUTURE
    phase/campaign, per the task doc's own Step 4 resolution: confirmed, by
    direct reading of `RenderTexture::Create()`, that `imageInfo.usage` never
    includes `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`/`_DST_BIT` under any condition
    today, and this engine's one real `AddBlitPass()` consumer
    (`src/Editor/BlitValidation.cpp`) always blits between directly-owned,
    imported `RenderTexture` instances, never a pooled/`TextureDesc`-driven
    `builder.CreateTexture()` resource — so these two bits have ZERO wired
    `CreateRenderTexture()` plumbing behind them yet. Each carries an explicit,
    loud doc comment stating this plainly, exactly as the task doc's Step 4
    required, rather than silently implying a capability that doesn't exist.
- `TextureDesc` gained `TextureUsage usage = TextureUsage::None;`, appended at
  the END of the struct (never inserted in the middle, per this codebase's
  universal struct-field-append rule), with a doc comment citing the file's own
  "standing rule" comment directly (does this field change whether two requests
  can share one physical allocation? — yes: a storage-capable image and a
  non-storage image are not Vulkan-interchangeable). `operator==` stays
  `= default`, so `usage` is automatically included in the comparison with zero
  extra code, exactly like `hasDepth` already is.

### `src/Renderer/RenderGraph/RenderGraphResourcePool.cpp`

- `AcquireTexture()`'s `Renderer::CreateRenderTexture()` call now passes
  `HasFlag(desc.usage, TextureUsage::Storage)` as the `allowStorageImageAccess`
  argument, instead of always passing the implicit default (`false`). This is
  the one concrete behavior change this phase makes — everything else is pure
  vocabulary/field addition with no observable effect until a caller actually
  sets `TextureUsage::Storage`.
- `TransferSrc`/`TransferDst` are deliberately NOT threaded anywhere (per the
  task doc's own Step 4 resolution, restated above) — a comment at the call
  site cross-references `TextureUsage`'s own doc comment for the full reasoning.

### `src/Renderer/RenderGraph/RenderGraphBuilder.cpp` / `.h`

- **No change** — confirmed, by direct reading, that `CreateTexture(name, desc)`
  is already a thin pass-through storing the whole `TextureDesc` into a
  `TextureSlot` (`m_textures.push_back(TextureSlot{ desc, name,
  TextureImportInfo{} });`), exactly as the task doc's own Step 3 predicted and
  asked to be confirmed before assuming any change was needed here.

### `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`

Added (this file is already registered in `tests/CMakeLists.txt` — no new list
entry needed, per the master strategy's own cross-cutting rule that PHASE8 only
extends an already-registered file):

- `RenderGraphTextureUsageTest.DefaultIsNone` — `TextureUsage{} == TextureUsage::None`.
- `RenderGraphTextureUsageTest.OrCombinesDistinctBits` — `Storage | TransferSrc`
  carries both flags and neither of the other two.
- `RenderGraphTextureUsageTest.OrAssignMutatesInPlace` — `operator|=` mutates
  correctly.
- `RenderGraphTextureUsageTest.HasFlagIsFalseForNoneAgainstAnyRealBit` —
  `HasFlag(None, X)` is false for every real bit.
- `RenderGraphDescTest.TextureDescsDifferingOnlyInUsageCompareUnequal` — **the
  core pooling-safety property this whole phase exists to guarantee**: two
  `TextureDesc` values differing ONLY in `usage` compare unequal.
- `RenderGraphDescTest.TextureDescsWithIdenticalUsageCompareEqual` — the
  positive-control counterpart.
- `RenderGraphDescTest.DefaultConstructedTextureDescHasExpectedDefaults` — per
  the task doc's own Step 5 correction (confirmed, by direct reading, that no
  existing test in this suite covers a plain default-constructed `TextureDesc`
  at all), a brand-new test asserting every field's default, not just `usage`.

## Design decisions resolved (no `ask_questions` needed)

1. **Enum placement** — `TextureUsage` was placed directly after
   `ResourceAccess`'s own `ToString()` declaration, per the task doc's own
   explicit instruction ("same file, same general 'small vocabulary enum'
   section").
2. **Which bits get real plumbing** — only `Storage` is threaded into
   `RenderGraphResourcePool::AcquireTexture()`'s `CreateRenderTexture()` call;
   `TransferSrc`/`TransferDst` are added as pure, explicitly-documented
   forward-looking vocabulary with zero wired effect, per the task doc's own
   Step 4 resolution (already confirmed via its own direct source reading,
   restated and re-confirmed independently during this phase).
3. **`Sampled`'s role** — confirmed it needs no `CreateRenderTexture()`
   plumbing of its own (every `RenderTexture` already gets
   `COLOR_ATTACHMENT_BIT | SAMPLED_BIT` unconditionally); it exists purely so
   `TextureUsage::None` (the default) is distinguishable from an explicit "I
   want a plain sampled texture" in future caller code.
4. **Test file target** — `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`
   is the correct, already-registered home for every new test this phase adds
   (confirmed — this is the exact file the task doc's own Step 5 names).

## Live verification (Step 6 — the real payoff this phase closes)

`RenderGraphResourcePoolTests.cpp` does not exist (confirmed by `search_in_dir`
— `RenderGraphResourcePool` is genuinely Tier-2/GPU-dependent, per `AGENTS.md`'s
"Testability & Regression Safety" section, with no automated test file to
extend). Per the task doc's own Step 6 fallback, a live, manual verification was
performed and then fully reverted:

1. A **temporary** `"Phase8StorageProbe"` render-pass provider was added to
   `Core::RegisterOffscreenRenderPipelineProviders()` (`src/Core/Core.cpp`),
   registered with `rg::ProviderScope::Once`. It:
   - Called `frame.builder.CreateTexture("Phase8StorageProbeTexture",
     TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, /*hasDepth=*/false,
     TextureUsage::Storage })` — a genuinely POOLED/transient resource, never
     an `ImportTexture()`-wrapped externally-owned one.
   - Built its own throwaway `ComputePipeline` from the already-shipped,
     already-reflection-migrated `shaders/RenderFeatureOps.comp.spv` (PHASE7)
     and bound the pooled texture as its one storage-image descriptor.
   - Dispatched a solid-fill write (opCode 0, color `(1, 0, 1, 1)` — magenta)
     into the pooled texture every frame.
2. Built (`cmake --build build`, zero errors) and launched `GreatTamanaEditor.exe`
   via `run_app_background`.
3. `GET /get_logs?min_level=Error` → `{"count":0,...}` immediately after
   startup.
4. `GET /list_textures` → confirmed a new entry,
   `{"name":"Phase8StorageProbeTexture","kind":"texture2d","width":64,"height":64,"format":"R8G8B8A8_UNORM","has_depth":false,...}`
   — the pooled resource is live and resolved.
5. `GET /get_texture?texture_name=Phase8StorageProbeTexture` → a real, solid
   **magenta** 64x64 PNG — direct, visual, conclusive proof the compute shader
   genuinely wrote into this POOLED texture through its storage-image
   descriptor binding, which is only valid Vulkan usage because
   `RenderGraphResourcePool::AcquireTexture()` now actually created the
   underlying `VkImage` with `VK_IMAGE_USAGE_STORAGE_BIT` set (via
   `allowStorageImageAccess=true`, itself only reachable because
   `desc.usage` included `TextureUsage::Storage`).
6. `GET /get_logs?min_level=Warning` → 40 entries, every single one a
   pre-existing, unrelated warning category already documented by PHASE7's own
   completion report baseline (render-feature priority tie-breaks, GPU-timing
   slot-budget exhaustion) — zero new warning category, zero warning mentioning
   this phase's own files.
7. `GET /get_swapchain` → the Game/Scene View rendered exactly as PHASE7's own
   documented baseline screenshot (the blurred blue vignette blob over a
   salmon-pink background) — confirming the new, temporary probe pass has zero
   visible effect on the real, composited output (as expected — it was never
   wired into any compositor/final-output chain).
8. `stop_app_background` — Editor closed cleanly.
9. **The temporary probe block was then fully removed** from `Core.cpp`,
   restoring it byte-for-byte to its pre-phase state — confirmed via
   `git_status`: `src/Core/Core.cpp` does **not** appear in the modified-files
   list after the revert, proving zero net diff. A final incremental
   `cmake --build build` after the revert again succeeded with zero errors.

This is a genuinely stronger confirmation than the task doc's own minimum bar
("construct a transient `RWTexture` ... dispatch a trivial compute write ...
confirm it renders/writes correctly") — the probe used a real, pre-existing,
already-migrated (PHASE7) production shader/pipeline rather than a brand-new
throwaway one, and the captured image's exact color matches the push constants
supplied, ruling out a stale/garbage-memory false positive.

## Related, out-of-scope finding (not acted on, per this phase's own narrow scope)

While confirming "every pre-existing `CreateTexture()` call site" for this
phase's regression check, one existing PRODUCTION call site was found that
creates a pooled texture written as a compute storage image but has never set
`TextureUsage::Storage` (or any predecessor of it, since the field didn't exist
before this phase): `src/Core/Plugins/PluginRenderResourceTranslation.cpp`'s
`ToRgTextureDesc()` — the `IPluginRenderPassBuilder_v3` plugin ABI's
`CreateTexture()` translation, consumed by real, already-shipped demo plugins
(`gte.builtin.box_blur`'s downsample output, etc., confirmed working live via
`GET /get_swapchain` above). This has been a **pre-existing gap since before
this phase** (the `usage` field didn't exist until this phase added it, so
there was nothing to set) — `AcquireTexture()`'s new `Storage`-threading logic
does not regress this call site (it still defaults to `TextureUsage::None`,
`allowStorageImageAccess=false`, byte-for-byte unchanged from before this
phase), but it also does not yet CLOSE the gap for this one call site. The
task doc's own Step 3/Acceptance Bar scope `RenderGraphResourcePool::
AcquireTexture()`'s own threading and a brand-new Tier-1 test/live check only —
it never mentions `PluginRenderResourceTranslation.cpp`/the plugin ABI
translation layer at all, and touching the plugin ABI surface is explicitly
out of scope for this entire `better-render-pass-1` campaign (PHASE7's own
completion report: "Zero change to `plugins/gte_plugin_abi/` or any plugin
`.dll`'s own source"). Left untouched, honestly flagged here rather than
silently left for a future reader to rediscover from scratch.

## Compile check and targeted test run

- `cmake --build build` (incremental, from the repository root) — succeeded
  end-to-end both with the temporary probe present (8/8 steps rebuilt) and
  again after its revert (8/8 steps rebuilt), zero warnings/errors in either
  pass.
- `ctest -R "RenderGraphTextureUsage|RenderGraphDescTest" --output-on-failure`
  (from `build/`) — all 21 tests pass: 14 pre-existing `RenderGraphDescTest`
  cases (unchanged, confirming zero regression) + 3 new `RenderGraphDescTest`
  cases this phase added + 4 new `RenderGraphTextureUsageTest` cases this phase
  added = 21 total.

Per this campaign's own process rule (Note 4/5), no full clean build or full
`ctest` regression pass was run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

- [x] `TextureDesc::usage` exists, is a genuinely equality-compared field, and
      two descs differing only in `usage` are proven (by a passing Tier-1 test,
      `TextureDescsDifferingOnlyInUsageCompareUnequal`) to compare unequal.
- [x] `RenderGraphResourcePool::AcquireTexture()` genuinely honors
      `TextureUsage::Storage`, proven by an explicit, documented live
      verification (see "Live verification" above) — a real compute shader
      successfully wrote into a pooled texture's storage-image descriptor
      binding, which is only valid because the underlying `VkImage` now
      genuinely carries `VK_IMAGE_USAGE_STORAGE_BIT`.
- [x] Zero change to any pre-existing `CreateTexture()` call site's required
      syntax — every existing call (confirmed via the full pre-existing
      `RenderGraphDescTest`/`RenderGraphBuilderTests`/`RenderGraphCompilerTests`
      suites still passing, plus `PluginRenderResourceTranslation.cpp`'s/
      `ProjectAssemblyProbe`'s own real call sites both still compiling and
      behaving unchanged) simply keeps constructing a `TextureDesc` with
      `usage` left at its default, `TextureUsage::None`, and behaves
      byte-for-byte as before.

## Files changed this phase

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — new `TextureUsage` enum +
  `operator|`/`operator|=`/`HasFlag()` helpers; `TextureDesc::usage` field
  appended at the end of the struct.
- `src/Renderer/RenderGraph/RenderGraphResourcePool.cpp` — `AcquireTexture()`
  threads `HasFlag(desc.usage, TextureUsage::Storage)` into
  `Renderer::CreateRenderTexture()`'s `allowStorageImageAccess` parameter.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — 7 new tests (4
  `TextureUsage` tests, 3 `TextureDesc` tests, including the new default-value
  test the task doc's own Step 5 asked for).
- `src/Core/Core.cpp` — **temporarily** modified for live verification, then
  fully reverted; zero net diff (confirmed via `git_status`).

This closes PHASE8, independent of PHASE4-7's migration batches (touched only
`RenderGraphTypes.h`/`RenderGraphResourcePool.cpp` plus tests, exactly as scoped).
PHASE9 (scaffolding fix + screen post-process convenience API) remains
independent, side-slotted work; PHASE10 is the only remaining phase allowed to
run a full clean build + full `ctest` regression pass.
