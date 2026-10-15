#pragma once

#include "Compute/ComputeDescriptorSet.h"
#include "Compute/ComputePipeline.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h" // gte::rg::ResourceState
#include "Texture2D.h"
#include "VolumeTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <volk.h>

namespace gte {

class Renderer;

// How the preview shader reads texels, which camera/box framing and which proxy shape (box or frustum) it uses.
enum class VolumeTexturePreviewInterpretation : std::int32_t {
    GenericDensityInAlpha = 0, // Default: density in the alpha channel, box proxy.
    AerialPerspectiveFrustum = 1, // Aerial perspective volume: frustum proxy and dedicated framing.
};

// On-demand compute renderer that raymarches a VolumeTexture into a 256x256 RGBA8 thumbnail for HTTP fetches.
// Records its own dispatch via ImmediateSubmit(), so it needs no render graph.
class VolumeTexturePreviewRenderer {
public:
    VolumeTexturePreviewRenderer() = default;

    // Owns live Vulkan objects and is never moved or copied.
    VolumeTexturePreviewRenderer(const VolumeTexturePreviewRenderer&) = delete;
    VolumeTexturePreviewRenderer& operator=(const VolumeTexturePreviewRenderer&) = delete;

    ~VolumeTexturePreviewRenderer();

    struct CapturedRawPixels {
        std::vector<std::uint8_t> pixels; // Tightly packed width*height*4 RGBA8 bytes.
        int width = 0;
        int height = 0;
    };

    // Renders one blocking raymarch thumbnail of `volume` (at `previousState`) and reads it back; at most once per
    // request. The layout is restored; `interpretation` picks texel decoding, framing and proxy shape.
    CapturedRawPixels RenderPreview(Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState,
        VolumeTexturePreviewInterpretation interpretation = VolumeTexturePreviewInterpretation::GenericDensityInAlpha);

private:
    void EnsureInitialized(Renderer& renderer);

    static constexpr int kOutputWidth = 256;
    static constexpr int kOutputHeight = 256;
    static constexpr int kStepCount = 64;
    static constexpr float kDensityScale = 4.0f; // One fixed default is enough for debug visualization.

    // Must match `layout(local_size_x = 16, local_size_y = 16) in;` in src/Shaders/VolumeTexturePreview.comp.
    static constexpr std::uint32_t kLocalSizeX = 16;
    static constexpr std::uint32_t kLocalSizeY = 16;

    bool m_initialized = false;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_volumeSampler = VK_NULL_HANDLE; // Trilinear, clamp-to-edge; created once.
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_pipeline;
    // Allocated once; its bindings are rewritten on every call because each call may preview a different volume.
    ComputeDescriptorSet m_descriptorSet;
    std::optional<Texture2D> m_outputTexture; // Created once, overwritten by every call.
    // Current state of m_outputTexture: SHADER_READ_ONLY after init, GENERAL after every RenderPreview().
    rg::ResourceState m_outputTextureState;
};

} // namespace gte
