# PHASE8 of 8 — CAPABILITY #2: CUSTOM RENDER PASS + CAMPAIGN CLOSEOUT

Read `PHASE0_MASTER_STRATEGY.md` first (Findings B, E, F especially — this
phase resolves all three concretely).
**Depends on:** PHASE3 (folder/CMake, including Finding C's header-
propagation fix), PHASE4 (a compiled `.spv` to build a real pipeline from),
PHASE5 (runtime loader must call `GTE_RegisterProject` with a real
`gte::Core&`).
**Blocks:** nothing (last phase). **This is the ONLY phase allowed to run a
full clean build + full `ctest` regression (LDD11)** — every earlier phase
used incremental compile checks only.

End state: `ProjectAssemblyProbe_Game.dll` contributes one real render-graph
pass (a trivial compute pass that fills a transient texture with a solid
color), using genuine `rg::TextureHandle`/`rg::RenderGraphBuilder`/
`rg::RenderPipeline` types directly, with its own freshly-compiled shader
(PHASE4's `ProbeCompute.comp`) — confirmed visible in the Editor's real
"Render Graph" panel and `GET /render_graph`. Plus this campaign's own full
regression pass, permanent `AGENTS.md` entry, and
`CAMPAIGN_COMPLETION_REPORT.md`.

**Re-verification note (2026-09-28, this phase's own deep double-check
pass):** every concrete claim, anchor, and code shape below was re-confirmed
directly against the real, current repository state via `search_in_dir`/
`read_file` — none of PHASE1-7 has actually been implemented yet (no
`Projects/` folder, no `cmake/GteProject.cmake`, no completion reports exist
anywhere under this campaign's folder), so this is a pre-implementation plan
re-check, not a post-hoc audit. Three genuinely new things were found and are
folded in below: (1) a better, more directly analogous real precedent for
`RenderGraphBuilder::CreateTexture()` than the buffer-only `"GpuSkinning"`
provider; (2) the exact real accessor chain for a live `VkDevice`/compute
descriptor set, resolving what used to be a "confirm before writing
literally" placeholder; (3) a concrete, definitive answer to Step 3's own
open question, resolved by directly reading `Core::BuildFrame()`'s real body.

## The gap this phase resolves (Finding B, concretely)

`Core::RegisterOffscreenRenderPipelineProviders()` is declared `private`
(confirmed, current `src/Core/Core.h`, inside the `private:` section
starting at line 379, the method itself at line 420). `Core` has NO existing
public method that lets an outside caller register a new provider onto
`m_offscreenRenderPipeline` (also `private`, line 478). **This is new code
this phase must add, not a matter of merely calling something that already
exists.** The fix is small and low-risk: `rg::RenderPipeline::Register()`
ITSELF is already public (confirmed, current
`src/Renderer/RenderGraph/RenderPipeline.h` line 463) — `Core` simply never
exposes a thin pass-through to it yet, mirroring EXACTLY the same "private
member, public thin pass-through" shape `Core::LoadPlugins()` already uses
for `m_pluginHost` (confirmed: `Core.h` line 295 declares the public
method, `Core.cpp` line 293 forwards to the private `m_pluginHost` member
declared at `Core.h` line 561 — PHASE5 already copied that exact shape
once for `LoadProjectAssemblies()`; copy it again here).

## STEP 1 — new public method: `Core::RegisterProjectRenderPassProvider()`

`Core.h` already `#include`s `"../Renderer/RenderGraph/RenderPipeline.h"`
(confirmed, line 24) — so `rg::ProviderScope`/`rg::RenderPassProvider` are
already visible with zero new `#include` needed. Add, in the public section,
near `LoadPlugins()` (line 295):

```cpp
// editor-core-separation-11 campaign (Project Assembly system), PHASE8
// (Finding B). Core::RegisterOffscreenRenderPipelineProviders() itself
// stays PRIVATE and unmodified - this is a NEW, separate, public thin
// pass-through, mirroring LoadPlugins()'s own identical shape (a private
// m_pluginHost member, a public one-line forwarding method). A Project
// Assembly _Game.dll calls this directly, from its own GTE_RegisterProject
// entry point, to contribute a real render-graph provider using the exact
// same rg::RenderPipeline::Register() every internal engine pass already
// goes through - no ABI wrapper, no curated operation registry (this
// system has no ABI boundary to protect, unlike gte_plugin_abi's
// IPluginRenderPassBuilder_v3).
void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider);
```

`Core.cpp`:

```cpp
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider));
}
```

**Verify concretely, do not assume:** `m_offscreenRenderPipeline` (line 478)
is the CORRECT pipeline for a Project Assembly's own general-purpose pass —
`Core.h`'s own class-level doc comment describes the "offscreen regime (Game
View + Scene View...)" vs. "present regime (the swapchain-present pass)"
split (confirmed, `Core.h` lines 107/165); re-read it before trusting this,
since picking the wrong one would silently register a Project Assembly's
pass somewhere it can never actually run against the views a user looks at.
`m_offscreenRenderPipeline` is confirmed correct: it is the pipeline every
production Game-View/Scene-View pass (`"AtmosphereSharedLut"`,
`"GpuSkinning"`, `"AtmosphereViewLut"`, `"RenderOpaque"`,
`"GpuDrivenBatches"`, ...) registers onto today (`Core.cpp`,
`RegisterOffscreenRenderPipelineProviders()`, starting line 388) —
`m_presentRenderPipeline` is the separate, narrower pipeline used ONLY for
the one `"Present"` swapchain-blit provider and is the wrong target for a
general-purpose content pass.

**Timing note, confirm this too:** a Project Assembly's `GTE_RegisterProject`
runs from `EditorHost.cpp`'s constructor body (PHASE5), which is AFTER
`Core`'s own constructor (and therefore after
`RegisterOffscreenRenderPipelineProviders()`'s own internal, private
registrations) has already run. `RenderPipeline::Register()` merely appends
to an internal `std::vector` (`m_providers` — confirmed: the `push_back`
call itself is at `RenderPipeline.h` line 466, inside `Register()`; the
vector is declared as a private member further down the same class) that is
read fresh, in full, EVERY frame by `DeclareInto()` (confirmed,
`RenderPipeline.h` lines 527-531) — so registering after construction, but
before the first real frame renders, is safe and behaves identically to
registering during construction. Confirm this by testing (Step 4 below)
rather than trusting the reasoning alone.

## STEP 2 — the Project Assembly's own registration code (replaces PHASE3's
placeholder `Assets/HelloGame.cpp`)

**Finding E, resolved concretely (do not re-derive this from scratch —
confirmed directly against the real class layout, `src/Renderer/RenderGraph/RenderGraphBuilder.h`):**
`RenderGraphBuilder::PassBuilder` (the type `RenderPassDesc::setup`
receives) has NO `CreateTexture()`/`ImportTexture()` method — only
`ReadTexture()`/`WriteColorAttachment()`/`WriteDepthStencilAttachment()`/
`WriteTexture()`/`ReadBuffer()`/`WriteBuffer()`/`ReadVolumeTexture()`/
`WriteVolumeTexture()`, all operating on an ALREADY-MINTED handle.
`CreateTexture()`/`CreateBuffer()`/`ImportTexture()` are methods of
`RenderGraphBuilder` ITSELF (confirmed, `RenderGraphBuilder.h`: `CreateTexture()`
line 303, `CreateBuffer()` line 304, `ImportTexture()` line 326). The correct
pattern: call `frame.builder.CreateTexture(...)` directly inside the
provider's own lambda body (available because `RenderPassFrameContext::builder`,
`RenderPipeline.h` line 364, is a plain reference member, freely callable
through a `const RenderPassFrameContext&`), obtain the resulting
`TextureHandle` BEFORE constructing the `RenderPassDesc`, then reference
that already-minted handle from `desc.setup` via `pass.WriteTexture(handle)`.

**A better, more directly analogous real precedent than "GpuSkinning" was
found during this re-check, and should be read alongside it.** The
`"GpuSkinning"` provider (`Core.cpp`, confirmed lines 411-455) is real and
working, but it never calls `CreateTexture()` at all — it only imports an
already-live external buffer (`b.ImportBuffer(...)`, called once per frame
in `BuildFrame()` itself, BEFORE `DeclareInto()` runs) and writes it; it is
the correct precedent for the deferred `RenderPassDesc`/`setup`/`execute`
SHAPE, but not for "minting a brand-new transient resource inside a
provider". A `search_in_dir` sweep for the literal text `.CreateTexture(`
across the ENTIRE `src/` tree found exactly ONE real production call site
of `RenderGraphBuilder::CreateTexture()` in the whole engine (besides its
own definition): `src/Core/Plugins/PluginRenderPassBuilderAdapter_v3.cpp`,
line 102 (`const rg::TextureHandle handle = m_builder.CreateTexture(debugName, rgDesc);`,
inside `PluginRenderPassBuilderAdapter_v3::CreateTexture()`). Read that
method (and its caller, `AddGraphicsPass()`/`AddComputePass()` in the same
file) for a real, shipped, CreateTexture-then-reference-the-handle-later
pattern — with one important caveat: that code path is the `_v3` plugin
ABI's own SYNCHRONOUS, immediate `RenderGraphBuilder&` access at real
pass-declaration time (calling `m_builder.AddRenderPass()` directly, not
through `RenderPipeline`/`RenderPassDesc` at all), not a
`RenderPipeline::Register()`-style DEFERRED provider. It proves
`CreateTexture()` immediately followed by referencing the resulting handle
from that SAME call's own `setup`/`execute` callbacks is a real, already-
shipped, production-proven shape — but the deferred `RenderPassDesc`
mechanism this phase's own provider must use (per Finding E above) is still
the correct structural template to copy for the overall shape; only the
"how do I get a `TextureHandle` at all" half of the puzzle is better
illustrated by this second precedent.

**Finding F, resolved concretely:** the shader path is a plain, bare
relative string, `"project_assemblies/shaders/ProbeCompute.comp.spv"`
(PHASE4's own confirmed staged location, relative to the process's own CWD,
which is assumed == exe dir — the exact same convention every existing
internal `ComputePipeline` construction call site already uses, e.g.
`"shaders/AtmosphereTransmittanceLut.comp.spv"`).

**The "confirm the exact real accessor" placeholder this file used to leave
open is now fully resolved — do not re-investigate, use this concretely.**
`ComputePipeline`'s real constructor (`src/Renderer/ComputePipeline.h`,
confirmed, unchanged) is:

```cpp
ComputePipeline(VkDevice device, const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
    std::optional<VkPushConstantRange> pushConstantRange = std::nullopt);
```

**But no real production code in this entire codebase ever calls this
constructor directly.** A `search_in_dir` sweep for `ComputePipeline(`
across every `.cpp` file found exactly 8 real production call sites
(`PluginRenderOperationRegistry.cpp`, `ComputeBlurValidation.cpp`,
`FrameDebuggerPreviewProcessing.cpp`, `GBufferValidation.cpp`,
`AtmosphereLutRenderer.cpp` x6, `CullingPipelines.cpp`,
`GpuSkinningPipelines.cpp`, `VolumeTexturePreviewRenderer.cpp`) — every
single one, with zero exceptions, calls `Renderer::CreateComputePipeline(shaderSpirvPath, descriptorSetLayouts, pushConstantRange)`
instead (`Renderer.h` line 577, public, confirmed — its own doc comment
states the reason explicitly: "so callers never need direct access to the
VkDevice this Renderer owns internally"). `Renderer` deliberately exposes
**no public raw `VkDevice` getter at all** (confirmed by that same doc
comment, repeated verbatim for `AllocateComputeDescriptorSet()` too,
`Renderer.h` lines 566/583). `Core::GetRenderer()` (`Core.h` line 174,
public, confirmed) is the accessor that gets you from a `gte::Core&` to a
`Renderer&` in the first place. The full, confirmed, real recipe a Project
Assembly must use — mirroring `AtmosphereLutRenderer.cpp`'s own established
shape (confirmed, lines 173/181/185/189) exactly:

```cpp
Renderer& renderer = core.GetRenderer();                    // Core.h line 174, public
const VkDevice device = renderer.GetVulkanContextInfo().device; // Renderer.h line 858, public;
                                                                  // the ONE legitimate way to get a
                                                                  // raw VkDevice from a live Renderer&
                                                                  // (confirmed: AtmosphereLutRenderer's
                                                                  // own m_device field is seeded this
                                                                  // exact way, AtmosphereLutRenderer.cpp
                                                                  // line 173).
DescriptorSetLayoutBuilder layoutBuilder(device);            // src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h
const VkDescriptorSetLayout layout =
    layoutBuilder.AddStorageImage(/*binding=*/0).Build();     // binding 0 must match ProbeCompute.comp's
                                                                  // own `layout(binding = 0) image2D` (PHASE4)

g_probeComputePipeline.emplace(renderer.CreateComputePipeline(
    "project_assemblies/shaders/ProbeCompute.comp.spv",
    std::vector<VkDescriptorSetLayout>{ layout }));           // returns ComputePipeline BY VALUE - store
                                                                  // in std::optional<gte::ComputePipeline>,
                                                                  // never std::unique_ptr, matching every
                                                                  // one of the 8 real call sites above
                                                                  // (e.g. AtmosphereLutRenderer.cpp line 185:
                                                                  // "m_transmittanceLutPipeline.emplace(...)").

g_probeDescriptorSet = ComputeDescriptorSet(
    renderer.AllocateComputeDescriptorSet(layout));           // Renderer.h line 593, public
```

`desc.execute`'s own lambda must capture `&renderer` separately (it is a
SEPARATE lambda from the `if (!g_probeComputePipeline)` block above — do not
assume the same locals are still in scope), then call
`ctx.resolveTexture(outputHandle)` (`RenderGraph.h` line 633 — confirmed to
work for a WRITE-only handle too, not just a read one) to get the CURRENT
`VkImageView` behind that handle, rewrite `g_probeDescriptorSet` against it,
and dispatch — see the full, complete example below for the exact shape
(mirrors `"GpuSkinning"`'s own `Dispatch()` call shape, `Core.cpp` lines
446-447, for the real signature).

**Confirm the exact real `TextureDesc` fields before writing this file** —
resolved concretely (`src/Renderer/RenderGraph/RenderGraphTypes.h`, confirmed
line 278): `TextureDesc` has ONLY THREE fields, `width`/`height`
(`std::uint32_t`), `format` (`VkFormat`, `VK_FORMAT_UNDEFINED` == "match
`Renderer::ColorFormat()` exactly" — the same convention
`Renderer::CreateRenderTexture()` already uses), and `hasDepth` (`bool`,
whether this resource also carries a companion depth buffer) — there is NO
`extent`/`usage` field on this struct at all (unlike `BufferDesc`, which
does have a `usage` field); do not guess those names. A minimal, concrete,
working example for this probe pass:

```cpp
// editor-core-separation-11 campaign (Project Assembly system), PHASE8 - a
// real render-graph compute pass contributed entirely from a Project
// Assembly's own _Game.dll, using genuine rg:: types directly (no ABI
// wrapper) - the concrete resolution of Finding B/E/F
// (PHASE0_MASTER_STRATEGY.md).
#include "../Libraries/ProjectAssemblyExports.h"
#include "../../../src/Core/Core.h"
#include "../../../src/Core/Logging.h"
#include "../../../src/Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../../src/Renderer/RenderGraph/RenderGraphTypes.h"
#include "../../../src/Renderer/RenderGraph/RenderPipeline.h"
#include "../../../src/Renderer/ComputePipeline.h"
#include "../../../src/Renderer/ComputeDescriptorSet.h"
#include "../../../src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <optional>

namespace {

// Lazily constructed on first use (needs a live VkDevice, not available at
// static-init time) - a process-lifetime instance is correct here (LDD4 -
// no hot reload, so this never needs to be rebuilt/destroyed mid-run).
// std::optional, NOT std::unique_ptr - matches every one of the 8 real
// ComputePipeline call sites in this codebase (see this file's own
// resolved-accessor section above), all of which store the BY-VALUE return
// of Renderer::CreateComputePipeline() this exact way.
std::optional<gte::ComputePipeline> g_probeComputePipeline;
VkDescriptorSetLayout g_probeDescriptorSetLayout = VK_NULL_HANDLE;
gte::ComputeDescriptorSet g_probeDescriptorSet;

void RegisterProbeGame(gte::Core& core) {
    core.RegisterProjectRenderPassProvider(
        "ProjectAssemblyProbe.FillTexture",
        gte::rg::ProviderScope::Once,
        [&core](const gte::rg::RenderPassFrameContext& frame, std::vector<gte::rg::RenderPassDesc>& outPasses) {
            // Finding E: CreateTexture() lives on RenderGraphBuilder itself,
            // never on the PassBuilder a deferred desc.setup receives -
            // mint the handle HERE, before building the RenderPassDesc.
            gte::rg::TextureDesc texDesc;
            texDesc.width = 256;
            texDesc.height = 256;
            texDesc.format = VK_FORMAT_UNDEFINED; // matches Renderer::ColorFormat()
            texDesc.hasDepth = false;
            const gte::rg::TextureHandle outputHandle =
                frame.builder.CreateTexture("ProjectAssemblyProbe.Output", texDesc);

            gte::Renderer& renderer = core.GetRenderer();
            if (!g_probeComputePipeline) {
                const VkDevice device = renderer.GetVulkanContextInfo().device;
                gte::DescriptorSetLayoutBuilder layoutBuilder(device);
                g_probeDescriptorSetLayout = layoutBuilder.AddStorageImage(/*binding=*/0).Build();
                g_probeComputePipeline.emplace(renderer.CreateComputePipeline(
                    "project_assemblies/shaders/ProbeCompute.comp.spv",
                    std::vector<VkDescriptorSetLayout>{ g_probeDescriptorSetLayout }));
                g_probeDescriptorSet =
                    gte::ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(g_probeDescriptorSetLayout));
                GTE_LOG_INFO("ProjectAssemblyProbe", "ProbeCompute pipeline/descriptor set built.");
            }

            gte::rg::RenderPassDesc desc;
            desc.debugName = "ProjectAssemblyProbe.FillTexture";
            desc.kind = gte::rg::PassKind::Compute;
            desc.setup = [outputHandle](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteTexture(outputHandle);
            };
            desc.execute = [&renderer, outputHandle](gte::rg::PassContext& ctx) {
                const gte::rg::PassContext::ResolvedTexture resolved = ctx.resolveTexture(outputHandle);
                g_probeDescriptorSet.Rewrite(renderer.GetVulkanContextInfo().device,
                    { gte::ComputeDescriptorWrite::StorageImage(/*binding=*/0, resolved.view) });

                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // groupCount math: mirror ComputeDispatch.h's ComputeGroupCount() convention
                // (never plain integer division) once ProbeCompute.comp's own local_size is known (PHASE4).
                renderer.Dispatch(*g_probeComputePipeline, g_probeDescriptorSet.Native(), nullptr, 0,
                    /*groupCountX=*/16, /*groupCountY=*/16, /*groupCountZ=*/1);
                renderer.EndGraphPassRecording();
            };
            outPasses.push_back(std::move(desc));

            frame.finalTextureOutputs.push_back(outputHandle); // keeps this
                // pass reachable from RenderGraphCompiler's own backward-
                // reachability culling scan (RenderPipeline.h's own
                // finalTextureOutputs field doc comment, confirmed real,
                // render-pass-3 campaign, PHASE1/PHASE3) - a transient texture
                // nobody else reads would otherwise be silently culled as dead.
        });
}

} // namespace

GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProbeGame)
```

**This code sketch is now concrete enough to implement close to verbatim —
the only remaining "confirm before finalizing" item is `ProbeCompute.comp`'s
own actual `local_size_x/y/z` (PHASE4), which determines the real
`groupCountX`/`groupCountY` values above (use `ComputeDispatch.h`'s
`ComputeGroupCount()` helper, never hand-computed division, mirroring every
other real compute dispatch in this codebase).** `RenderPassDesc::id`
(`RenderPassId`) is deliberately left unset above — confirmed, by reading
`RenderPipeline::DeclareOnePhase()`'s real flush code (`RenderPipeline.h`
lines 569-599), that this field is NEVER read anywhere in the deferred
`RenderPassDesc` -> `AddRenderPass()` translation path (it only matters for
`RenderPassBlackboard::Publish()`/`Fetch()` keys, unused by this probe pass).

## STEP 3 — should this pass's output reach the actual, displayed Game View?
(resolved concretely below — this used to be left as a pure open question)

This system's own design does not promise a compositing step the way the
`gte_plugin_abi` system's `RenderFeatureCompositor` does. It only promises
the pass becomes visible in the Editor's "Render Graph" panel and `GET
/render_graph` (Step 4 below). Because a Project Assembly has REAL, direct
access, one concrete, technically-plausible path was proposed:
`Core::GetGameViewTargetThisFrame()` (confirmed, current `Core.h` line 235,
public) — a Project Assembly's own pass could `ImportTexture()` this SAME
real `RenderTexture*` and write into it directly, achieving on-screen output
with no compositor needed.

**Part (a) — is the pointer already valid by the time a provider runs?
CONFIRMED YES, definitively, by reading `Core::BuildFrame()`'s real body
(`Core.cpp`):** `m_gameTargetThisFrame = gameTarget;` (line 883) executes
UNCONDITIONALLY, near the very top of `BuildFrame()`, strictly BEFORE
`m_offscreenRenderPipeline.DeclareInto(b, frame);` (line 1050) — both inside
the SAME `BuildFrame()` call, same frame. So `GetGameViewTargetThisFrame()`'s
returned pointer is already fully resolved and stable by the time ANY
registered provider (including a Project Assembly's own) runs during that
same frame's `DeclareInto()`.

**Part (b) — does a second `ImportTexture()` of the SAME physical
`RenderTexture*` a separate pass ALSO writes produce a correct, race-free
result? CONFIRMED NO, this is NOT safe as this system exists today — a real,
structural gap, not merely "unverified":** `RenderGraph::EnsureTextureResolved()`
(`RenderGraph.cpp`, confirmed lines 34-88) resolves and tracks resource
state **per `TextureHandle`, never per underlying physical resource**. Two
INDEPENDENT `ImportTexture()` calls against the exact same physical
`RenderTarget`/`VkImage` (the engine's own internal chain already imports
`GetGameViewTargetThisFrame()`'s target once, as its own `"GameView"` handle,
`Core.cpp` lines 926-927, EARLIER in the SAME `DeclareInto()` call than any
Project Assembly provider could run) mint TWO completely separate
`PhysicalTexture` tracking slots, each independently seeded from whatever
`currentLayout` its own `ImportTexture()` call supplied. `RenderGraphCompiler`'s
dependency-graph construction (the RAW/WAW edges the barrier planner relies
on) is built purely from `TextureHandle` IDENTITY — there is no mechanism
anywhere in this codebase today that tells the compiler "this newly-imported
handle is really the same physical resource as that OTHER already-tracked
handle". Concretely: a Project Assembly's own second import/write against
this same `RenderTarget` would get NO automatic `VkImageMemoryBarrier`
against the engine's own internal Game-View-compositing chain's reads/
writes of the identical physical image — `RenderPassEvent`-based ordering
(`desc.order`) only affects recorded-command SEQUENCE inside one command
buffer, never the actual cross-command-buffer/cross-pipeline-stage memory
visibility a genuine read/write hazard on the SAME image needs, exactly the
class of bug `ProviderTiming`'s own doc comment (`RenderPipeline.h` lines
437-448) already documents this engine having hit once before (the
`"AtmosphereComposite"` ordering bug — though that one was a same-handle
declaration-order/culling bug, a different, narrower failure mode than the
"two independent handles, same physical image, zero tracked relationship"
gap described here).

**Conclusion, stated as fact rather than left open:** do NOT attempt Step
3's on-screen-compositing idea in this phase. It is not merely uninvestigated
— it is a confirmed, real correctness hazard given how `RenderGraph`
currently tracks resource state, and making it genuinely safe would require
new, separate engine-level work (e.g. a way for `RenderGraphBuilder` to
alias one handle onto another's already-tracked physical resource, which
does not exist anywhere in this codebase today). **The honest, correct
Definition of Done for this phase is "the pass is real and visible in the
Render Graph panel/HTTP endpoint" ONLY** — record this resolution explicitly
in completion notes (Step 5 below) as a confirmed, deliberately-deferred
follow-up item for a future campaign, not an oversight.

## STEP 4 — verify, concretely

1. Build `ProjectAssemblyProbe_Game` (`cmake --build build --target
   ProjectAssemblyProbe_Game`).
2. `run_app_background` → `build/GreatTamanaEditor.exe`.
3. `gte_send_request` → `GET /render_graph` (confirmed real endpoint,
   `NetworkServer.cpp` line 204, no query parameters) — confirm a pass named
   `"ProjectAssemblyProbe.FillTexture"` appears in the returned JSON, every
   frame (poll it 2-3 times to confirm it is stable, not a one-frame fluke).
4. `gte_send_request` → `GET /activate_tab?name=Render%20Graph` (confirmed
   real endpoint + query parameter, `NetworkServer.cpp` line 343: `GET
   /activate_tab?name=<PanelName>`; `"Render Graph"` confirmed as the real
   panel name) then `GET /get_swapchain` (confirmed real endpoint,
   `NetworkServer.cpp` line 313) — visually confirm the pass row appears in
   the Editor's real "Render Graph" panel too (both surfaces should agree —
   if they disagree, that is itself a real finding worth recording).
5. Per Step 3's now-resolved conclusion, do NOT attempt to verify on-screen
   Game View output for this pass — it was confirmed NOT safe to implement
   in this phase (Step 3 above). Instead, use `GET /get_logs` (confirmed
   real endpoint, `NetworkServer.cpp` line 1179) to confirm zero unexpected
   warnings/errors across a few polled frames while the pass runs.
6. `stop_app_background`.

## STEP 5 — CAMPAIGN CLOSEOUT (this phase only)

1. **Full clean build.** Reconfigure (`cmake -S . -B build`) and
   `cmake --build build` with no target filter — every source file in the
   project, from a genuinely clean state if practical (or at minimum, every
   target this campaign touched/added, explicitly named).
2. **Full regression test suite:**
   ```
   cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build
   ctest -C Debug --output-on-failure
   ```
   Compare the pass count against the most recent prior campaign's own
   documented baseline (`editor-core-separation-10`'s
   `CAMPAIGN_COMPLETION_REPORT.md`, or whichever campaign under
   `task_manager/` most recently ran a full suite) — any newly-failing test
   is a real regression to diagnose and fix (see PHASE0 for how this
   campaign's own final iteration delegates such a fix), never something to
   silently loosen.
3. **Finalize the `AGENTS.md` entry** (a new section, mirroring the exact
   density/style of the existing "Plugin Architecture"/"Render Pass System"
   sections) — include, verbatim, PHASE1's own toolchain-switch-already-done
   disclosure (PHASE0 §2.2) so a future reader is never misled by the many
   OLDER campaigns' own honest "no actual switch happened" caveats, plus a
   summary of the whole system (folder layout, `GTE_ENABLE_PROJECT_ASSEMBLIES`,
   the two capabilities proven, the explicit Non-Goals, and Step 3's own
   confirmed on-screen-compositing hazard as an explicit, honest open item
   for future work).
4. **New file: `docs/conventions/project-assembly-system.md`** — the full
   convention write-up `AGENTS.md`'s new section links to, mirroring every
   other `docs/conventions/*.md` file's own shape.
5. **`CAMPAIGN_COMPLETION_REPORT.md`** in this same `task_manager/editor-core-separation-11/`
   folder — what was actually built across all 8 phases, every real
   deviation from these `.md` files' own instructions (and why), every new
   gap found the way Findings A-F were found, the full regression numbers,
   and an explicit, honest statement of what remains open (Step 3's
   confirmed-unsafe on-screen-compositing idea, restated plainly as
   deliberately deferred future work, not silently dropped).

## Definition of Done

- [ ] `Core::RegisterProjectRenderPassProvider()` exists, is public,
      forwards to `m_offscreenRenderPipeline.Register()` (confirmed correct
      pipeline).
- [ ] `ProjectAssemblyProbe_Game.dll` registers a real pass using this new
      method, using genuine `rg::` types directly, with the `CreateTexture()`/
      `finalTextureOutputs` pattern resolved per Finding E.
- [ ] The pass is confirmed visible in BOTH the Editor's "Render Graph"
      panel AND `GET /render_graph`'s JSON output.
- [ ] Completion notes state explicitly that Step 3's on-screen-compositing
      idea was investigated and found NOT safe to implement in this phase
      (a confirmed structural gap in per-handle resource tracking, not
      merely an untried idea), and record it as deliberately deferred future
      work.
- [ ] Full clean build + full `ctest` regression pass, zero unexplained
      regressions versus the most recent prior baseline.
- [ ] `AGENTS.md` entry, `docs/conventions/project-assembly-system.md`, and
      `CAMPAIGN_COMPLETION_REPORT.md` all exist and are internally
      consistent with each other and with every PHASE1-7 completion report.

## What this phase does NOT do

- Does NOT modify `plugins/gte_plugin_abi/`'s own `IPluginRenderPassBuilder_v3`
  system, `RenderFeatureCompositor`, or any other ABI-side render machinery
  (LDD1) — this is a fully separate, parallel path through real engine
  types.
- Does NOT build a curated "operation registry" of any kind — a Project
  Assembly's own shader is genuinely its own, compiled by PHASE4's
  mechanism, with no host-curation step at all.
- Does NOT attempt real, on-screen Game View compositing for this probe
  pass — confirmed, concretely, to be structurally unsafe as this system
  exists today (Step 3 above), not merely out of scope by choice.
- Does NOT resolve every possible future render-graph capability (reading
  `SceneDepth`, multiple render targets, cross-Project-Assembly-pass
  chaining, a safe way to alias an imported handle onto an already-tracked
  physical resource, ...) — this phase proves ONE minimal, concrete, working
  example end-to-end; broadening it further is real, separate, future work.
