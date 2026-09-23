#include "EditorGpuMemoryNameOverlay.h"

#include <cstdint>
#include <unordered_map>

namespace gte {

namespace {

// Packs a GpuResourceHandle into a single 64-bit key - mirrors
// GpuMemoryTracker's own former internal PackHandle() exactly (index in the
// high 32 bits, generation in the low 32 bits), now living here instead
// since this is the only remaining place that needs it.
std::uint64_t PackHandle(GpuResourceHandle handle) noexcept
{
    return (static_cast<std::uint64_t>(handle.index) << 32) | handle.generation;
}

// Function-local static (Meyer's singleton) - avoids static-init-order
// concerns entirely, same precedent as Core/LogSink.cpp's own installed-sink
// storage.
std::unordered_map<std::uint64_t, std::string>& Names()
{
    static std::unordered_map<std::uint64_t, std::string> names;
    return names;
}

} // namespace

void EditorGpuMemoryNameOverlay::Install(GpuMemoryTracker& tracker)
{
    tracker.SetDebugNameObserver(&EditorGpuMemoryNameOverlay::OnDebugNameObserved, nullptr);
}

const std::string& EditorGpuMemoryNameOverlay::GetDebugName(GpuResourceHandle handle)
{
    static const std::string kEmpty;
    const auto it = Names().find(PackHandle(handle));
    return it != Names().end() ? it->second : kEmpty;
}

void EditorGpuMemoryNameOverlay::ResetForTesting()
{
    Names().clear();
}

void EditorGpuMemoryNameOverlay::OnDebugNameObserved(void* /*userData*/, GpuResourceHandle handle, const char* name)
{
    if (name == nullptr) {
        // GpuMemoryTracker::Untrack()'s own "forget this handle" signal
        // (see GpuMemoryTracker.h's DebugNameObserver doc comment).
        Names().erase(PackHandle(handle));
        return;
    }
    Names()[PackHandle(handle)] = name;
}

} // namespace gte
