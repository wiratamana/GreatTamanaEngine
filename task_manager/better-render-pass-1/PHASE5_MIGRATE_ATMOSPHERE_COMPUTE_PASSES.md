# PHASE5 — Migrate Atmosphere Compute Passes (Migration Batch 2)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE4` (proven migration
pattern + destructor-fix discipline).

---

## Step 1 — The Goal

Migrate all six compute pipelines in `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` onto
reflection-based pipeline creation + `CommandBuffer`, with **zero visual/behavioral change**, and
**without touching `.Rewrite()`/`ComputeDescriptorSet` calls at all** (per-frame descriptor
rewriting is Milestone 2/bindless territory — explicitly out of scope for this whole campaign,
see `PHASE0`'s "What This Campaign Does NOT Do"). This phase migrates ONLY: (a) pipeline
CONSTRUCTION (`EnsureXxxInitialized()` methods), and (b) the dispatch BRACKET
(`BeginGraphPassRecording`/`Dispatch`/`EndGraphPassRecording` → `ctx.Cmd()...`).

The six passes (all inside `AtmosphereLutRenderer.cpp`, confirmed call-site line numbers as of
this writing — always re-read the live file, these WILL have shifted):

1. `EnsureTransmittanceLutInitialized()` / `AddTransmittanceLutPass()` (~line 168-295).
2. `EnsureMultiScatteringLutInitialized()` / `AddMultiScatteringLutPass()` (~line 297+).
3. `EnsureSkyViewLutInitialized()` / `AddSkyViewLutPass()` (~line 441+).
4. `EnsureAerialPerspectiveVolumeInitialized()` / `AddAerialPerspectiveVolumePass()` (~line 589+).
5. `EnsureAerialPerspectiveCompositeInitialized()` / `AddAerialPerspectiveCompositePass()` (~line 730+).
6. `EnsureAerialPerspectiveVolumeDebugSliceInitialized()` /
   `AddAerialPerspectiveVolumeDebugSlicePass()` (~line 941+) — the ONE of the six that is NOT on
   the real per-frame path (only fires from the Editor's "Atmosphere" panel's "Inspect Aerial
   Perspective LUT" button) — still migrate it identically; it is exactly the same shape.

---

## Step 2 — The Situation (the exact pattern to repeat six times, confirmed live)

### 2.1 Today's `EnsureTransmittanceLutInitialized()` (real, confirmed source, lightly excerpted)

```cpp
DescriptorSetLayoutBuilder layoutBuilder(m_device);
m_transmittanceLutDescriptorSetLayout =
    layoutBuilder.AddStorageBuffer(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

m_transmittanceLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereTransmittanceLut.comp.spv",
    std::vector<VkDescriptorSetLayout>{ m_transmittanceLutDescriptorSetLayout }));
```

Note: this one has NO push-constant range at all (the 2-argument call omits it, relying on the
existing `std::nullopt` default) — confirm which of the six passes do/don't use push constants
individually (do not assume all six match) — `ReflectComputeShader()`/PHASE2's reflection path
already handles "no push-constant block declared" correctly (`hasPushConstantRange == false`),
so this requires no special-casing in the migration itself, just awareness while reviewing.

### 2.2 Today's real dispatch call site (inside the pass's `execute` lambda)

```cpp
m_transmittanceLutDescriptorSet.Rewrite(m_device,
    std::vector<ComputeDescriptorWrite>{
        ComputeDescriptorWrite::StorageBuffer(0, m_atmosphereParametersBuffer->Native()),
        ComputeDescriptorWrite::StorageImage(1, dest.view),
    });

const Extent3D groupCounts = ComputeGroupCount3D(
    Extent3D{ kTransmittanceLutWidth, kTransmittanceLutHeight, 1 },
    Extent3D{ kTransmittanceLutLocalSizeX, kTransmittanceLutLocalSizeY, 1 }); // hand-restated constants

renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
renderer.Dispatch(*m_transmittanceLutPipeline, m_transmittanceLutDescriptorSet.Native(), nullptr, 0,
    groupCounts.width, groupCounts.height, groupCounts.depth);
renderer.EndGraphPassRecording();
```

### 2.3 The migrated shape (repeat for all six)

```cpp
// EnsureXxxInitialized():
m_transmittanceLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereTransmittanceLut.comp.spv"));
m_transmittanceLutDescriptorSetLayout = m_transmittanceLutPipeline->ReflectedDescriptorSetLayout(0);
// ... m_transmittanceLutDescriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(
//         m_transmittanceLutDescriptorSetLayout)); - UNCHANGED, still needed, still built from the
//     (now-reflected, instead of hand-built) layout.

// execute lambda - .Rewrite() call stays EXACTLY as today (out of scope for this campaign):
m_transmittanceLutDescriptorSet.Rewrite(m_device, /* unchanged */);

auto cmd = ctx.Cmd();
cmd.BindComputePipeline(*m_transmittanceLutPipeline);
cmd.BindDescriptorSet(m_transmittanceLutDescriptorSet.Native());
cmd.DispatchOverSize(kTransmittanceLutWidth, kTransmittanceLutHeight, 1); // uses the pipeline's OWN reflected LocalGroupSize() - no more hand-restated kTransmittanceLutLocalSizeX/Y
```

Confirm, for EACH of the six passes individually, whether that pass has a push-constant block —
if it does, insert `cmd.SetPushConstants(theRealPushConstantStruct);` in the right place (matching
whatever that pass's own `execute` lambda already builds as its push-constant payload today, if
any — some of the six may have none at all; read each one, do not assume).

**Delete every now-dead hand-restated local-size constant** (`kTransmittanceLutLocalSizeX/Y`,
and its five siblings across the other passes) ONLY once its last real reference is migrated —
confirm via `search_in_dir` each one has zero remaining references before deleting.

**Apply PHASE4's destructor-ownership fix identically, six times**: every one of
`~AtmosphereLutRenderer()`'s six `vkDestroyDescriptorSetLayout()` calls (one per pass, for
`m_transmittanceLutDescriptorSetLayout`/etc.) must be DELETED — `ComputePipeline` now owns each of
these layouts (per PHASE2's ownership design), not `AtmosphereLutRenderer` itself. Locate
`~AtmosphereLutRenderer()`'s real body before editing (search the file — it was not directly
quoted above) and confirm exactly how many `vkDestroyDescriptorSetLayout()` calls exist there
today (expect six, one per pass — verify, do not assume the count).

---

## Step 3 — The Plan

1. Migrate all six `EnsureXxxInitialized()` methods per Step 2.3.
2. Migrate all six dispatch call sites (inside each pass's `execute` lambda) per Step 2.3.
3. Fix `~AtmosphereLutRenderer()`'s destructor (delete all six now-wrong
   `vkDestroyDescriptorSetLayout()` calls).
4. Delete every now-dead hand-restated local-size constant (confirmed unreferenced first).
5. Remove `#include "../Vulkan/DescriptorSetLayoutBuilder.h"` from `AtmosphereLutRenderer.cpp` if
   nothing else in the file still needs it after this migration (confirm by re-reading the file).
6. Compile-check (incremental).
7. **Live visual verification** — this is production, always-on-for-most-of-these-six code, so
   verify thoroughly: `run_app_background` the Editor, use `gte_send_request` to capture the Game
   View's sky/atmosphere rendering before and after (open the "Atmosphere" Editor panel, if one
   exists, and sweep the sun-angle slider or equivalent parameter to confirm the Transmittance/
   Multi-Scattering/Sky-View LUTs and Aerial Perspective volume/composite all still respond
   correctly), and separately click the "Inspect Aerial Perspective LUT" button (or its HTTP
   equivalent, if one exists) to confirm the sixth, Debug-Slice-only pass still works too. Use
   `GET /get_logs` to confirm zero new warnings/errors. `stop_app_background` when done.
8. Write `PHASE5_COMPLETION_REPORT.md` (list all six passes' before/after diffs, confirm the
   destructor fix was applied to all six, confirm the live verification), commit.

### Acceptance bar for this phase

- All six Atmosphere compute pipelines build via path-only `CreateComputePipeline()`, zero
  hand-built `DescriptorSetLayoutBuilder`/`VkPushConstantRange` remaining anywhere in
  `AtmosphereLutRenderer.cpp`.
- `~AtmosphereLutRenderer()` no longer destroys any `VkDescriptorSetLayout` it does not own.
- `.Rewrite()`/`ComputeDescriptorSet` usage is completely untouched — confirmed by `git diff`
  showing no change to any `.Rewrite(` call site's own arguments.
- Live, HTTP-verified: the sky/atmosphere still renders identically across a sun-angle sweep, and
  the Aerial Perspective debug-slice inspection button still works, zero new log warnings/errors.
