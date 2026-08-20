#include "ImGuiMemoryTracker.h"

#include <imgui.h>

#include <atomic>
#include <cstdlib>
#include <limits>

namespace gte {

namespace {

// Same "hidden header" trick as SdlMemoryTracker (src/Memory/
// SdlMemoryTracker.cpp) - see its comment for the full rationale. Dear
// ImGui's own default allocator is just plain malloc/free (see imgui.cpp),
// so that's what this wraps directly - there is no ImGui equivalent of
// SDL_GetOriginalMemoryFunctions() to query instead.
struct AllocHeader {
    std::size_t size;
};
constexpr std::size_t kHeaderSize = 16;
static_assert(kHeaderSize >= sizeof(AllocHeader), "kHeaderSize must fit an AllocHeader.");

std::atomic<std::uint64_t> g_liveBytes{ 0 };
std::atomic<std::uint64_t> g_liveCount{ 0 };

void* HeaderEncode(void* block, std::size_t logicalSize) noexcept
{
    static_cast<AllocHeader*>(block)->size = logicalSize;
    return static_cast<char*>(block) + kHeaderSize;
}

void* HeaderDecode(void* userPtr, std::size_t& outLogicalSize) noexcept
{
    void* block = static_cast<char*>(userPtr) - kHeaderSize;
    outLogicalSize = static_cast<AllocHeader*>(block)->size;
    return block;
}

} // namespace

void ImGuiMemoryTracker::Install()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    ImGui::SetAllocatorFunctions(&ImGuiMemoryTracker::TrackedAlloc, &ImGuiMemoryTracker::TrackedFree, nullptr);
}

std::uint64_t ImGuiMemoryTracker::LiveBytes() noexcept
{
    return g_liveBytes.load(std::memory_order_relaxed);
}

std::uint64_t ImGuiMemoryTracker::LiveAllocationCount() noexcept
{
    return g_liveCount.load(std::memory_order_relaxed);
}

void* ImGuiMemoryTracker::TrackedAlloc(std::size_t size, void* /*userData*/)
{
    if (size > std::numeric_limits<std::size_t>::max() - kHeaderSize) {
        return nullptr; // Would overflow the footprint computation below.
    }

    void* block = std::malloc(size + kHeaderSize);
    if (block == nullptr) {
        return nullptr;
    }

    g_liveBytes.fetch_add(size, std::memory_order_relaxed);
    g_liveCount.fetch_add(1, std::memory_order_relaxed);
    return HeaderEncode(block, size);
}

void ImGuiMemoryTracker::TrackedFree(void* ptr, void* /*userData*/)
{
    if (ptr == nullptr) {
        return;
    }

    std::size_t size = 0;
    void* block = HeaderDecode(ptr, size);

    g_liveBytes.fetch_sub(size, std::memory_order_relaxed);
    g_liveCount.fetch_sub(1, std::memory_order_relaxed);
    std::free(block);
}

} // namespace gte
