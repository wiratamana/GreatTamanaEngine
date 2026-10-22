#pragma once

#include "GpuDrivenSceneDebugEndpoint.h"
#include "GpuDrivenSceneTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

namespace gte {

// Per-view state: the frozen cull camera and the last report.
struct GpuDrivenSceneViewState {
    FrozenCullCamera frozenCamera;
    GpuDrivenSceneViewSnapshot snapshot;
};

// GPU frustum culling of cluster-ready meshes in the opaque stage of every view.
// Entities it draws are skipped by the CPU draw of the same view.
class GpuDrivenSceneFeature final : public fx::Feature {
public:
    explicit GpuDrivenSceneFeature(fx::FeatureHost& host);

    const char* Name() const override { return kGpuDrivenSceneFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const GpuDrivenSceneSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const GpuDrivenSceneSettings& candidate);

    // The last report of `scope`'s view; `rendered` is false when it has not run for a few frames.
    GpuDrivenSceneViewSnapshot Snapshot(rg::ViewScope scope) const;

    // False until a frame has shown that the device can run the cluster draw.
    bool Supported() const noexcept { return m_supported; }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void OpaquePass(fx::OpaqueFrame& f);
    void HizPass(fx::PostOpaqueFrame& f);

    GpuDrivenSceneSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    bool m_supported = false;
    GpuDrivenSceneDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<GpuDrivenSceneViewState> m_views;
    fx::ClusterDrawState m_state{ kGpuDrivenSceneFeatureName, fx::ClusterDrawShape{ kMainViewLanes, kMainViewPartSlots } };
    fx::HizPyramid m_hiz{ "GpuDrivenScene.Hiz" };
};

} // namespace gte
