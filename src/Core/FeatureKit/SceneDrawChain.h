#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../../Math/Mat4.h"

#include <memory>

namespace gte::fx {

class SceneShaderSet;

// One scene redraw into a depth (optionally colour) target; ended by Record(). Declares nothing while toggled off.
//   f.SceneDraw("Map.Draw").Shaders(set).Camera(vp).ClearDepthTarget(map, 1.0f).CullToFrustum(0.25f).Record();
//   f.SceneDraw("Look.Draw").Shaders(shaded).Camera(vp).Target(f.SceneTarget()).SceneServices().Only(sel).Claim().Record();
class SceneDrawChain {
public:
    ~SceneDrawChain();
    SceneDrawChain(SceneDrawChain&&) noexcept;
    SceneDrawChain& operator=(SceneDrawChain&&) noexcept;
    SceneDrawChain(const SceneDrawChain&) = delete;
    SceneDrawChain& operator=(const SceneDrawChain&) = delete;

    SceneDrawChain& Shaders(SceneShaderSet& shaders);
    SceneDrawChain& Camera(const Mat4& viewProjection);
    SceneDrawChain& ClearDepthTarget(Texture depth, float clearValue);
    SceneDrawChain& ColorTarget(Texture color);

    // Draws into `target` as both color and depth, loading both (nothing is cleared). Use the view's scene target.
    SceneDrawChain& Target(Texture target);

    // Binds the scene services set (lighting uniforms and published scene textures) of this view. Needed by every set made with
    // SceneShaderSet::Shaded(). Refused in a shared frame.
    SceneDrawChain& SceneServices();

    // Skips entities whose world bounds, grown by `paddingFraction` of their largest extent, miss the camera frustum.
    // Meshes without bounds are always drawn.
    SceneDrawChain& CullToFrustum(float paddingFraction = 0.0f);

    // Skips the entities of `exclusion` (read when the pass executes). An empty handle changes nothing.
    // The pass is declared even when every entity is skipped, so a depth clear still happens.
    SceneDrawChain& Exclude(const EntityExclusion& exclusion);

    // Draws only the entities of `selection`. An invalid selection draws nothing: Record() declares no pass.
    SceneDrawChain& Only(const EntitySelection& selection);

    // Takes the selected entities over from the main draw of this view once the pass was declared, so they are
    // drawn once. Needs Only(). Declares nothing (the main draw keeps them) in a shared frame and while a frame
    // capture is armed for the primary view.
    SceneDrawChain& Claim();

    // False when nothing was declared.
    bool Record();

private:
    friend class Frame;
    struct Impl;
    explicit SceneDrawChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
