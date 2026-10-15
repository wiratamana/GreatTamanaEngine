#pragma once

#include "ShadowMath.h"
#include "ShadowTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../Math/Mat4.h"

#include <cstdint>

namespace gte {

// What the depth pass hands to the mask pass.
struct ShadowMapData {
    fx::Texture texture;
    Mat4 lightViewProjection;
};

// Per-view resources.
struct ShadowViewState {
    fx::OffscreenTarget map{ "Shadow.Map" };
    fx::PersistentTarget mask{ "Shadow.Mask" };
    fx::GpuParams<ShadowMaskParamsGpu> params;
};

// One sun, one shadow map: a PreOpaque depth pass, a PostOpaque compute mask pass and a PostComposite darken pass.
class ShadowFeature final : public fx::Feature {
public:
    explicit ShadowFeature(fx::FeatureHost& host);

    const char* Name() const override { return kShadowFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    ShadowSettings& Settings() noexcept { return m_settings; }

private:
    void DepthPass(fx::PreOpaqueFrame& f);
    void MaskPass(fx::PostOpaqueFrame& f);
    void CompositePass(fx::CompositeFrame& f);

    // The map size is fixed on first use; a later settings change only warns once.
    std::uint32_t LockedMapResolution(const ShadowSettings& settings);

    ShadowSettings m_settings;
    std::uint32_t m_mapResolution = 0;
    bool m_resolutionWarned = false;

    fx::PerView<ShadowViewState> m_views;
    fx::SceneSlot m_shadowMapSlot;
    fx::ComputeShader m_maskShader{ "ShadowMask" };
    fx::FullscreenShader m_compositeShader{ "ShadowComposite" };
    fx::SceneShaderSet m_depthShaders = fx::SceneShaderSet::DepthOnly("ShadowDepth");
    fx::Channel<ShadowMapData> m_mapData{ "Map" };
    fx::Channel<fx::Texture> m_maskData{ "Mask" };
};

} // namespace gte
