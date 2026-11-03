#pragma once

#include "../../ECS/Entity.h"

#include <vector>

namespace gte {

class Registry;

// Appends every textured mesh part whose nearest ToonShading (on itself or an ancestor) is enabled. Does not clear `out`.
// A scene without any ToonShading costs one size check.
void CollectToonParts(Registry& registry, std::vector<Entity>& out);

} // namespace gte
