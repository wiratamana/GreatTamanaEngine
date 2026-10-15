#include "VolumeTexturePreviewRenderer.h"

#include "Renderer.h"
#include "RenderGraph/ComputePass.h"
#include "RenderGraph/OneShot.h"
#include "VolumeTexturePreviewMath.h"

#include <stdexcept>
#include <utility>

namespace gte {

namespace {

// Must match the `layout(push_constant)` block in src/Shaders/VolumeTexturePreview.comp.
// Each Vec3 followed by one float forms a 16-byte group with no padding (std140 vec3-then-scalar packing).
struct PushConstants {
    Vec3 eyePosition;
    float tanHalfFovY = 0.0f;
    Vec3 forward;
    float densityScale = 0.0f;
    Vec3 right;
    std::int32_t stepCount = 0;
    Vec3 up;
    float _padding0 = 0.0f;
    // Box half-extents when shapeMode == 0. When shapeMode == 1 it carries (farHalfWidth, farHalfHeight, halfDepth),
    // a different order than FrustumProxy declares them.
    Vec3 boxHalfExtents;
    float _padding1 = 0.0f;
    std::int32_t interpretationMode = 0; // VolumeTexturePreviewInterpretation, as int.
    float aerialPreviewExposure = 0.0f; // Only used when interpretationMode == 1.
    std::int32_t shapeMode = 0; // 0 = axis-aligned box (IntersectRayBox), 1 = tapering frustum (IntersectRayFrustum).
};
static_assert(sizeof(PushConstants) == 92, "the shader's push block is 92 bytes");

// Exposure applied before the shader's Reinhard tonemap for the aerial perspective interpretation.
// Its in-scattering values are tiny, so this lifts them into a legible mid-range.
static constexpr float kAerialPreviewExposure = 3200.0f;

} // namespace

VolumeTexturePreviewRenderer::~VolumeTexturePreviewRenderer()
{
    m_volumeSamplerHandle.Resolve(VK_NULL_HANDLE); // Unregister before the sampler goes away.
    if (m_volumeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_volumeSampler, nullptr);
    }
}

void VolumeTexturePreviewRenderer::EnsureInitialized(Renderer& renderer)
{
    if (m_initialized) {
        return;
    }

    m_device = renderer.GetVulkanContextInfo().device;

    // Trilinear, clamp-to-edge in all three axes, like VolumeTexture's sampler (VolumeTarget carries none).
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.25f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_volumeSampler) != VK_SUCCESS) {
        throw std::runtime_error("VolumeTexturePreviewRenderer: vkCreateSampler failed.");
    }

    // Zero-filled is enough: the first dispatch overwrites every pixel.
    const std::vector<std::uint8_t> zeroFilledPixels(static_cast<std::size_t>(kOutputWidth) * kOutputHeight * 4, 0);
    m_outputTexture.emplace(renderer.CreateTexture2D(zeroFilledPixels.data(), kOutputWidth, kOutputHeight,
        "VolumeTexturePreviewOutput", /*allowStorageImageAccess=*/true));

    m_initialized = true;
}

VolumeTexturePreviewRenderer::CapturedRawPixels VolumeTexturePreviewRenderer::RenderPreview(
    Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState,
    VolumeTexturePreviewInterpretation interpretation)
{
    EnsureInitialized(renderer);

    const bool aerial = interpretation == VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum;
    const VolumeCameraSetup setup = aerial
        ? ComputeAerialPerspectivePreviewCameraSetup(static_cast<int>(volume.extent.width),
              static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth))
        : ComputeVolumeCameraSetup(static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height),
              static_cast<int>(volume.extent.depth));

    PushConstants pushConstants{};
    pushConstants.eyePosition = setup.eyePosition;
    pushConstants.tanHalfFovY = setup.tanHalfFovY;
    pushConstants.forward = setup.forward;
    pushConstants.densityScale = kDensityScale;
    pushConstants.right = setup.right;
    pushConstants.stepCount = kStepCount;
    pushConstants.up = setup.up;
    pushConstants.boxHalfExtents = setup.boxHalfExtents;
    pushConstants.interpretationMode = static_cast<std::int32_t>(interpretation);
    pushConstants.aerialPreviewExposure = aerial ? kAerialPreviewExposure : 0.0f;

    // The aerial perspective preview raymarches a frustum: boxHalfExtents carries its far size and half depth.
    if (aerial) {
        pushConstants.shapeMode = 1;
        const FrustumProxy frustum = ComputeAerialPerspectivePreviewFrustum();
        pushConstants.boxHalfExtents = Vec3(frustum.farHalfWidth, frustum.farHalfHeight, frustum.halfDepth);
    }

    {
        rg::OneShot oneShot(renderer);
        const rg::TextureHandle source =
            oneShot.Import(volume, previousState, m_volumeSamplerHandle.Resolve(m_volumeSampler));
        const rg::TextureHandle output = oneShot.Import(*m_outputTexture, rg::FinalState::Keep);
        rg::ComputePass(oneShot.Builder(), m_shader, "VolumeTexturePreview", rg::ViewScope::Shared,
            rg::RenderPassEvent::Opaques)
            .Bind("volumeTex", source)
            .Bind("outputImage", output)
            .Push(pushConstants)
            .Dispatch(Extent2D{ static_cast<std::uint32_t>(kOutputWidth), static_cast<std::uint32_t>(kOutputHeight) });
        if (!oneShot.Run()) {
            return {};
        }
    }

    // The output is R8G8B8A8, never BGRA, so no channel swizzle is needed. It stays in the compute-write state.
    const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(m_outputTexture->Image(),
        VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM,
        Extent2D{ static_cast<std::uint32_t>(kOutputWidth), static_cast<std::uint32_t>(kOutputHeight) },
        m_outputTexture->CurrentState());

    CapturedRawPixels result;
    result.pixels = raw.pixels;
    result.width = raw.width;
    result.height = raw.height;
    return result;
}

} // namespace gte
