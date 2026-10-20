#pragma once

#include "../../ECS/Registry.h"
#include "../../Renderer/MaterialTextureHandle.h"

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace gte {

// False when the pipeline needs a material set and none resolved.
constexpr bool CanDrawWithMaterial(bool pipelineHasMaterialSet, bool materialSetResolved) noexcept
{
    return !pipelineHasMaterialSet || materialSetResolved;
}

// Handles of every material that a MeshRenderer in `registry` uses. Invalid handles are ignored.
std::unordered_set<MaterialTextureHandle> CollectMaterialTexturesInUse(Registry& registry);

// The cached handles that are not in `inUse`, in input order.
std::vector<MaterialTextureHandle> SelectUnusedMaterials(
    std::span<const MaterialTextureHandle> cached, const std::unordered_set<MaterialTextureHandle>& inUse);

// True when a sweep should run now: one is pending, and every `period` frames.
constexpr bool ShouldRunMaterialSweep(bool pending, std::uint64_t frameIndex, std::uint64_t period = 30) noexcept
{
    return pending && (period == 0 || frameIndex % period == 0);
}

} // namespace gte
