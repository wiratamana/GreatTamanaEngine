#include "RenderPipeline.h"

#ifndef NDEBUG
#include <unordered_map>
#endif

namespace gte::rg {

#ifndef NDEBUG

namespace {

// Debug-only name registry; never used for identity comparisons. A consteval operator""_passId cannot
// host a mutable static, so it lives here.
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

// FNV-1a: the view's hash first (fixed width), then the name bytes.
RenderPassId SceneResourceKeyOf(std::string_view name, RenderViewId view) noexcept
{
    std::uint64_t hash = 2166136261u;

    const std::uint64_t viewHash = view.Hash();
    unsigned char viewBytes[sizeof(viewHash)];
    std::memcpy(viewBytes, &viewHash, sizeof(viewHash));
    for (unsigned char b : viewBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }
    for (const char c : name) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 16777619u;
    }
    return RenderPassId{ hash };
}

RenderPassId NamedSceneResourceKey(const char* name, RenderViewId view) noexcept
{
    const bool nameIsEmpty = (name == nullptr || name[0] == '\0');

#ifndef NDEBUG
    static bool hasLoggedEmptyNameOnce = false;
    if (nameIsEmpty && !hasLoggedEmptyNameOnce) {
        std::fprintf(stderr,
            "NamedSceneResourceKey: called with a null/empty name - treating it as an empty-string key.\n");
        hasLoggedEmptyNameOnce = true;
    }
#endif

    const RenderPassId key = SceneResourceKeyOf(nameIsEmpty ? std::string_view{} : std::string_view(name), view);

#ifndef NDEBUG
    RegisterPassIdDebugName(key, nameIsEmpty ? "(unnamed)" : name);
#endif

    return key;
}

} // namespace gte::rg
