# PHASE2 — Reflection-Based Compute Pipeline Creation

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Depends on `PHASE1` (`ShaderReflection.h`).

---

## Step 1 — The Goal

`GpuResourceFactory::CreateComputePipeline(shaderSpirvPath)` — called with ONLY a path — must
work in the common case: no hand-built `VkDescriptorSetLayout` vector, no hand-built
`VkPushConstantRange`. Concretely:

1. `ComputePipeline` gains the ability to build its own descriptor-set layout(s) and push-constant
   range FROM `PHASE1`'s `ReflectComputeShader()` result, when the caller does not supply them
   explicitly.
2. The existing, fully-manual constructor/call shape (explicit `descriptorSetLayouts`/
   `pushConstantRange`) is preserved, byte-for-byte, as the documented escape hatch for any exotic
   shader reflection cannot correctly express (R2's own explicit requirement).
3. `ComputePipeline` exposes three new, small accessors a migrated call site (PHASE4 onward) and
   `CommandBuffer` (PHASE3) both need: `ReflectedDescriptorSetLayout(std::uint32_t set = 0)`,
   `PushConstantSize()`, `LocalGroupSize()`.

---

## Step 2 — The Situation

`ComputePipeline`'s real constructor today (`src/Renderer/ComputePipeline.h`, confirmed):

```cpp
ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
    std::optional<VkPushConstantRange> pushConstantRange = std::nullopt);
```

`descriptorSetLayouts`/`pushConstantRange` ALREADY default to empty/`nullopt` — but today,
passing nothing means a genuinely empty-of-resources pipeline (only valid for a compute shader
using push constants alone with no buffers/images, which no real shader in this engine does). This
phase changes what "omitted" MEANS: when the caller passes nothing, the constructor reflects
`shaderSpirvPath` itself (via `PHASE1`'s `ReflectComputeShader()`) and BUILDS the layout(s)/range
from that reflection data instead of leaving them empty.

`GpuResourceFactory::CreateComputePipeline()`/`Renderer::CreateComputePipeline()` both already
forward their own `descriptorSetLayouts = {}`, `pushConstantRange = std::nullopt` defaults
straight through unchanged (`GpuResourceFactory.cpp` line ~326, `Renderer.cpp` line ~350) — this
phase's whole surface change is inside `ComputePipeline`'s own constructor body; NEITHER
`GpuResourceFactory::CreateComputePipeline()` NOR `Renderer::CreateComputePipeline()`'s own
signatures need to change at all. This is the cleanest possible shape: the exact same call,
`renderer.CreateComputePipeline("shaders/Foo.comp.spv")`, now does more than it used to, for free,
everywhere.

**Distinguishing "caller omitted" from "caller explicitly supplied" — RESOLVED, no new enum
parameter needed.** An earlier draft of this phase considered adding a new trailing
`ComputePipelineLayoutMode mode = ComputePipelineLayoutMode::Reflect` parameter (`Reflect` vs.
`Manual`). **Do NOT do this — it is actively dangerous and must be dropped.** Every one of
`PHASE0` Step 2.2's 8 files / 16 pipelines is migrated in LATER phases (PHASE4-7), not this one —
between this phase landing and PHASE7 finishing, every one of those 16 still-unmigrated call sites
keeps passing its own explicit, non-empty `descriptorSetLayouts` (and usually a real
`pushConstantRange`) exactly as it does today. None of them would ever pass a new 5th `mode`
argument (they don't know it exists), so it would silently default to `Reflect` — and if `Reflect`
mode were defined as "always re-derive from reflection, ignoring any caller-supplied
`descriptorSetLayouts`/`pushConstantRange`", EVERY SINGLE not-yet-migrated call site in the engine
would have its own explicit arguments silently discarded and overridden the moment this phase
merges, directly violating this phase's own Acceptance Bar below ("the existing, fully-manual
constructor overload/call shape still compiles and behaves byte-for-byte identically for every one
of PHASE0 Step 2.2's real call sites, none of which are migrated yet in THIS phase").
The correct, safe, and simpler design needs no new enum parameter at all — decide purely from the
VALUES already passed: **if `descriptorSetLayouts.empty() && !pushConstantRange.has_value()` (i.e.
both are left at their existing `{}`/`std::nullopt` defaults — true only for a NEW, migrated,
path-only call like `renderer.CreateComputePipeline("shaders/Foo.comp.spv")`), reflect
`shaderSpirvPath` and build the layout(s)/range from that. Otherwise (the caller supplied a
non-empty `descriptorSetLayouts` and/or a real `pushConstantRange` — true for every one of today's
16 not-yet-migrated call sites), use exactly what was supplied, verbatim, reflecting nothing — the
exact byte-for-byte today's behavior.** This is unambiguous, requires no public signature change
beyond what already exists, and is automatically, provably safe for every currently-unmigrated call
site without needing to track "was this argument explicitly passed" at all (something C++ cannot
distinguish from "happens to equal the default" anyway). The one, now-moot hypothetical this
closes off — a caller wanting genuinely ZERO descriptor sets AND zero push constants, explicitly,
while still opting out of reflection — has no real shader in this engine today (confirmed against
`PHASE0` Step 2.2's full inventory); if a future shader genuinely needs this, that is a new,
separate, later decision, not something this phase needs to solve speculatively.

`ComputePipeline` today stores only `m_device`/`m_layout`/`m_pipeline` — it does NOT retain the
reflection result (bindings/push-constant size/local size) anywhere. This phase must add storage
for at least: the resolved `VkDescriptorSetLayout` PER DECLARED SET (today's manual path already
supports MULTIPLE layouts via its `std::vector<VkDescriptorSetLayout>` parameter — a reflected
shader may also, in principle, declare bindings across more than one `set` number, though every
real shader in this engine today uses `set = 0` exclusively; build the reflection path
GENERICALLY over however many distinct `set` values `ReflectComputeShader()`'s bindings actually
contain, grouping bindings by `set` before calling `DescriptorSetLayoutBuilder` once per distinct
set — do not hardcode "always exactly one set"), the resolved push-constant size (0 if none), and
the resolved local work-group size (`{1,1,1}` if `ReflectComputeShader()` somehow returns that,
though every real `.comp` shader declares a real one).

**Who owns the newly-built `VkDescriptorSetLayout`(s) in the reflection path?** In the EXISTING
manual path, the CALLER builds and owns the layout externally (e.g. `CullingPipelines` stores its
own `m_layout` member and destroys it in its own destructor) — `ComputePipeline` itself never
owns a `VkDescriptorSetLayout` today, only borrows references to build the pipeline layout from.
In the NEW reflection path, `ComputePipeline` itself is the one building the layout (the caller
never supplied one) — so `ComputePipeline` must now OWN whichever layout(s) it builds via
reflection, and destroy them in its own destructor (`Destroy()`) — but must NOT destroy a
layout it never built (the manual path's caller-supplied layouts remain caller-owned, exactly as
today). This means `ComputePipeline` needs a new bool/flag (or simply: a
`std::vector<VkDescriptorSetLayout> m_ownedReflectedLayouts` member, non-empty ONLY in the
reflection path, destroyed in `Destroy()`; the manual path leaves this vector empty and destroys
nothing extra) distinguishing "I own these, destroy them" from "the caller owns these, I only
borrowed them to build `m_pipelineLayout`". Get this exactly right — a double-free or a leak here
is a real, silent Vulkan validation-layer-catchable bug. Confirm this ownership design via
`ask_questions` before implementing if any part of it feels ambiguous once the real header is in
front of you.

---

## Step 3 — The Plan

1. **Extend `ComputePipeline`'s constructor** (`ComputePipeline.h`/`.cpp`) — NO new parameter is
   added to its public signature at all (see Step 2's "RESOLVED" note above — the
   previously-considered `ComputePipelineLayoutMode` enum parameter must NOT be added). At the top
   of the constructor body, branch purely on the VALUES already received:
   `if (descriptorSetLayouts.empty() && !pushConstantRange.has_value())` → REFLECT (the new path,
   below); else → MANUAL (today's exact existing path, completely untouched).
   - **Reflect path** (`descriptorSetLayouts.empty() && !pushConstantRange.has_value()`):
     - Call `ReflectComputeShader(shaderSpirvPath)` (PHASE1).
     - Group `descriptorBindings` by `.set`; for each distinct set, in ascending set-number order,
       build one `VkDescriptorSetLayout` via `DescriptorSetLayoutBuilder` — one `AddStorageBuffer`/
       `AddStorageImage`/`AddCombinedImageSampler` call per reflected binding (using
       `.binding`/`.count`, always `VK_SHADER_STAGE_COMPUTE_BIT`, matching `DescriptorSetLayoutBuilder`'s
       own existing default stage). Store the resulting layout(s) in the new
       `m_ownedReflectedLayouts` vector, and pass them (as a plain `std::vector<VkDescriptorSetLayout>`,
       in set order) into the SAME `VkPipelineLayoutCreateInfo` construction the manual path already
       builds — reuse that existing code path unchanged, just feed it reflection-derived layouts
       instead of caller-supplied ones.
     - If `hasPushConstantRange`, build a real `VkPushConstantRange{ VK_SHADER_STAGE_COMPUTE_BIT,
       pushConstantRange.offset, pushConstantRange.size }` and feed it into the same
       `VkPipelineLayoutCreateInfo` path the manual path already uses for its own caller-supplied
       `pushConstantRange`.
     - Store `pushConstantRange.size` (or 0) in a new `m_pushConstantSize` member, and
       `localSize` in a new `m_localSize` (`Extent3D`, from `ComputeDispatch.h` — reuse that exact
       type, do not invent a second one) member.
     - If a reflected binding's type is anything other than the three this engine's
       `DescriptorSetLayoutBuilder` supports, `throw std::runtime_error` with a clear message naming
       the shader path and the unsupported type (mirrors `PHASE1`'s own reflection-layer error for
       the same condition — decide, via `ask_questions` if unclear, whether this check belongs in
       `ReflectComputeShader()` itself (PHASE1) or here (PHASE2); either is defensible, but it must
       live in exactly ONE of the two, not both, to avoid dead/unreachable duplicate code).
   - **Manual path** (anything else — a non-empty `descriptorSetLayouts` and/or a real
     `pushConstantRange` was supplied, covering every one of today's 16 not-yet-migrated call
     sites): behave EXACTLY as today — zero change to that code path, `m_ownedReflectedLayouts`
     stays empty, `m_pushConstantSize`/`m_localSize` are set to 0/`{1,1,1}` (since no reflection
     ran) — a manually-constructed pipeline simply has no reflected metadata to report;
     `CommandBuffer`'s own `SetPushConstants<T>()` assert (PHASE3) must treat
     `m_pushConstantSize == 0` as "nothing to check against" rather than "the struct must be zero
     bytes," so a manually-built pipeline's callers are never falsely flagged.
   - Update `Destroy()` to also destroy every layout in `m_ownedReflectedLayouts` (loop +
     `vkDestroyDescriptorSetLayout`), guarded the same way `m_layout`/`m_pipeline` already are
     (null-checked, safe to call multiple times / on a moved-from object).
   - Update the move constructor/move-assignment to also transfer `m_ownedReflectedLayouts`/
     `m_pushConstantSize`/`m_localSize` (via `std::exchange`/`std::move`, mirroring the existing
     three-member pattern exactly).
2. **Add the three new public accessors**:
   ```cpp
   VkDescriptorSetLayout ReflectedDescriptorSetLayout(std::uint32_t set = 0) const noexcept; // returns VK_NULL_HANDLE if `set` is out of range or this pipeline was built manually
   std::uint32_t PushConstantSize() const noexcept { return m_pushConstantSize; }
   Extent3D LocalGroupSize() const noexcept { return m_localSize; }
   ```
3. **No signature change needed at all** on `ComputePipeline`'s own constructor, nor on
   `GpuResourceFactory::CreateComputePipeline()`/`Renderer::CreateComputePipeline()` — all three
   keep their exact current parameter lists, forwarding verbatim exactly as today. The
   reflect-vs-manual decision lives ENTIRELY inside `ComputePipeline`'s own constructor body (Step
   1 above), so there is nothing new for either factory method to forward. PHASE7's
   `PluginRenderOperationRegistry` migration (and any future caller) reaches the reflection path
   simply by calling `CreateComputePipeline(path)` with no 2nd/3rd argument, exactly like every
   other migrated call site.
4. **Add Tier-1/Tier-2 tests.** The pure grouping-by-set logic (turning a flat
   `std::vector<ReflectedDescriptorBinding>` into per-set groups, in ascending set order) is
   genuinely Tier-1-testable in isolation — extract it as its own small, pure function (e.g.
   `GroupDescriptorBindingsBySet()`, taking/returning plain vectors, no `VkDevice` involved) and
   test it directly with hand-built `ReflectedDescriptorBinding` fixtures covering: all bindings in
   set 0 (today's real-world case), bindings split across set 0 and set 1, and an empty input.
   Building the REAL `ComputePipeline` end-to-end (a live `VkDevice` needed) is Tier 2 — verify
   manually by constructing one against `BoxBlur.comp.spv` with zero explicit arguments beyond the
   path and confirming (via a quick throwaway debug print or debugger inspection) that
   `ReflectedDescriptorSetLayout(0)`/`PushConstantSize()`/`LocalGroupSize()` report the exact same
   values `PHASE1`'s own Tier-1 test already proved `ReflectComputeShader()` returns for that same
   file — do not commit throwaway debug prints; remove them before finishing.
5. **Compile-check**, run the new/changed tests, write `PHASE2_COMPLETION_REPORT.md`, commit.

### Acceptance bar for this phase

- `renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv")` (path-only) now produces a fully
  correct, bindable pipeline with the exact same real binding/push-constant-size layout as
  `ComputeBlurValidation.cpp`'s own existing hand-built call — verified by constructing BOTH the
  reflection-path pipeline and (temporarily, for this verification only) the existing manual-path
  pipeline side by side and confirming `ReflectedDescriptorSetLayout(0)` resolves the same
  descriptor types/bindings the manual `DescriptorSetLayoutBuilder` call already does (do not ship
  this side-by-side comparison code — it is a one-time manual verification step for this phase's
  own confidence, not a permanent regression test, since `VkDescriptorSetLayout` handles are
  opaque and cannot be compared for structural equality directly at runtime without reflecting
  them again).
- The existing, fully-manual constructor overload/call shape still compiles and behaves
  byte-for-byte identically for every one of PHASE0 Step 2.2's real call sites (none of which are
  migrated yet in THIS phase — that is PHASE4 onward's job).
- No leak/double-free of any `VkDescriptorSetLayout` (confirmed by inspection of the new
  ownership logic per Step 2's own detailed reasoning, plus, if convenient, a Vulkan validation
  layer run against a live Editor session with the new reflection path exercised at least once via
  a throwaway manual smoke test).
