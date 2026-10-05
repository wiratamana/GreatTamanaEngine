#pragma once

// Live validation harness for the per-layer TextureArray render-target
// mechanism (WriteArrayLayer()/resolveArrayLayer()) - mirrors
// src/Editor/TextureArrayValidation.h's exact shape (lazy init against a
// live Renderer&, persistent owned outputs, declared via
// RenderGraphBuilder::AddRenderPass(), ViewScope::SceneView +
// RenderPassCategory::Debug, toggled by its own small Scene-panel checkbox,
// zero effect on the default Game View).
//
// Declares a 4-layer, depth-only TextureArray and, for each layer i, a
// small graphics pass that calls WriteArrayLayer(handle, i, ...) in
// setup() and resolveArrayLayer(handle, i) in execute(), drawing a
// hand-picked subset of 3 dummy cubes (reusing
// PrimitiveMeshGenerator::Generate(PrimitiveType::Cube) - no new vertex
// data invented) through ONE SHARED, wide orthographic view-projection
// transform - every layer sees the exact same camera; only WHICH of the
// 3 cubes each layer's own pass submits differs (a deliberately
// hand-authored inclusion table, never a generalized frustum-culling
// result). Layers 0/1/2 each draw exactly one cube (at its own distinct,
// camera-relative screen position, since the 3 cubes sit at different
// world X positions under the one shared camera); layer 3 draws all
// three - proving both "4 layers hold visibly different depth content"
// and "an object excluded from one layer's draw list but included in
// another's appears in the correct layer only".
//
// A second pass group (4 compute passes) extracts each layer back into its
// own plain 2D RenderTexture output, since TextureArray has no direct
// GET /get_texture support - mirrors TextureArrayValidation's own "Pass B
// x4" extraction idea exactly, adapted for a SAMPLED depth source
// (sampler2DArray) instead of a storage-image color one (a depth image can
// never be storage-capable - see TextureArray2D.cpp's own constructor
// assert).

#include "EditorLayer.h"
#include "../Math/Mat4.h"
#include "../Renderer/ComputeDescriptorSet.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Pipeline.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <array>
#include <optional>

namespace gte {

namespace rg {
class RenderPassToggleRegistry;
} // namespace rg

class Renderer;

// IEditorLayer::ArrayLayerRenderValidationHandles - see EditorLayer.h's own
// definition (reused directly as this class's own AddPass() return type,
// mirroring TextureArrayValidation's identical precedent).

class ArrayLayerRenderValidation {
public:
    ArrayLayerRenderValidation() = default;
    ~ArrayLayerRenderValidation();

    ArrayLayerRenderValidation(const ArrayLayerRenderValidation&) = delete;
    ArrayLayerRenderValidation& operator=(const ArrayLayerRenderValidation&) = delete;
    ArrayLayerRenderValidation(ArrayLayerRenderValidation&&) = delete;
    ArrayLayerRenderValidation& operator=(ArrayLayerRenderValidation&&) = delete;

    // Declares this frame's 8 passes (4 depth-only per-layer write passes;
    // 4 compute extraction passes sampling each layer back into its own 2D
    // output) - lazily builds this object's own Pipeline/ComputePipeline/
    // RenderTexture/Mesh the first time this is called (needs a live
    // Renderer/VkDevice).
    //
    // `toggleRegistry` (default nullptr) mirrors every other
    // AddXxxValidationPass() independent-gating convention (see
    // docs/conventions/render-pass-toggle-honesty.md).
    IEditorLayer::ArrayLayerRenderValidationHandles AddPass(
        rg::RenderGraphBuilder& builder, Renderer& renderer, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Transitions all 4 per-layer extraction outputs from
    // ComputeShaderWrite to ShaderRead - mirrors
    // TextureArrayValidation::FinalizeForSampling(). A safe no-op whenever
    // AddPass() above was not actually called this frame.
    void FinalizeForSampling(VkCommandBuffer cmd);

private:
    void EnsureInitialized(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;

    // The 4 write passes' shared depth-only graphics pipeline + hand-picked
    // dummy scene (3 cubes, reusing PrimitiveMeshGenerator's own existing
    // Cube generator - see this file's own header comment).
    std::optional<Pipeline> m_writePipeline;
    std::optional<Mesh> m_cubeMesh;
    std::array<Mat4, 3> m_objectModel{};
    Mat4 m_sharedViewProj = Mat4::Identity();

    // The 4 extraction passes - one shared compute pipeline, 4 independent
    // descriptor sets/output textures (one per layer).
    VkDescriptorSetLayout m_visualizeDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_visualizePipeline;
    std::array<ComputeDescriptorSet, 4> m_visualizeDescriptorSets;
    std::array<std::optional<RenderTexture>, 4> m_layerOutputs;

    // Tracks whether this frame's AddPass() actually declared anything, so
    // FinalizeForSampling() above only emits a barrier when real work
    // happened - mirrors TextureArrayValidation's own m_writtenThisFrame.
    bool m_writtenThisFrame = false;
};

} // namespace gte
