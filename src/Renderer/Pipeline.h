#pragma once

#include <volk.h>

#include <cstddef>
#include <span>
#include <string>

namespace gte {

// Which per-vertex GPU layout a Pipeline expects its bound Mesh's vertex
// buffer to already be in - see Vertex.h (PositionColor) and MeshVertex.h
// (PositionNormal). A Mesh itself carries no notion of "its own" vertex
// layout (see Mesh.h - it's just a VkBuffer + a count); it is entirely up to
// whoever builds a Pipeline/Mesh PAIR to make sure both agree on this, the
// same way AGENTS.md's "Render Target Format Matching" already requires a
// Pipeline and the render target it draws into to agree on their exact
// color/depth format.
enum class VertexLayout {
    // This engine's original position+color layout (Vertex.h) - every
    // built-in primitive shape (Renderer/Primitives/PrimitiveMeshGenerator.h)
    // and the original hardcoded triangle demo use this, via Triangle.vert/
    // .frag. The default, for backward compatibility with every existing
    // Renderer::CreatePipeline() call site.
    PositionColor,

    // Position+normal (MeshVertex.h) - for an imported, indexed mesh asset
    // (a *.gta AssetType::Mesh - see Game::CreateMeshEntityFromGtaFile(),
    // src/Game/Game.h/.cpp), via Shaders/Mesh.vert/.frag. Such an asset
    // carries no per-vertex color (see src/Assets/MeshData.h), only a real
    // per-vertex normal.
    PositionNormal,

    // Position+normal+UV (MeshVertex.h's MeshVertexUv) - for an imported,
    // indexed, TEXTURED mesh submesh (a per-material slice of a *.gta
    // AssetType::Mesh's triangle-index list whose material references a
    // resolvable diffuse texture - see Game::EnsureMeshAsset(),
    // src/Game/Game.h/.cpp), via Shaders/TexturedMesh.vert/.frag. Unlike
    // the other two layouts above, a Pipeline built with this layout ALSO
    // carries a descriptor-set-layout for a single combined-image-sampler
    // (set = 0, binding = 0) in its VkPipelineLayout - see
    // Pipeline's constructor `materialSetLayout` parameter and
    // GpuResourceFactory::MaterialDescriptorSetLayout() - so a per-submesh
    // MaterialTexture's own VkDescriptorSet (Renderer/MaterialTexture.h)
    // can be bound before each draw (see FrameRecorder::RecordFrame()).
    PositionNormalUv,

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE2 (task_manager/render-pass-5/
    // PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md) - same vertex
    // INPUT attributes as PositionNormal (MeshVertex.h - position+normal),
    // via Shaders/MeshInstanced.vert (reuses Shaders/Mesh.frag UNCHANGED).
    // UNLIKE every other VertexLayout, the per-draw model matrix is NOT
    // sourced from this Pipeline's push constant - MeshInstanced.vert reads
    // it from a per-instance storage buffer (set = 0, binding = 0, vertex
    // stage), indexed by gl_InstanceIndex, so ONE
    // vkCmdDrawIndexedIndirect(Count) call (Renderer::SubmitIndirect()) can
    // render N differently-positioned/oriented objects. A Pipeline built
    // with this layout carries a descriptor-set-layout for that ONE
    // readonly storage buffer in its VkPipelineLayout - see this class's
    // own `instanceBufferSetLayout` constructor parameter and
    // GpuResourceFactory::InstanceBufferDescriptorSetLayout(). Indexed
    // meshes ONLY (VkDrawIndexedIndirectCommand-only - Locked Design
    // Decision 6, PHASE0_MASTER_STRATEGY.md); never used together with
    // `materialSetLayout` on the same Pipeline (Locked Design Decision 8 -
    // textured instanced batches are out of scope this campaign).
    PositionNormalInstanced,
};

// Number of VertexLayout enumerators. Keep in sync with the enum above.
inline constexpr std::size_t kVertexLayoutCount = 4;

// PipelineOverrideSet::byLayout (SceneQuery.h) is sized off this constant and
// indexed with an unchecked operator[] in ResolveOverridePipeline() - if a
// new VertexLayout is ever added without bumping this constant, that is a
// silent out-of-bounds read, not a safely-skipped draw. Catch it at compile
// time instead.
static_assert(kVertexLayoutCount == static_cast<std::size_t>(VertexLayout::PositionNormalInstanced) + 1,
    "kVertexLayoutCount must equal the enum's last entry + 1 - add a new case and bump this constant together.");

// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE3 - the
// maximum number of color attachments a single Pipeline can be built
// against. Mirrors gte::rg::kMaxColorAttachments
// (Renderer/RenderGraph/RenderGraphTypes.h) exactly, but is kept as its own,
// independent, LOCAL constant here rather than an #include of that
// higher-level rg:: header - Pipeline is a lower-level Renderer primitive
// (constructible with no RenderGraph involved at all, e.g. the original
// hardcoded triangle demo) and should not reach upward into the RenderGraph
// layer that is built on top of Renderer/Pipeline, not the other way
// around (see AGENTS.md's "Clean Architecture" guideline). If
// gte::rg::kMaxColorAttachments is ever changed, update this value to
// match it.
inline constexpr std::size_t kPipelineMaxColorAttachments = 8;

// RAII wrapper around a VkPipeline + its VkPipelineLayout, built for dynamic
// rendering (no VkRenderPass/VkFramebuffer) against an exact color AND
// depth format. Owns both for its entire lifetime: created in the
// constructor, destroyed in the destructor. Construct via
// Renderer::CreatePipeline() rather than directly (same convention as
// Buffer/RenderTexture/Mesh) - it needs the device and the exact color/depth
// formats Renderer negotiated (see AGENTS.md, "Render Target Format
// Matching": a pipeline built for one format cannot legally draw into a
// target of a different one - depth now follows this exact same rule as
// color, via Renderer::DepthFormat()/VulkanDevice::PickDepthFormat()).
// Always depth-tests/writes (VK_COMPARE_OP_LESS, standard "smaller = closer
// to the camera" convention matching this engine's PerspectiveFovLH_ZO's
// [0,1] depth range) - every render target this pipeline draws into is
// paired with a real DepthBuffer (see DepthBuffer.h/RenderTarget.h), so real
// (non-coplanar) 3D geometry is correctly occluded instead of drawing in
// whatever order it happened to be submitted in.
//
// `vertexLayout` (see VertexLayout above) selects which vertex
// binding/attribute description this pipeline is built against - still no
// descriptor sets, and viewport/scissor left as dynamic state so the exact
// same pipeline can draw into either the swapchain or an Editor
// RenderTexture, whatever size each currently is. DOES carry one push
// constant range: a "model" matrix followed immediately by a "viewProj"
// matrix (vertex stage only, offset 0, 128 bytes total - the guaranteed
// minimum maxPushConstantsSize on every conformant Vulkan implementation,
// so this never needs a per-GPU size check) - see RenderSystem.h/
// FrameRecorder.h for how a per-draw world matrix and the active Camera's
// view-projection matrix reach this via vkCmdPushConstants, and Shaders/
// Triangle.vert/Mesh.vert for the matching `layout(push_constant)` block,
// shared identically by both vertex layouts. Grow this further (a real
// shader/material system, descriptor sets for textures/uniforms, ...) once
// there's more than these two hardcoded vertex layouts to draw.
class Pipeline {
public:
    // vertexShaderSpirvPath/fragmentShaderSpirvPath point at compiled
    // SPIR-V binaries - see cmake/CompileShaders.cmake, which compiles
    // src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at build
    // time (gitignored; only the GLSL source is version-controlled). NOT
    // paths to GLSL source.
    // `materialSetLayout` is only meaningful (non-VK_NULL_HANDLE) for
    // VertexLayout::PositionNormalUv - see that enumerator's own comment -
    // and must be GpuResourceFactory::MaterialDescriptorSetLayout() exactly,
    // so every MaterialTexture's descriptor set (allocated against that
    // exact same layout) stays binding-compatible with this Pipeline's
    // layout. Left VK_NULL_HANDLE (the default) for every other vertex
    // layout, which then builds a VkPipelineLayout with no descriptor sets
    // at all, exactly as before this parameter existed.
    // `debugName` (optional, default nullptr - stored as an empty string
    // then) is a purely COSMETIC, human-readable label describing which
    // real shader files/vertex layout this Pipeline was built with (e.g.
    // "Mesh.vert/Mesh.frag (PositionNormal)") - never folded into any
    // equality-compared/cache-key struct, mirroring RenderGraphTypes.h's
    // own explicit "a resource's human-readable name is threaded as its
    // OWN separate parameter" precedent (see task_manager/frame-debugger-3/
    // PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md). Consumed by
    // RenderSystem::Draw() when a FrameDebuggerCaptureContext is armed -
    // see DebugName() below.
    //
    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE2 - `instanceBufferSetLayout` (default VK_NULL_HANDLE) is only
    // meaningful for VertexLayout::PositionNormalInstanced - see that
    // enumerator's own comment - and must be GpuResourceFactory::
    // InstanceBufferDescriptorSetLayout() exactly. A SEPARATE, independent
    // parameter from `materialSetLayout` above (never reused/renamed) -
    // the two concepts (a material texture set, an instance-transform-
    // buffer set) are unrelated; asserted (debug builds only, see
    // Pipeline.cpp) to never both be non-VK_NULL_HANDLE at once (Locked
    // Design Decision 8, PHASE0_MASTER_STRATEGY.md - no Pipeline needs
    // both today).
    //
    // Global Scene Services Descriptor Set campaign (better-render-pass-6),
    // PHASE3 (task_manager/better-render-pass-6/PHASE3_PIPELINE_SET1_WIRING.md)
    // - `sceneServicesSetLayout` (default VK_NULL_HANDLE) is a genuinely NEW,
    // trailing parameter, appended AFTER `instanceBufferSetLayout` so every
    // existing call site (which only ever supplies positional arguments up
    // through `instanceBufferSetLayout`) keeps compiling completely
    // unmodified. When non-VK_NULL_HANDLE, it must be Core's one
    // SceneServicesDescriptorSet::Layout() exactly, and the resulting
    // VkPipelineLayout carries it at `set = 1`, CONTIGUOUSLY with whatever
    // (if anything) occupies `set = 0` - Vulkan forbids a hole at `set = 0`
    // if `set = 1` is present, so when neither `materialSetLayout` nor
    // `instanceBufferSetLayout` is supplied, this Pipeline synthesizes its
    // own small, owned, zero-binding filler layout for `set = 0` (see
    // Pipeline.cpp) rather than leaving a gap. Owned by whoever calls in
    // (Core's one SceneServicesDescriptorSet instance), never reached-for by
    // Pipeline itself. See HasSceneServicesSet() below.
    //
    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE3 - this
    // single-VkFormat constructor is now a thin forwarder (see Pipeline.cpp)
    // onto the new std::span<const VkFormat> constructor below, built as a
    // one-element span. Its own signature/behavior/PSO output are
    // completely unchanged from before this campaign - every existing call
    // site keeps compiling and behaving identically.
    Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout instanceBufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE);

    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE3 - the
    // real N-color-attachment constructor: builds a VkPipelineRenderingCreateInfo
    // with `colorAttachmentCount == colorFormats.size()` and one
    // VkPipelineColorBlendAttachmentState per entry (all identical - opaque,
    // no blending, full RGBA write mask, matching this engine's existing
    // single-target default exactly; per-attachment blend-state
    // customization is out of scope for this campaign). `colorFormats` must
    // be non-empty and no larger than kPipelineMaxColorAttachments (asserted
    // in the .cpp, debug builds only). Every other parameter behaves
    // identically to the single-format constructor above, including the new
    // trailing `instanceBufferSetLayout` (render-pass-5 campaign, PHASE2) and
    // `sceneServicesSetLayout` (better-render-pass-6 campaign, PHASE3).
    Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
        const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout instanceBufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE);
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    VkPipeline Native() const noexcept { return m_pipeline; }
    VkPipelineLayout Layout() const noexcept { return m_layout; }

    // See `debugName` above - empty string when this Pipeline was built
    const std::string& DebugName() const noexcept { return m_debugName; }

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE4 (task_manager/render-pass-5/
    // PHASE4_PER_BATCH_RESOURCE_MANAGEMENT_AND_BATCHING.md) - which
    // VertexLayout this Pipeline was actually built with (see the
    // constructor's own `vertexLayout` parameter above). Needed so
    // RenderSystem::CollectGpuDrivenBatches() can check PHASE0's Locked
    // Design Decision 7(c) ("a group's shared Pipeline was built with
    // EXACTLY VertexLayout::PositionNormal") without this campaign
    // inventing a second, parallel place to track a fact this class already
    // knows internally - mirrors DebugName() above's own "small, additive,
    // purely descriptive accessor" precedent.
    VertexLayout VertexLayoutKind() const noexcept { return m_vertexLayout; }

    // Global Scene Services Descriptor Set campaign (better-render-pass-6),
    // PHASE3 - true if this Pipeline was built with a non-VK_NULL_HANDLE
    // `sceneServicesSetLayout` (see the constructor's own comment above),
    // i.e. its VkPipelineLayout carries a real `set = 1`. Mirrors
    // VertexLayoutKind() above's own "small, additive, purely descriptive
    // accessor" precedent - used downstream (PHASE5) to gate whether
    // Renderer::Submit() is given a scene-services VkDescriptorSet to bind,
    // and (PHASE0 global rule 10) to assert Renderer::SubmitIndirect() is
    // never called with a Pipeline built this way.
    bool HasSceneServicesSet() const noexcept { return m_hasSceneServicesSet; }

    // True iff built with a non-null materialSetLayout - this Pipeline's
    // VkPipelineLayout actually declares a descriptor set at index 0 for a
    // material texture. Mirrors HasSceneServicesSet(). A depth-only pipeline
    // built with useMaterialTexture = false reports false here, which is what
    // lets Renderer::Submit() safely drop a non-null materialDescriptorSet
    // instead of binding it against a pipeline layout with no set 0.
    bool HasMaterialSet() const noexcept { return m_hasMaterialSet; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::string m_debugName;
    VertexLayout m_vertexLayout = VertexLayout::PositionColor;

    // Global Scene Services Descriptor Set campaign (better-render-pass-6),
    // PHASE3 - see HasSceneServicesSet() above.
    bool m_hasSceneServicesSet = false;

    // See HasMaterialSet() above.
    bool m_hasMaterialSet = false;
    // Owned ONLY when this Pipeline needed a zero-binding filler at set = 0
    // (sceneServicesSetLayout != VK_NULL_HANDLE AND neither
    // materialSetLayout nor instanceBufferSetLayout was supplied).
    // VK_NULL_HANDLE otherwise.
    VkDescriptorSetLayout m_syntheticSetZeroLayout = VK_NULL_HANDLE;
};

} // namespace gte
