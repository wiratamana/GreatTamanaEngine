#pragma once

#include "../../Renderer/RenderGraph/PerView.h"

namespace gte::fx {

// One T per view scope (Shared, GameView, SceneView).
template <typename T>
using PerView = rg::PerView<T>;

} // namespace gte::fx
