#pragma once

#include "VolumetricCloudDebugEndpoint.h"
#include "VolumetricCloudTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <cstdint>

namespace gte {

// Volumetric clouds. This stage only loads the baked noise assets and shows one of them as a tiled overlay,
// so the upload, the REPEAT sampling and the axis order can be checked on screen.
class VolumetricCloudFeature final : public fx::Feature {
public:
    explicit VolumetricCloudFeature(fx::FeatureHost& host);

    const char* Name() const override { return kVolumetricCloudFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const VolumetricCloudSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const VolumetricCloudSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    VolumetricCloudViewSnapshot Snapshot(rg::ViewScope scope) const;

    // Size of a loaded asset, zero while it is not loaded.
    fx::VolumeSize ShapeSize() const noexcept { return m_shape.Size(); }
    fx::VolumeSize DetailSize() const noexcept { return m_detail.Size(); }
    Extent2D WeatherSize() const noexcept { return m_weather.Size(); }
    Extent2D BlueNoiseSize() const noexcept { return m_blueNoise.Size(); }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void CompositePass(fx::CompositeFrame& f);
    // The asset behind the chosen debug view; invalid while it is missing.
    fx::Texture AcquireDebugSource(fx::CompositeFrame& f);

    VolumetricCloudSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    VolumetricCloudDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<VolumetricCloudViewSnapshot> m_views;
    fx::StaticVolume m_shape{ kCloudShapeAsset };
    fx::StaticVolume m_detail{ kCloudDetailAsset };
    fx::StaticTexture m_weather{ kCloudWeatherAsset };
    fx::StaticTexture m_blueNoise{ kCloudBlueNoiseAsset };
    fx::ComputeShader m_slice3dShader{ "VolumetricCloudSlice3D" };
    fx::ComputeShader m_slice2dShader{ "VolumetricCloudSlice2D" };
    fx::ViewOutput m_output{ "VolumetricCloud" };
};

} // namespace gte
