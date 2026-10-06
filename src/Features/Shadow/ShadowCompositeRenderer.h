#pragma once

#include "../../Renderer/RenderGraph/RenderGraphTypes.h" // rg::TextureHandle - real struct.

#include <volk.h>

#include <string>
#include <unordered_map>

namespace gte {

class Renderer;
namespace rg { class RenderGraphBuilder; }

// Darkens scene color by Shadow.Mask's output, writing into the
// compositor's private target under RenderFeatureBlendMode::ScreenSpaceMask.
//
// Hand-rolled fullscreen-triangle graphics pipeline, same low-level recipe
// as AtmosphereSkyBackgroundRenderer - keeps ONE DESCRIPTOR SET PER CALLING
// VIEW, never one shared set: a shared set would let the Game View's draw
// silently sample the Scene View's own scene-color/mask (or vice versa)
// whenever both are visible the same frame.
class ShadowCompositeRenderer {
public:
    ~ShadowCompositeRenderer();

    // `viewKey` ("Game"/"Scene") selects this view's own independent
    // descriptor set - see this class's own comment above. Must be a stable
    // string for this renderer's lifetime; a string literal is fine.
    void AddCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer, const char* viewKey,
        rg::TextureHandle privateTarget, VkExtent2D extent, rg::TextureHandle sceneColorHandle,
        VkSampler sceneColorSampler, rg::TextureHandle maskHandle, float strength);

private:
    void EnsurePipeline(Renderer& renderer);
    void Reset();
    VkDescriptorSet EnsureViewDescriptorSet(Renderer& renderer, const char* viewKey);

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    // One descriptor set per calling view - never collapse into one shared
    // VkDescriptorSet, see this class's own comment above.
    std::unordered_map<std::string, VkDescriptorSet> m_descriptorSetsByView;
};

} // namespace gte
