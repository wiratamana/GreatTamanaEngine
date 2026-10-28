#pragma once

#include "ComputeShader.h"
#include "Frame.h"
#include "Texture.h"
#include "TransientTarget.h"
#include "../../Renderer/Culling/ClusterCullMath.h"
#include "../../Renderer/Culling/HizBuildPlan.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gte::fx {

// What a frame's pyramid of one view needs. Decided without declaring anything.
struct HizPlan {
    HizReason reason = HizReason::Unavailable; // None when Build() can run.
    HizLayout layout;
    HizBuildPlan dispatches;

    bool Usable() const noexcept { return reason == HizReason::None; }
};

// Farthest-depth pyramid of the view's scene depth, one R32_Float atlas per view and frame.
// It is built and used in the same frame: nothing is kept.
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

    // Checks the view size, the depth, both pass toggles and both shaders. Declares nothing.
    // `sceneDepth` must be this view's depth of this frame. TooLarge for a view the pyramid cannot cover.
    HizPlan Plan(Frame& frame, Texture sceneDepth);

    // Declares the two build dispatches from `sceneDepth` and returns the atlas of this frame.
    // Invalid (and nothing usable) when `plan` is not usable or a pass was refused.
    Texture Build(Frame& frame, const HizPlan& plan, Texture sceneDepth);

private:
    std::string m_base;
    std::string m_error;
    std::array<const char*, kHizPassCount> m_passNames{}; // Base, Top.
    ComputeShader m_baseShader{ "HizBuildBase" };
    ComputeShader m_topShader{ "HizBuildTop" };
    TransientTarget m_atlas;
};

} // namespace gte::fx
