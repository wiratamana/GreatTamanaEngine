#pragma once
#include "../../Core/Extent.h"
#include "../../Math/Mat4.h"
#include "../../Renderer/RenderGraph/GpuParams.h"
#include "../../Renderer/RenderGraph/PerView.h"
#include "../../Renderer/RenderGraph/PersistentTarget.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/Shader.h"

#include <cstdint>
#include <type_traits>

namespace gte {

// Per-view inputs for the shadow mask pass.
struct ShadowMaskInputs {
    // Static-storage string: the persistent texture's name within the feature scope.
    const char* outputName = nullptr;
    rg::ViewScope viewScope = rg::ViewScope::Shared;
    Extent2D viewExtent;
    Mat4 invViewProjection;
    Mat4 lightViewProjection;
    float depthBias = 0.0f;
    std::uint32_t shadowMapResolution = 0;
};

// Matches the std430 block in ShadowMask.comp (144 bytes).
struct ShadowMaskParamsGpu {
    Mat4 invViewProjection;
    Mat4 lightViewProjection;
    float depthBias = 0.0f;
    float unused = 0.0f;
    float shadowTexelSize = 0.0f; // 1 / shadow map resolution.
    float pad = 0.0f;
};
static_assert(sizeof(Mat4) == 64);
static_assert(sizeof(ShadowMaskParamsGpu) == 144);
static_assert(std::is_trivially_copyable_v<ShadowMaskParamsGpu>);

ShadowMaskParamsGpu MakeShadowMaskParams(const ShadowMaskInputs& inputs) noexcept;

// Reconstructs world position from view depth and the shadow map, writes an r8 shadow mask.
class ShadowMaskRenderer {
public:
    // Returns an invalid handle when the shadow map is invalid, the scope is not a per-view scope,
    // or the mask texture was refused.
    rg::TextureHandle AddMaskPass(rg::RenderGraphBuilder& builder, const ShadowMaskInputs& inputs,
        rg::TextureHandle shadowMap, rg::TextureHandle sceneDepth);

private:
    struct ViewState {
        rg::PersistentTarget mask;
        rg::GpuParams<ShadowMaskParamsGpu> params;
    };

    rg::Shader m_shader{ "ShadowMask" };
    rg::PerView<ViewState> m_views;
};

} // namespace gte
