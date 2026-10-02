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

// Reuses the same FNV-1a constants/byte order SceneServiceBlackboardKey()
// (Renderer/SceneServicesDescriptorSet.cpp) established - the view's own
// Hash() folded first (fixed-width segment), then `name` folded byte-by-byte
// (variable-length segment last). A null/empty name is treated as an empty
// string and never crashes; it is logged once per process lifetime in debug
// builds only, since it almost always indicates a caller bug.
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

    std::uint64_t hash = 2166136261u;

    const std::uint64_t viewHash = view.Hash();
    unsigned char viewBytes[sizeof(viewHash)];
    std::memcpy(viewBytes, &viewHash, sizeof(viewHash));
    for (unsigned char b : viewBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    if (!nameIsEmpty) {
        for (const char* p = name; *p != '\0'; ++p) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(*p));
            hash *= 16777619u;
        }
    }

    const RenderPassId key{ hash };

#ifndef NDEBUG
    RegisterPassIdDebugName(key, nameIsEmpty ? "(unnamed)" : name);
#endif

    return key;
}

} // namespace gte::rg
