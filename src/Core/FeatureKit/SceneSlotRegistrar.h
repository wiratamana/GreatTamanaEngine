#pragma once

// Kit-private: declares scene slots with the engine's process-wide slot registry.

#include "SceneSlot.h"

namespace gte::fx::detail {

class EngineSceneSlotRegistrar final : public ISceneSlotRegistrar {
public:
    SceneSlot Declare(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex) override;
};

} // namespace gte::fx::detail
