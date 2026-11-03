#include "ToonPartCollector.h"

#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/ToonShading.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"

#include <cstdint>

namespace gte {

namespace {

// Longest parent chain walked; a malformed (cyclic) chain stops here.
constexpr std::uint32_t kMaxHierarchyDepth = 64;

// The nearest ToonShading on `part` or its ancestors, or null.
const ToonShading* NearestToonShading(Registry& registry, Entity part)
{
    Entity current = part;
    for (std::uint32_t depth = 0; depth < kMaxHierarchyDepth && current.IsValid(); ++depth) {
        if (const ToonShading* toon = registry.TryGetComponent<ToonShading>(current)) {
            return toon;
        }
        const Transform* transform = registry.TryGetComponent<Transform>(current);
        if (transform == nullptr) {
            return nullptr;
        }
        current = EffectiveParent(registry, *transform);
    }
    return nullptr;
}

} // namespace

void CollectToonParts(Registry& registry, std::vector<Entity>& out)
{
    if (registry.Storage<ToonShading>().Size() == 0) {
        return;
    }
    // Parts without a material texture have no toon-capable draw; they stay on the main draw.
    ComponentStorage<MeshRenderer>& renderers = registry.Storage<MeshRenderer>();
    for (std::size_t i = 0; i < renderers.Size(); ++i) {
        if (!renderers.ComponentAt(i).texture.IsValid()) {
            continue;
        }
        const Entity part = renderers.EntityAt(i);
        const ToonShading* toon = NearestToonShading(registry, part);
        if (toon != nullptr && toon->enabled) {
            out.push_back(part);
        }
    }
}

} // namespace gte
