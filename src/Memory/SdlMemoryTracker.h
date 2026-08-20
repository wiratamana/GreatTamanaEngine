#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstdint>

namespace gte {

// Byte-counting wrapper around SDL's own malloc/calloc/realloc/free (see
// SDL_SetMemoryFunctions(), SDL3/SDL_stdinc.h) - the CPU-memory counterpart
// to GpuMemoryTracker (src/Renderer/Memory/GpuMemoryTracker.h): "how much
// memory is SDL itself using right now", surfaced by the Editor's "Memory"
// panel (src/Editor/Panels/MemoryPanel.cpp) as its own named bucket,
// separate from GPU memory and from the engine's general heap usage.
//
// MUST be installed before the very first SDL call of any kind - see
// SDL_SetMemoryFunctions()'s own doc comment ("usually this needs to be the
// first call made into the SDL library"): swapping allocators after SDL has
// already allocated something risks a later SDL_free() using a DIFFERENT
// allocator than whatever SDL_malloc() originally served that pointer.
// Application::SdlContext's constructor (Application.cpp) - the one place
// this engine ever calls SDL_Init() - installs this first, before SDL_Init()
// itself, for exactly this reason - but ONLY inside an `#if GTE_ENABLE_EDITOR`
// block: a release build's Application never calls Install() at all, so SDL
// uses its own untouched default allocator with zero interception and zero
// per-allocation overhead, exactly as if this class didn't exist. The class
// itself still compiles in every build (see below) purely so it stays
// available/testable in a GTE_ENABLE_EDITOR=OFF configuration too (see
// tests/Memory/SdlMemoryTrackerTests.cpp, which is NOT Editor-gated) -
// compiling does not mean installed/active. See AGENTS.md ("CPU Dependency
// Memory Tracking") for this exact rule.
//
// Not an instance/RAII type like GpuMemoryTracker: SDL_malloc_func and
// friends (SDL3/SDL_stdinc.h) carry no userdata parameter, so there is
// nowhere to stash a `this` pointer - the counters here are necessarily
// static/process-global, the same constraint SDL's own
// SDL_GetNumAllocations() already has. Always compiled (unlike
// ImGuiMemoryTracker, src/Editor/ImGuiMemoryTracker.h, whose install call
// site already naturally lives in Editor-only compiled code) since SDL
// itself is used regardless of GTE_ENABLE_EDITOR.
class SdlMemoryTracker {
public:
    // Installs the tracking allocator via SDL_SetMemoryFunctions(). Safe to
    // call more than once - every call after the first is a no-op, so
    // callers never need to guard their own call site.
    static void Install();

    // Live totals across every still-outstanding SDL_malloc/calloc/realloc
    // allocation - O(1), safe to call every frame (e.g. from the Editor's
    // "Memory" panel). Both are 0 if Install() was never called (SDL's
    // default allocator is used, untracked).
    static std::uint64_t LiveBytes() noexcept;
    static std::uint64_t LiveAllocationCount() noexcept;

private:
    static void* SDLCALL TrackedMalloc(std::size_t size);
    static void* SDLCALL TrackedCalloc(std::size_t nmemb, std::size_t size);
    static void* SDLCALL TrackedRealloc(void* mem, std::size_t size);
    static void SDLCALL TrackedFree(void* mem);
};

} // namespace gte
