#pragma once

// A fullscreen draw that can be recorded again inside another graphics pass (Frame Debugger replay).

#include "RenderGraphBuilder.h"

#include <functional>

namespace gte::rg {

struct PassContext;

struct FullscreenReplay {
    // Declares the shader bindings only; the host pass declares its own attachments.
    std::function<void(RenderGraphBuilder::PassBuilder&)> declare;

    // Binds the pipeline, the bound set and the push constants, then draws.
    std::function<void(PassContext&)> record;

    explicit operator bool() const noexcept { return static_cast<bool>(declare) && static_cast<bool>(record); }
};

} // namespace gte::rg
