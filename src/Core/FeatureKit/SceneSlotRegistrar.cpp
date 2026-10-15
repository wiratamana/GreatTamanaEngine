#include "SceneSlotRegistrar.h"

#include "../Logging.h"
#include "../../Renderer/SceneServicesDescriptorSet.h"

#include <string>

namespace gte::fx::detail {

SceneSlot EngineSceneSlotRegistrar::Declare(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex)
{
    const std::string copy(name);
    const SceneServiceResourceKind resource =
        kind == SceneSlotKind::Image3D ? SceneServiceResourceKind::Image3D : SceneServiceResourceKind::Image2D;
    const std::uint32_t index = RegisterSceneServiceSlot(copy.c_str(), resource, preferredIndex, IsDepthSlotKind(kind));
    if (index >= kSceneServiceSlotCount) {
        GTE_LOG_ERROR("FeatureKit", "DeclareSceneSlot('" + copy + "'): the engine refused the slot");
        return SceneSlot{};
    }
    return SceneSlot{ index, kind };
}

} // namespace gte::fx::detail
