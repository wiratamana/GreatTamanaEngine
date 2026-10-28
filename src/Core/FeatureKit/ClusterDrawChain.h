#pragma once

#include "ClusterDrawState.h"
#include "ClusterDrawTypes.h"
#include "Frame.h"
#include "HizPyramid.h"
#include "Texture.h"
#include "../../Renderer/Culling/ClusterCullMath.h"

#include <cstdint>
#include <memory>
#include <span>

namespace gte::fx {

// GPU-culled draw of cluster-ready meshes. Made by Frame::ClusterDraw(), ended by Record().
// Declares nothing and claims nothing when a pass is toggled off, a device feature is missing,
// a limit is exceeded or a pass refuses; those entities stay on the CPU draw.
class ClusterDrawChain {
public:
    ~ClusterDrawChain();
    ClusterDrawChain(ClusterDrawChain&&) noexcept;
    ClusterDrawChain& operator=(ClusterDrawChain&&) noexcept;
    ClusterDrawChain(const ClusterDrawChain&) = delete;
    ClusterDrawChain& operator=(const ClusterDrawChain&) = delete;

    // The candidates to draw. Lanes of one frame must pass the same span.
    ClusterDrawChain& Instances(std::span<const ClusterInstance> instances);

    // The camera of the draw. The cull camera defaults to it.
    ClusterDrawChain& Camera(const ClusterCamera& camera);
    ClusterDrawChain& CullCamera(const ClusterCamera& camera);

    // Which tests run. The back-face cone test is ignored for a depth-only target.
    ClusterDrawChain& Culling(ClusterCullFlags flags);

    // Occlusion culling in two phases for a colour target: this frame's depth after the phase-1 draws builds the
    // pyramid (see HizPyramid), then every item is tested against it. `sceneDepth` is the depth of the target
    // (OpaqueFrame::SceneDepth()) and the cull camera must equal the draw camera. Needs a state with two lanes
    // and this chain on lane 0. When it cannot run, the chain is the plain single-phase chain and
    // ClusterStats::hizReason says why. Ignored for a depth-only target.
    ClusterDrawChain& TwoPhase(HizPyramid& pyramid, Texture sceneDepth);

    // Color + depth target (load). Exactly one of Target() and DepthOnlyTarget().
    ClusterDrawChain& Target(Texture sceneTarget);
    ClusterDrawChain& DepthOnlyTarget(Texture depth);

    // Colors each cluster by its id (debug view).
    ClusterDrawChain& IdTint(bool enabled);

    // Reads the survivor count back (colour target, first lane only). See ClusterDrawState::VisibleClusters().
    // A refused stats pass never takes a claim away.
    ClusterDrawChain& Stats(bool enabled);

    // Resolves and reserves everything and declares nothing. Idempotent.
    ClusterPlan Plan();

    // Declares reset, cull and draw per part (twice around one pyramid build for a two-phase chain), then takes
    // the entities over.
    ClusterStats Record();

    // Kit-internal state; named here so the verb's helper functions can take it.
    struct Impl;

private:
    friend class Frame;
    explicit ClusterDrawChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
