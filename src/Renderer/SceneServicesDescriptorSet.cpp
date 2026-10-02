#include "SceneServicesDescriptorSet.h"

#include <cstring>

namespace gte {

rg::RenderPassId SceneServiceBlackboardKey(SceneServiceSlot slot, rg::RenderViewId view) noexcept
{
    // Reuses the EXACT SAME FNV-1a offset-basis/prime this codebase's own
    // operator""_passId / RenderViewId::Named() already use (RenderPipeline.h)
    // - continuing ONE running accumulator across the 8 bytes of
    // view.Hash() immediately followed by the 4 bytes of
    // static_cast<std::uint32_t>(slot), exactly as mandated by the source
    // design doc (BLOCK4_GLOBAL_SCENE_SERVICES_DESCRIPTOR_SET.txt, Section 3,
    // "IMPLEMENTATION REQUIREMENT"). A weak combiner (plain XOR/addition)
    // can silently alias two DIFFERENT (slot, view) pairs onto the SAME key
    // for some unlucky pair of view hashes - this must be correct by
    // construction, not merely "probably fine against the views tested".
    std::uint64_t hash = 2166136261u;

    const std::uint64_t viewHash = view.Hash();
    unsigned char viewBytes[sizeof(viewHash)];
    std::memcpy(viewBytes, &viewHash, sizeof(viewHash));
    for (unsigned char b : viewBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    const std::uint32_t slotValue = static_cast<std::uint32_t>(slot);
    unsigned char slotBytes[sizeof(slotValue)];
    std::memcpy(slotBytes, &slotValue, sizeof(slotValue));
    for (unsigned char b : slotBytes) {
        hash ^= static_cast<std::uint64_t>(b);
        hash *= 16777619u;
    }

    return rg::RenderPassId{ hash };
}

} // namespace gte
