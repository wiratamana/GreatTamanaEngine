#include "Registry.h"

#include <unordered_map>

namespace gte {
namespace detail {

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE5 - see Registry.h's own extensive header comment on
// ComponentTypeId<T>()/ResolveComponentTypeIdByName() for the full incident
// this fixes. This function's own body is deliberately the ONLY place a
// component type's numeric slot is ever decided, process-wide - defined
// out-of-line, in this ordinary (non-template, non-inline) .cpp file, so it
// compiles into exactly ONE real symbol inside GreatTamanaEditor.exe's own
// image, correctly resolved/imported as that SAME single symbol by any
// Project Assembly .dll that calls it (mirrors ComponentTypeRegistry::
// Instance()'s own already-proven-correct "real singleton in a real .cpp
// file" shape - see docs/conventions/project-assembly-system.md).
//
// Not thread-safe (matches Registry itself, and every other ECS type in
// this single-threaded engine).
std::size_t ResolveComponentTypeIdByName(const std::string& mangledTypeName)
{
    static std::unordered_map<std::string, std::size_t> ids;
    static std::size_t next = 0;

    const auto it = ids.find(mangledTypeName);
    if (it != ids.end()) {
        return it->second;
    }

    const std::size_t id = next++;
    ids.emplace(mangledTypeName, id);
    return id;
}

} // namespace detail
} // namespace gte
