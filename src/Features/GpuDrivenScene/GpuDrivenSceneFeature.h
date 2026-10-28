#pragma once

#include "GpuDrivenSceneDebugEndpoint.h"
#include "GpuDrivenSceneTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

namespace gte {

// The camera a view culls with, shared by the cluster cull and the CPU cull.
struct CullPlan {
    fx::ClusterCamera live;
    fx::ClusterCamera cull; // Meaningful only when `source` is not None.
    CullCameraSource source = CullCameraSource::None;
};

// Per-view state: the tested history, the last report and this frame's cull plan.
struct GpuDrivenSceneViewState {
    TestedHistory tested;
    GpuDrivenSceneViewSnapshot snapshot;
    std::uint64_t gpuFrame = 0; // Frame in which `snapshot.gpu` was written.
    CullPlan plan;
    std::uint64_t planFrame = 0;
    bool hasPlan = false;
};

// Culling of every view on both draw paths. Cluster-ready meshes are culled on the GPU and skipped by
// the CPU draw of the same view; every other mesh is frustum-culled on the CPU.
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
    void SceneCullPass(fx::OpaqueFrame& f);

    // Once per frame, at the first view callback.
    void BeginFrame(std::uint64_t frameIndex);

    GpuDrivenSceneSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    std::uint64_t m_beginFrame = 0;
    bool m_frameBegun = false;
    bool m_supported = false;
    GpuDrivenSceneDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<GpuDrivenSceneViewState> m_views;
    fx::ClusterDrawState m_state{ kGpuDrivenSceneFeatureName, fx::ClusterDrawShape{ kMainViewLanes, kMainViewPartSlots } };
    fx::HizPyramid m_hiz{ "GpuDrivenScene.Hiz" };
};

} // namespace gte
