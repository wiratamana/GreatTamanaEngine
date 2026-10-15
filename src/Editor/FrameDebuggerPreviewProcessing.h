#pragma once

#include "../Renderer/RenderGraph/Shader.h"
#include "../Renderer/Texture2D.h"

#include <array>
#include <cstdint>
#include <optional>
#include <volk.h>

namespace gte {

class Renderer;
class RenderTexture;

// The channel isolation of the preview. The values match the `channel` push constant of FrameDebuggerPreview.comp.
enum class FrameDebuggerPreviewChannel {
    All,
    R,
    G,
    B,
    A,
};

// Per-pixel preview transform: levels remap first (clamped to [0, 1]), then channel isolation. An isolated
// channel is replicated over RGB with alpha 1. The compute shader implements the same formula.
std::array<float, 4> ApplyFrameDebuggerPreviewTransform(
    std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite) noexcept;

// Renders a retained history texture through the channel and levels transform into its own scratch texture,
// on demand only. The source is never modified.
class FrameDebuggerPreviewRenderer {
public:
    FrameDebuggerPreviewRenderer() = default;

    FrameDebuggerPreviewRenderer(const FrameDebuggerPreviewRenderer&) = delete;
    FrameDebuggerPreviewRenderer& operator=(const FrameDebuggerPreviewRenderer&) = delete;

    // `source` must be in the shader-read state, as retained history textures always are. The scratch texture
    // is recreated when the source size changes; the returned reference stays valid until the next call.
    const Texture2D& RenderPreview(Renderer& renderer, const RenderTexture& source, FrameDebuggerPreviewChannel channel,
        float levelsBlack, float levelsWhite);

    // The scratch texture's view and sampler, in the shader-read state; null before the first RenderPreview().
    VkImageView OutputView() const noexcept { return m_outputTexture.has_value() ? m_outputTexture->View() : VK_NULL_HANDLE; }
    VkSampler OutputSampler() const noexcept
    {
        return m_outputTexture.has_value() ? m_outputTexture->Sampler() : VK_NULL_HANDLE;
    }

private:
    rg::Shader m_shader{ "FrameDebuggerPreview" };
    std::optional<Texture2D> m_outputTexture;
};

} // namespace gte
