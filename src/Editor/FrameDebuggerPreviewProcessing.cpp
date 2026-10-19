#include "FrameDebuggerPreviewProcessing.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/ComputePass.h"
#include "../Renderer/RenderGraph/OneShot.h"
#include "../Renderer/RenderTexture.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace gte {

namespace {

float RemapLevels(float value, float levelsBlack, float levelsWhite) noexcept
{
    const float denom = std::max(levelsWhite - levelsBlack, 1e-5f);
    return std::clamp((value - levelsBlack) / denom, 0.0f, 1.0f);
}

// Matches the push-constant block of FrameDebuggerPreview.comp.
struct PushConstants {
    std::int32_t channel = 0;
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
};

} // namespace

std::array<float, 4> ApplyFrameDebuggerPreviewTransform(
    std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite) noexcept
{
    const std::array<float, 4> leveled{
        RemapLevels(srcRgba[0], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[1], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[2], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[3], levelsBlack, levelsWhite),
    };

    // No `default:`, so a new channel enumerator warns here instead of falling through.
    switch (channel) {
    case FrameDebuggerPreviewChannel::All:
        return leveled;
    case FrameDebuggerPreviewChannel::R:
        return { leveled[0], leveled[0], leveled[0], 1.0f };
    case FrameDebuggerPreviewChannel::G:
        return { leveled[1], leveled[1], leveled[1], 1.0f };
    case FrameDebuggerPreviewChannel::B:
        return { leveled[2], leveled[2], leveled[2], 1.0f };
    case FrameDebuggerPreviewChannel::A:
        return { leveled[3], leveled[3], leveled[3], 1.0f };
    }
    return leveled;
}

const Texture2D& FrameDebuggerPreviewRenderer::RenderPreview(Renderer& renderer, const RenderTexture& source,
    FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite)
{
    const Extent2D extent = source.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    if (!m_outputTexture.has_value() || m_outputTexture->Width() != width || m_outputTexture->Height() != height) {
        const std::vector<std::uint8_t> zeroPixels(static_cast<std::size_t>(width) * height * 4, 0);
        m_outputTexture.emplace(renderer.CreateTexture2D({ .pixelsRgba8 = zeroPixels.data(),
            .width = width,
            .height = height,
            .debugName = "FrameDebuggerPreviewScratch",
            .allowStorageImageAccess = true }));
    }

    rg::OneShot oneShot(renderer);
    const rg::TextureHandle src =
        oneShot.ImportReadOnly(source, rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
    const rg::TextureHandle dst = oneShot.Import(*m_outputTexture, rg::FinalState::ShaderRead);
    rg::ComputePass(oneShot.Builder(), m_shader, "FrameDebuggerPreview", rg::ViewScope::Shared, rg::RenderPassEvent::Opaques)
        .Bind("sourceTex", src)
        .Bind("outputImage", dst)
        .Push(PushConstants{ static_cast<std::int32_t>(channel), levelsBlack, levelsWhite })
        .Dispatch(extent);
    oneShot.Run();
    return *m_outputTexture;
}

} // namespace gte
