#pragma once

#include "GpuDrivenSceneDebugEndpoint.h"
#include "GpuDrivenSceneTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <memory>

namespace gte {

// Per-view state: the frozen cull camera, the tested history and the last report.
struct GpuDrivenSceneViewState {
    FrozenCullCamera frozenCamera;
    TestedHistory tested;
    GpuDrivenSceneViewSnapshot snapshot;
};

// GPU culling of cluster-ready meshes in the opaque stage of every view.
// Entities it draws are skipped by the CPU draw of the same view.
class GpuDrivenSceneFeature final : public fx::Feature {
public:
    explicit GpuDrivenSceneFeature(fx::FeatureHost& host);

    const char* Name() const override { return kGpuDrivenSceneFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const GpuDrivenSceneSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    // Call FlushRequest() before reading Settings() for a read-modify-write.
    void ApplySettings(const GpuDrivenSceneSettings& candidate);

    // Applies a pending Scene panel request to the settings. Main thread only.
    void FlushRequest();

    // The link the Scene panel reads and writes. Shared, so either side may outlive the other.
    std::shared_ptr<fx::ViewCullLink> Link() const noexcept { return m_link; }

    // The last report of `scope`'s view; `rendered` is false when it has not run for a few frames.
    GpuDrivenSceneViewSnapshot Snapshot(rg::ViewScope scope) const;

    // False until a frame has shown that the device can run the cluster draw.
    bool Supported() const noexcept { return m_supported; }

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void OpaquePass(fx::OpaqueFrame& f);
    void HizPass(fx::PostOpaqueFrame& f);

    // Once per frame, at the first view callback.
    void BeginFrame(std::uint64_t frameIndex);

    GpuDrivenSceneSettings m_settings;
    std::shared_ptr<fx::ViewCullLink> m_link = std::make_shared<fx::ViewCullLink>();
    std::uint64_t m_newestFrame = 0;
    std::uint64_t m_beginFrame = 0;
    bool m_frameBegun = false;
    StatsHold m_hudStatsHold;
    bool m_hudStatsActive = false;
    bool m_supported = false;
    GpuDrivenSceneDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<GpuDrivenSceneViewState> m_views;
    fx::ClusterDrawState m_state{ kGpuDrivenSceneFeatureName, fx::ClusterDrawShape{ kMainViewLanes, kMainViewPartSlots } };
    fx::HizPyramid m_hiz{ "GpuDrivenScene.Hiz" };
};

} // namespace gte
