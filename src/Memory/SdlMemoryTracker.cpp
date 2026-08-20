#include "SdlMemoryTracker.h"

#include <atomic>

namespace gte {

namespace {

// Every tracked allocation is prefixed with this small header (holding the
// LOGICAL size the caller asked for, not including the header itself) so
// TrackedFree()/TrackedRealloc() know how many bytes to subtract from the
// running total - SDL_free_func only ever hands back the pointer, never a
// size. kHeaderSize is a full 16 bytes (not just sizeof(AllocHeader)) so the
// pointer handed back to SDL is offset by a multiple of this Windows
// target's guaranteed allocation alignment (the smaller of
// alignof(std::max_align_t) or 2*sizeof(void*) - see SDL_malloc()'s own doc
// comment, both 16 bytes on x64) - preserving whatever alignment guarantee
// the ORIGINAL allocator gave the untouched block.
struct AllocHeader {
    std::size_t size;
};
constexpr std::size_t kHeaderSize = 16;
static_assert(kHeaderSize >= sizeof(AllocHeader), "kHeaderSize must fit an AllocHeader.");

std::atomic<std::uint64_t> g_liveBytes{ 0 };
std::atomic<std::uint64_t> g_liveCount{ 0 };

SDL_malloc_func g_originalMalloc = nullptr;
SDL_calloc_func g_originalCalloc = nullptr;
SDL_realloc_func g_originalRealloc = nullptr;
SDL_free_func g_originalFree = nullptr;

// Writes `logicalSize` into the header at the START of `block` (a raw
// allocation of at least kHeaderSize + logicalSize bytes) and returns the
// user-facing pointer just past it.
void* HeaderEncode(void* block, std::size_t logicalSize) noexcept
{
    static_cast<AllocHeader*>(block)->size = logicalSize;
    return static_cast<char*>(block) + kHeaderSize;
}

// Inverse of HeaderEncode(): given a user-facing pointer, returns the
// original raw block pointer and reads back the logical size stored for it.
void* HeaderDecode(void* userPtr, std::size_t& outLogicalSize) noexcept
{
    void* block = static_cast<char*>(userPtr) - kHeaderSize;
    outLogicalSize = static_cast<AllocHeader*>(block)->size;
    return block;
}

} // namespace

void SdlMemoryTracker::Install()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    installed = true;

    SDL_GetOriginalMemoryFunctions(&g_originalMalloc, &g_originalCalloc, &g_originalRealloc, &g_originalFree);
    SDL_SetMemoryFunctions(&SdlMemoryTracker::TrackedMalloc, &SdlMemoryTracker::TrackedCalloc,
        &SdlMemoryTracker::TrackedRealloc, &SdlMemoryTracker::TrackedFree);
}

std::uint64_t SdlMemoryTracker::LiveBytes() noexcept
{
    return g_liveBytes.load(std::memory_order_relaxed);
}

std::uint64_t SdlMemoryTracker::LiveAllocationCount() noexcept
{
    return g_liveCount.load(std::memory_order_relaxed);
}

void* SdlMemoryTracker::TrackedMalloc(std::size_t size)
{
    std::size_t footprint = 0;
    if (!SDL_size_add_check_overflow(size, kHeaderSize, &footprint)) {
        return nullptr;
    }

    void* block = g_originalMalloc(footprint);
    if (block == nullptr) {
        return nullptr;
    }

    g_liveBytes.fetch_add(size, std::memory_order_relaxed);
    g_liveCount.fetch_add(1, std::memory_order_relaxed);
    return HeaderEncode(block, size);
}

void* SdlMemoryTracker::TrackedCalloc(std::size_t nmemb, std::size_t size)
{
    std::size_t total = 0;
    if (!SDL_size_mul_check_overflow(nmemb, size, &total)) {
        return nullptr;
    }
    std::size_t footprint = 0;
    if (!SDL_size_add_check_overflow(total, kHeaderSize, &footprint)) {
        return nullptr;
    }

    // Single-element calloc of the full (already-zeroed) footprint - avoids
    // re-deriving an nmemb/size split for the underlying allocator while
    // still guaranteeing zero-initialized memory for the caller's portion.
    void* block = g_originalCalloc(1, footprint);
    if (block == nullptr) {
        return nullptr;
    }

    g_liveBytes.fetch_add(total, std::memory_order_relaxed);
    g_liveCount.fetch_add(1, std::memory_order_relaxed);
    return HeaderEncode(block, total);
}

void* SdlMemoryTracker::TrackedRealloc(void* mem, std::size_t size)
{
    if (mem == nullptr) {
        return TrackedMalloc(size);
    }

    std::size_t oldSize = 0;
    void* oldBlock = HeaderDecode(mem, oldSize);

    std::size_t footprint = 0;
    if (!SDL_size_add_check_overflow(size, kHeaderSize, &footprint)) {
        return nullptr;
    }

    void* newBlock = g_originalRealloc(oldBlock, footprint);
    if (newBlock == nullptr) {
        return nullptr; // oldBlock/mem remains valid and untouched - matches SDL_realloc()'s own contract.
    }

    g_liveBytes.fetch_add(size, std::memory_order_relaxed);
    g_liveBytes.fetch_sub(oldSize, std::memory_order_relaxed);
    // Allocation count is unchanged - this is the same logical allocation, just resized.
    return HeaderEncode(newBlock, size);
}

void SdlMemoryTracker::TrackedFree(void* mem)
{
    if (mem == nullptr) {
        return;
    }

    std::size_t size = 0;
    void* block = HeaderDecode(mem, size);

    g_liveBytes.fetch_sub(size, std::memory_order_relaxed);
    g_liveCount.fetch_sub(1, std::memory_order_relaxed);
    g_originalFree(block);
}

} // namespace gte
