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
// On a toggle, missing feature, limit or refused pass it declares and claims nothing; entities stay on the CPU draw.
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

    // Two-phase occlusion for a colour target: phase-1 depth builds the pyramid, then every item is tested.
    // The cull camera must equal the draw camera; needs two lanes. If it cannot run, ClusterStats::hizReason says why.
    ClusterDrawChain& TwoPhase(HizPyramid& pyramid, Texture sceneDepth);

    // Secondary-view chain only: draws the survivors the primary chain published for every part, else culls itself.
    // ClusterStats::sharedSurvivors says which.
    ClusterDrawChain& ReuseSurvivors();

    // Color + depth target (load). Exactly one of Target() and DepthOnlyTarget().
    ClusterDrawChain& Target(Texture sceneTarget);
    ClusterDrawChain& DepthOnlyTarget(Texture depth);

    // Colors each cluster by id (debug view); `byPhase` colors by draw phase instead (red, green, gray = single).
    ClusterDrawChain& IdTint(bool enabled, bool byPhase = false);

    // Reads the survivor count and outcome counters back (colour target, first lane only). A refused pass keeps the claim.
    ClusterDrawChain& Stats(bool enabled);

    // Resolves and reserves everything and declares nothing. Idempotent.
    ClusterPlan Plan();

    // Declares reset, cull and draw per part and takes the entities over. A part wholly outside the
    // cull frustum gets no pass but its entities are still taken over.
    ClusterStats Record();

    // Kit-internal state; named here so the verb's helper functions can take it.
    struct Impl;

private:
    friend class Frame;
    explicit ClusterDrawChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
