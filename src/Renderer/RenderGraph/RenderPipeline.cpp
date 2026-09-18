#include "RenderPipeline.h"

#ifndef NDEBUG
#include <unordered_map>
#endif

namespace gte::rg {

#ifndef NDEBUG

namespace {

// A plain function-local static - debug-only, never touched in release,
// never used for runtime identity comparisons (see RenderPassId's own doc
// comment in RenderPipeline.h). Deliberately NOT inside operator""_passId
// itself - a consteval function cannot host a mutable function-local
// static (see that comment for the full reasoning).
std::unordered_map<std::uint64_t, const char*>& PassIdDebugNameRegistry()
{
    static std::unordered_map<std::uint64_t, const char*> registry;
    return registry;
}

} // namespace

void RegisterPassIdDebugName(RenderPassId id, const char* name) noexcept
{
    if (name != nullptr) {
        PassIdDebugNameRegistry()[id.hash] = name;
    }
}

const char* DebugNameForPassId(RenderPassId id) noexcept
{
    const auto& registry = PassIdDebugNameRegistry();
    const auto it = registry.find(id.hash);
    return it != registry.end() ? it->second : "<unknown>";
}

#endif // !NDEBUG

} // namespace gte::rg
