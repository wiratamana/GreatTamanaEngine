#include "RenderPipeline.h"

namespace gte::rg {

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

} // namespace gte::rg
