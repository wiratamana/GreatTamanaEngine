#pragma once

#include "../Renderer/RenderGraph/Shader.h"
#include "../Renderer/Texture2D.h"

#include <array>
#include <cstdint>
#include <optional>

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

// Per-pixel preview: optional tone mapping (rgb), levels remap to [0, 1], then channel isolation.
// The compute shader implements the same formula.
std::array<float, 4> ApplyFrameDebuggerPreviewTransform(std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel,
    float levelsBlack, float levelsWhite, bool sceneReferred = false) noexcept;

// Renders a retained history texture through the channel and levels transform into its own scratch texture,
// on demand only. The source is never modified.
class FrameDebuggerPreviewRenderer {
public:
    FrameDebuggerPreviewRenderer() = default;

    FrameDebuggerPreviewRenderer(const FrameDebuggerPreviewRenderer&) = delete;
    FrameDebuggerPreviewRenderer& operator=(const FrameDebuggerPreviewRenderer&) = delete;

    // `source` must be shader-read; `sceneReferred` tone maps linear scene color. The result is valid until the next call.
    const Texture2D& RenderPreview(Renderer& renderer, const RenderTexture& source, FrameDebuggerPreviewChannel channel,
        float levelsBlack, float levelsWhite, bool sceneReferred);

    // The scratch texture, in the shader-read state; null before the first RenderPreview().
    const Texture2D* OutputTexture() const noexcept { return m_outputTexture.has_value() ? &*m_outputTexture : nullptr; }

private:
    rg::Shader m_shader{ "FrameDebuggerPreview" };
    std::optional<Texture2D> m_outputTexture;
};

} // namespace gte
