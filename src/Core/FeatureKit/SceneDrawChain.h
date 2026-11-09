#pragma once

#include "ClusterDrawTypes.h"
#include "Frame.h"
#include "Texture.h"
#include "../../Math/Mat4.h"

#include <cstdint>
#include <memory>

namespace gte::fx {

class SceneShaderSet;

// One scene redraw into a depth (optionally colour) target; ended by Record(). Declares nothing while toggled off.
//   f.SceneDraw("Map.Draw").Shaders(set).Camera(vp).ClearDepthTarget(map, 1.0f).CullToFrustum(0.25f).Record();
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

    // Binds the scene services set (lighting uniforms, scene textures) of this view. Needed by Shaded() sets. Not in a shared frame.
    SceneDrawChain& SceneServices();

    // Skips entities whose world bounds, grown by `paddingFraction` of their largest extent, miss the camera frustum.
    // Meshes without bounds are always drawn.
    SceneDrawChain& CullToFrustum(float paddingFraction = 0.0f);

    // With CullToFrustum(): also skips entities whose padded bounds lie outside any of these planes.
    SceneDrawChain& CullPlanes(const ExtraCullPlanes& planes);

    // Skips the entities of `exclusion` (read when the pass executes). An empty handle changes nothing.
    // The pass is declared even when every entity is skipped, so a depth clear still happens.
    SceneDrawChain& Exclude(const EntityExclusion& exclusion);

    // Draws only the entities of `selection`. An invalid selection draws nothing: Record() declares no pass.
    SceneDrawChain& Only(const EntitySelection& selection);

    // After Record(), takes the selected entities this pass can draw over from the main draw. Needs Only().
    // Declares nothing in a shared frame or while a frame capture is armed.
    SceneDrawChain& Claim();

    // False when nothing was declared.
    bool Record();

    // Entities claimed by the last Record(); 0 without Claim().
    std::uint32_t ClaimedCount() const noexcept;

private:
    friend class Frame;
    struct Impl;
    explicit SceneDrawChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
