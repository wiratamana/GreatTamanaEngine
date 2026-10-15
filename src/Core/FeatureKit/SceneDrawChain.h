#pragma once

#include "Frame.h"
#include "Texture.h"
#include "../../Math/Mat4.h"

#include <memory>

namespace gte::fx {

class SceneShaderSet;

// One redraw of the scene into a depth (and optionally color) target. Made by Frame::SceneDraw(), ended by Record().
//   f.SceneDraw("Map.Draw").Shaders(set).Camera(lightViewProjection).ClearDepthTarget(map, 1.0f).Record();
// Declares nothing while its pass is toggled off or the shader set is not ready.
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

    // False when nothing was declared.
    bool Record();

private:
    friend class Frame;
    struct Impl;
    explicit SceneDrawChain(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
