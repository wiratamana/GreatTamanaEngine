#pragma once

// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md) - the REAL, host-owned,
// growable, string-keyed PluginRenderOperationRegistry (Locked Architecture
// Decision #11, PHASE0_MASTER_STRATEGY.md - correcting the source Design
// Doc's own under-specified §4.3 `ComputeOpInfo{id,opCode,maxParamBytes}`-
// only shape, which silently assumes every registered operation shares ONE
// fixed descriptor-set layout/pipeline - see PHASE0_MASTER_STRATEGY.md Step
// 2.3, Correction #1, for the full reasoning). Every registered operation
// therefore owns its OWN pipeline/descriptor-set-layout/ordered slot table -
// `gte.builtin.box_blur` (a genuinely NEW operation this phase adds, reusing
// the already-shipped src/Shaders/BoxBlur.comp verbatim) needs a completely
// different binding shape (a CombinedImageSampler + a StorageImage) than the
// 3 pre-existing `gte.builtin.solid_fill`/`radial_vignette`/`color_grade`
// operations (one shared StorageImage binding, differing only by `opCode`),
// and this registry's own shape is what makes both live side by side with
// zero `IPluginRenderPassBuilder_v3` interface change - the concrete,
// load-bearing proof of the Design Doc's R13 central claim.
//
// This is ALSO the new PERMANENT home of the two pipelines
// RenderFeatureCompositor used to own directly
// (m_opsPipeline/m_opsDescriptorSetLayout/m_blendPipeline/
// m_blendDescriptorSetLayout, plus RenderFeatureOpsPushConstants/
// RenderFeatureBlendPushConstants themselves) - a pure ownership migration,
// zero `_v2` behavior change (this phase's own Step 3.2) -
// RenderFeatureCompositor::DispatchOps()/DispatchBlend() keep their exact
// own byte-for-byte behavior, just sourcing these four things through this
// registry's own small read-only accessors instead of owning them directly.

#include "../../Renderer/ComputePipeline.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/Pipeline.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class Renderer;
// editor-core-separation-9 campaign, PHASE3 - `Pipeline` (Renderer/Pipeline.h)
// is now fully #included above (no longer merely forward-declared) - this
// registry's own RegisterBlitFullscreen() constructs a real
// std::optional<Pipeline> member, which needs the complete type.

// The C++-side push-constant struct mirroring RenderFeatureOps.comp's own
// `PushConstants` GLSL block byte-for-byte (4 vec4s, 64 bytes total, no
// padding). RELOCATED here (from RenderFeatureCompositor.h) as of this
// phase's own pipeline-ownership migration - this registry is now the one
// place that builds m_opsPipeline's own VkPushConstantRange, so it is also
// the natural home for the exact struct that range's size is derived from.
// RenderFeatureCompositor.h/.cpp and PluginRenderPassBuilderAdapter_v2.cpp
// both keep reaching this struct transitively (via RenderFeatureCompositor.h's
// own #include of this header) - neither needed an #include change.
struct RenderFeatureOpsPushConstants {
    float opCodeAndPad[4] = {};    // .x = opCode (0=SolidFill, 1=RadialVignette, 2=ColorGrade)
    float colorRgba[4] = {};       // solid fill color / vignette color / tint color
    float centerAndRadius[4] = {}; // vignette: centerX, centerY, innerRadius, outerRadius
    float gradeParams[4] = {};     // color grade: brightness, contrast, saturation, tintStrength
};

// The C++-side push-constant struct mirroring RenderFeatureBlend.comp's own
// `PushConstants` GLSL block byte-for-byte (1 vec4, 16 bytes). RELOCATED
// here alongside RenderFeatureOpsPushConstants above, for the identical
// reason.
struct RenderFeatureBlendPushConstants {
    float blendModeAndPad[4] = {}; // .x = RenderFeatureBlendMode, as a float cast to int in-shader
};

// A registered operation is EITHER a compute dispatch (IPluginCommandRecorder::
// Dispatch()) OR a full-screen-triangle graphics draw
// (IPluginCommandRecorder::DrawFullscreenTriangle()) - never both.
enum class PluginRenderOpKind : std::uint8_t {
    Compute,
    DrawFullscreenTriangle,
};

// One ORDERED descriptor slot for a registered operation - slot index ==
// the real shader descriptor binding number. A plugin author never sees a
// VkDescriptorType directly - IPluginCommandRecorder::BindTexture()/
// BindBuffer() only ever take a plain slot index (see
// IPluginRenderPassBuilder_v3.h) - this table is what
// PluginRenderPassBuilderAdapter_v3::Dispatch()/DrawFullscreenTriangle()
// validates a caller's bound resource KIND against before ever touching a
// real VkDescriptorSet.
struct PluginRenderOpSlot {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bool isBuffer = false; // true only for VK_DESCRIPTOR_TYPE_STORAGE_BUFFER slots.
};

// One registered operation's full shape - see this file's own header
// comment (Locked Architecture Decision #11) for why this is NOT the Design
// Doc's own {id,opCode,maxParamBytes}-only shape.
struct PluginRenderOpInfo {
    std::string id;
    PluginRenderOpKind kind = PluginRenderOpKind::Compute;
    std::vector<PluginRenderOpSlot> slots; // ORDERED - index == descriptor binding number.
    std::uint32_t opCode = 0;              // meaningful only for entries sharing the shared "uber ops" pipeline.
    // The EXACT byte size this op's own VkPushConstantRange was built with -
    // Dispatch()/DrawFullscreenTriangle() reject a caller-supplied paramSize
    // that does not equal this value exactly (never merely check it against
    // the global 128-byte ceiling alone - see PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md
    // Step 3.4, Dispatch() step 3).
    std::size_t maxParamBytes = 0;

    // Compute-kind entries only:
    const ComputePipeline* computePipeline = nullptr;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    // DrawFullscreenTriangle-kind entries only (PHASE3 is the first real registrant):
    const Pipeline* graphicsPipeline = nullptr;
    // editor-core-separation-9 campaign, PHASE3 - DrawFullscreenTriangle-kind
    // entries only. `graphicsPipeline` above is built through the standard,
    // shared `Pipeline` class (Renderer/Pipeline.h), which ALWAYS declares a
    // real vertex-input binding (VertexLayout::PositionColor here) with no
    // way to opt out - so SOMETHING real must be bound at that binding
    // number before `vkCmdDraw()` or it is invalid Vulkan usage. This is a
    // real (but throwaway/never-read-by-the-shader) 3-vertex Mesh's own
    // VkBuffer, mirroring src/Editor/GBufferValidation.cpp's own identical
    // "m_dummyTriangle" workaround for the exact same underlying reason
    // (both this op's own vertex shader and GBufferValidation.vert derive a
    // full-screen triangle purely from gl_VertexIndex, never reading real
    // vertex attribute data).
    VkBuffer dummyVertexBuffer = VK_NULL_HANDLE;
};

// Host-owned, growable, string-keyed operation registry - the Design Doc's
// R13 central mechanism: adding a brand-new operation is a HOST-SIDE CONTENT
// ADDITION (register one more entry in EnsureBuiltinsRegistered() below),
// never an ABI change to IPluginRenderPassBuilder_v3 itself.
//
// One instance lives on `Core` (`Core::m_pluginRenderOperationRegistry`),
// constructed once, at Core-construction time, shared by
// RenderFeatureCompositor for the entire process lifetime - never
// reconstructed per frame, unlike PluginRenderPassBuilderAdapter_v3 (which
// IS reconstructed per plugin/per view/per frame and merely holds a
// reference to this registry).
class PluginRenderOperationRegistry {
public:
    explicit PluginRenderOperationRegistry(Renderer& renderer);

    PluginRenderOperationRegistry(const PluginRenderOperationRegistry&) = delete;
    PluginRenderOperationRegistry& operator=(const PluginRenderOperationRegistry&) = delete;
    PluginRenderOperationRegistry(PluginRenderOperationRegistry&&) = delete;
    PluginRenderOperationRegistry& operator=(PluginRenderOperationRegistry&&) = delete;

    // Lazily builds every BUILT-IN operation's pipeline/layout on first call
    // (mirrors RenderFeatureCompositor::EnsureOpsInitialized()'s own former
    // lazy-init convention) - idempotent, safe to call every frame, safe to
    // call from BOTH a _v2-only frame (via DispatchOps()/DispatchBlend()) and
    // a _v3-only frame (RenderFeatureCompositor::ContributeRenderGraphPasses()'s
    // own per-view "Seed" dispatch always calls DispatchBlend() before any
    // per-entry loop iteration, _v2 or _v3, and DispatchBlend() itself now
    // calls this method internally - so this is ALWAYS called at least once
    // before any _v3 Dispatch() call could possibly need it; no separate call
    // site is needed inside the _v3 adapter itself).
    void EnsureBuiltinsRegistered();

    const PluginRenderOpInfo* Find(const std::string& id) const noexcept;

    // Read-only access for RenderFeatureCompositor's OWN _v2 DispatchOps()/
    // DispatchBlend() to source the shared "uber ops"/blend pipeline through
    // - never used by any plugin-facing code directly. Only valid to call
    // after EnsureBuiltinsRegistered() has run at least once.
    const ComputePipeline& OpsPipeline() const noexcept;
    VkDescriptorSetLayout OpsDescriptorSetLayout() const noexcept;
    const ComputePipeline& BlendPipeline() const noexcept;
    VkDescriptorSetLayout BlendDescriptorSetLayout() const noexcept;

    // editor-core-separation-9 campaign, PHASE2 - the SAME Renderer&
    // instance this registry was constructed with - so
    // PluginRenderPassBuilderAdapter_v3's own Dispatch()/DrawFullscreenTriangle()
    // can call Renderer::BeginGraphPassRecording()/Dispatch()/
    // EndGraphPassRecording() without needing a SECOND, duplicate Renderer&
    // reference of its own (the adapter already holds a
    // PluginRenderOperationRegistry& - this is simply threaded one step
    // further). Never null (a plain reference).
    Renderer& GetRenderer() const noexcept { return m_renderer; }

    // The SAME VkDevice EnsureBuiltinsRegistered() resolved (VK_NULL_HANDLE
    // before that first call) - a small, cheap convenience accessor so
    // callers that already hold a PluginRenderOperationRegistry& never need
    // a second GetVulkanContextInfo() round-trip just to get the device
    // handle for a ComputeDescriptorSet::Rewrite() call.
    VkDevice GetDevice() const noexcept { return m_device; }

private:
    void RegisterUberOp(const char* id, std::uint32_t opCode);
    void RegisterBoxBlur();
    // editor-core-separation-9 campaign, PHASE3
    // (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md, Step 3.2) -
    // registers `gte.builtin.blit_fullscreen`, this campaign's ONE brand-new,
    // tiny, additive GRAPHICS-kind registry operation (a minimal
    // fullscreen-triangle passthrough fragment shader) - the second real
    // proof, alongside RegisterBoxBlur() above (PHASE2), that a new
    // operation lands with ZERO IPluginRenderPassBuilder_v3 interface
    // change, this time for a GRAPHICS-kind operation.
    void RegisterBlitFullscreen();

    // Shared helper both of the above call at the very end, once their own
    // PluginRenderOpInfo is otherwise fully filled in - a debug-only assert
    // that slots.size() <= 8 (the fixed scratch-array size
    // IPluginCommandRecorder::BindTexture()/BindBuffer() bind into,
    // PluginRenderPassBuilderAdapter_v3) - defensive, since nothing
    // registered this phase gets remotely close to 8 slots, but a future op
    // accidentally declaring more must fail loudly at registration time, not
    // silently overflow a fixed-size array later.
    void InsertOp(PluginRenderOpInfo info);

    Renderer& m_renderer;
    VkDevice m_device = VK_NULL_HANDLE;
    bool m_builtinsRegistered = false;

    // Shared "uber ops" pipeline (RenderFeatureOps.comp) - ONE pipeline,
    // THREE registry entries (solid_fill/radial_vignette/color_grade) all
    // pointing at it, differing only by `opCode`. Migrated here FROM
    // RenderFeatureCompositor - the exact same construction code, moved, not
    // rewritten.
    std::optional<ComputePipeline> m_opsPipeline;
    VkDescriptorSetLayout m_opsDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_blendPipeline; // RenderFeatureBlend.comp.
    VkDescriptorSetLayout m_blendDescriptorSetLayout = VK_NULL_HANDLE;

    // gte.builtin.box_blur's OWN, separate pipeline/layout - a genuinely
    // DIFFERENT ComputePipeline instance than whatever ComputeBlurValidation.cpp
    // builds for the SAME already-compiled shaders/BoxBlur.comp.spv file
    // (that class owns its own lifetime independently, for its own unrelated
    // Editor-debug-toggle purpose - confirmed by direct reading before this
    // was written).
    std::optional<ComputePipeline> m_boxBlurPipeline;
    VkDescriptorSetLayout m_boxBlurDescriptorSetLayout = VK_NULL_HANDLE;

    // editor-core-separation-9 campaign, PHASE3 - `gte.builtin.blit_fullscreen`'s
    // OWN, separate graphics Pipeline/descriptor-set-layout, plus a real (but
    // throwaway/never-read-by-the-shader) 3-vertex dummy Mesh - see
    // PluginRenderOpInfo::dummyVertexBuffer's own doc comment above for
    // exactly why the dummy Mesh is required (Pipeline's mandatory vertex
    // binding, mirroring src/Editor/GBufferValidation.cpp's own identical
    // "m_dummyTriangle" workaround). Built against a HARDCODED
    // VK_FORMAT_R8G8B8A8_UNORM color format (deliberately NOT
    // Renderer::ColorFormat()) - this op only ever draws into a `_v3`
    // plugin's own private compositing target
    // (RenderFeatureCompositor::EnsurePrivateTargetState()), which is ALWAYS
    // created at that exact fixed format (RenderFeatureCompositor.cpp's own
    // EnsureTextureSized()), never the swapchain's own negotiated
    // Renderer::ColorFormat() - mirrors ComputeBlurValidation.cpp's/
    // GBufferValidation.cpp's own identical, already-documented reasoning
    // for their own private outputs ("this texture has no pipeline-sharing
    // requirement with the swapchain/Game/Scene views at all, so there is no
    // reason to inherit the swapchain's own negotiated format"). The depth
    // format DOES use the real, negotiated Renderer::DepthFormat() (AGENTS.md's
    // "Render Target Format Matching" rule applies normally there, since
    // every RenderTexture's own companion DepthBuffer - including a `_v3`
    // plugin's own private target - is always created against that same
    // real depth format, never a fixed literal - see RenderTexture.h/
    // GpuResourceFactory::CreateRenderTexture()).
    std::optional<Pipeline> m_blitPipeline;
    VkDescriptorSetLayout m_blitDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<Mesh> m_blitDummyTriangle;

    std::unordered_map<std::string, PluginRenderOpInfo> m_ops;
};

} // namespace gte
