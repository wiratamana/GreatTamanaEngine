#pragma once

#include "ComputeShader.h"
#include "Frame.h"
#include "PerView.h"
#include "PersistentTarget.h"
#include "Texture.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/Culling/ClusterCullMath.h"
#include "../../Renderer/Culling/HizBuildPlan.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gte::fx {

// Last frame's pyramid of one view, ready to bind to ClusterDrawChain::Occlusion().
struct HizPrevious {
    bool usable = false;                    // False on the first frame, after a resize or camera cut, or while the build is off.
    HizReason reason = HizReason::Unavailable; // None when usable.
    Texture atlas;
    Mat4 viewProjection = Mat4::Identity(); // The view-projection the pyramid was built under.
    HizLayout layout;                       // Layout of `atlas`.
};

// Per-view history of the pyramid: two atlases (this frame's and last frame's) and the camera of the last build.
struct HizViewState {
    PersistentTarget atlas[2]{ PersistentTarget{ "HizAtlas0" }, PersistentTarget{ "HizAtlas1" } };
    HizValidity validity;
    HizLayout layout;
    Mat4 viewProjection = Mat4::Identity();
    Vec3 eye;
};

// Farthest-depth pyramid of the scene depth, one R32_Float atlas per view, kept for the next frame.
// Build() runs in a PostOpaque callback; Previous() in any callback of the next frame.
class HizPyramid {
public:
    explicit HizPyramid(std::string_view baseName);

    HizPyramid(const HizPyramid&) = delete;
    HizPyramid& operator=(const HizPyramid&) = delete;

    // False when the pass name table is full; Error() says why.
    bool Valid() const noexcept { return m_error.empty(); }
    const std::string& Error() const noexcept { return m_error; }

    // "<base>.Base" and "<base>.Top" for a gate and for the stats registry.
    std::vector<std::string> AllPassNames() const;

    // Declares the two build dispatches from the view's scene depth. False when nothing usable was built
    // (a toggled-off pass, a missing shader, an invalid target); the history of that view restarts.
    bool Build(PostOpaqueFrame& frame);

    // The pyramid built last frame for this frame's view, with its guards applied.
    HizPrevious Previous(Frame& frame);

    // Forgets the history of the frame's view (for a feature that was switched off).
    void Reset(Frame& frame);

private:
    std::string m_base;
    std::string m_error;
    std::array<const char*, kHizPassCount> m_passNames{}; // Base, Top.
    ComputeShader m_baseShader{ "HizBuildBase" };
    ComputeShader m_topShader{ "HizBuildTop" };
    PerView<HizViewState> m_views;
};

} // namespace gte::fx
