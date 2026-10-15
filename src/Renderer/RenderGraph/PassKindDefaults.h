#pragma once

#include "RenderGraphHandles.h"

namespace gte::rg {

// The pass kind of each declarative pass type. The Frame Debugger groups by it; transfers and compute share one label.
enum class DeclarativePassType : std::uint8_t { Compute, Fullscreen, Mesh, IndirectDraw, Transfer };

constexpr PassKind DefaultPassKind(DeclarativePassType type) noexcept
{
    switch (type) {
    case DeclarativePassType::Compute:
    case DeclarativePassType::Transfer:
        return PassKind::Compute;
    case DeclarativePassType::Fullscreen:
    case DeclarativePassType::Mesh:
    case DeclarativePassType::IndirectDraw:
        return PassKind::Graphics;
    }
    return PassKind::Graphics;
}

// The Frame Debugger child-event label of each declarative pass type.
constexpr RenderPassDrawKind DefaultDrawKind(DeclarativePassType type) noexcept
{
    return type == DeclarativePassType::Fullscreen ? RenderPassDrawKind::DrawQuad : RenderPassDrawKind::DrawMesh;
}

} // namespace gte::rg
