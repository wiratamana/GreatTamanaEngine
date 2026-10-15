#pragma once

// Kit-private: the per-callback logic of the module adapter, free of Core so tests can drive it.

#include "FeatureSetup.h"
#include "FrameContext.h"
#include "SharedOncePerFrame.h"
#include "../../Renderer/RenderGraph/BoundedOnceSet.h"

#include <cstdint>
#include <optional>

namespace gte::fx::detail {

struct RunInput {
    const Registration* registration = nullptr;
    SharedOncePerFrame* sharedLatch = nullptr; // Set for a once-per-frame registration.
    const std::optional<View>* view = nullptr; // Empty when the engine has no data for the view.
    Registry* scene = nullptr;
    FrameContext* context = nullptr; // Everything but scope and serial filled by the caller.
    rg::TextureHandle sceneColor{};
    rg::TextureHandle sceneDepth{};
    rg::TextureHandle privateTarget{};
    Extent2D extent{};
    std::uint64_t frameIndex = 0;
    std::uint64_t serial = 0;
};

// Gate, view check and once-per-frame check, then the callback inside the feature scope.
// A throwing callback is logged once through `failures`. True when the callback returned normally.
bool RunRegistration(const RunInput& input, rg::BoundedOnceSet<32>& failures);

} // namespace gte::fx::detail
