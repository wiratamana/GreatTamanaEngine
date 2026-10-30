# Memory tracking
> Rules for the three memory trackers (GPU, SDL, ImGui) behind the editor "Memory" panel. For anyone who adds a GPU resource type or a new tracker.
> Verified against code: 2026-10-30

## TL;DR
- Every GPU `Buffer` and `Texture` registers with `GpuMemoryTracker`. The tracker keeps live records, not only a byte total.
- SDL and ImGui allocations are counted by two static trackers. They hook the library allocator.
- A tracker must be installed before the first call into its library.
- The "Memory" panel shows all three, plus the real GPU heap budget from `Renderer::GetVmaHeapBudgets()`.

## Overview
| Tracker | File | Kind | Installed in |
|---|---|---|---|
| `GpuMemoryTracker` | `src/Renderer/Memory/GpuMemoryTracker.h` | Instance, owned by `Renderer`, shared by `shared_ptr` | `Renderer` |
| `EditorGpuMemoryNameOverlay` | `src/Editor/EditorGpuMemoryNameOverlay.h` | Static. Holds debug names by handle | `CreateEditorLayer()` |
| `SdlMemoryTracker` | `src/Memory/SdlMemoryTracker.h` | Static | `EditorHost::SdlContext`, before `SDL_Init` |
| `ImGuiMemoryTracker` | `src/Editor/ImGuiMemoryTracker.h` | Static | `ImGuiEditorLayer` constructor, before `ImGui::CreateContext` |

## Rules: GPU resources
1. NEVER invent a resource id. `GpuMemoryTracker::Track()` returns a `GpuResourceHandle` (8 bytes: `index`, `generation`).
2. Call `Untrack(handle)` when the resource dies.
3. A recreate is a new allocation. `Untrack` the old handle and `Track` a new one. A handle is NOT stable over a resize.
4. Track the size VMA gave you (`VmaAllocationInfo::size`), not the size you asked for.
5. Get the memory location from `ClassifyGpuMemoryLocation()`. VMA may pick another heap than requested.
6. Never store a name in `GpuResourceRecord`. Call `SetDebugName(handle, "literal")`. The name must be a string with static lifetime.
7. Re-apply the debug name every time a resource re-tracks.
8. Own the tracker with `std::shared_ptr`. Never keep a raw pointer into `Renderer`.

## Rules: CPU trackers
1. Install before the first call of the library (`SDL_SetMemoryFunctions`, `ImGui::SetAllocatorFunctions`).
2. `Install()` must be safe to call twice.
3. These trackers are static, because the C allocator callbacks have no user data.
4. Each allocation gets a 16-byte header with the logical size. The free callback only gets a pointer.
5. A new header must stay a multiple of the allocator alignment, not only `sizeof(size_t)`.
6. `SdlMemoryTracker::Install()` runs in every build. There is no editor-only switch.

## Do and Don't
| Do | Don't |
|---|---|
| `Track` and `Untrack` in the resource constructor and destructor | Count bytes in your own global variable |
| Read `Handle()` again after a resize | Cache a handle across `Resize()` |
| Pass `const char*` literals as debug names | Pass `std::string` or a temporary |
| Assert on deltas in tests | Assert on an absolute `LiveBytes()` baseline |

## How to track a new GPU resource type
1. Take `std::shared_ptr<GpuMemoryTracker>` in the constructor.
2. After the VMA allocation, call `Track(type, location, allocationInfo.size, format)`.
3. Call `SetDebugName(handle, name)` right after.
4. Call `Untrack(handle)` in the destructor and before any recreate.
5. Add a test in `tests/Memory/GpuMemoryTrackerTests.cpp`.

## Checks
- `tests/Memory/GpuMemoryTrackerTests.cpp`, `tests/Memory/SdlMemoryTrackerTests.cpp`, `tests/Editor/ImGuiMemoryTrackerTests.cpp`.
- SDL and ImGui allocators work without `SDL_Init` or an ImGui context, so these tests need no window.
- Test pattern: read `LiveBytes()` and `LiveAllocationCount()` first, then allocate and free, then check the change.

## Gotchas
- `GpuMemoryTracker` is not thread safe. Call it from the main thread only.
- The overlay must be installed before the first named GPU resource exists. Later resources get no name.
- Totals from `GetTotals()` are the engine's own tally. VMA heap budgets are the driver's numbers. They differ.
- `LiveBytes()` is 0 when `Install()` was never called.

## Related
- [Job system](job-system.md) for the thread safety table
- [Editor](../architecture/editor.md) for the Memory panel
- [Contributing](contributing.md)
