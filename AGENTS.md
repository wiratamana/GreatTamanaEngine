# AGENTS.md

Instructions for LLM/AI agents working on this codebase.

## Coding Guidelines

- **Clean Architecture**: Write clean architecture code. Keep clear
  separation of concerns between layers (e.g. SDL -> Application -> Window
  and Renderer -> Game). Lower-level/core layers must not depend on
  higher-level or framework-specific details. Only the `Application` layer
  should know about SDL directly; other layers must go through the custom
  abstraction objects (Window, Renderer, etc.).
- **RAII**: Every resource-owning piece of code must use RAII (Resource
  Acquisition Is Initialization). Resources (SDL handles, memory, file
  handles, GPU objects, etc.) must be acquired in constructors and released
  in destructors, so lifetime is tied to object scope and cleanup is
  automatic and exception/error-safe. Avoid manual/explicit cleanup calls
  scattered through the code — wrap raw resources in owning types instead.
- **Namespace**: Every new script (every class/function/type this project
  defines) must live inside the `gte` namespace (short for Great Tamana
  Engine), e.g. `namespace gte { class Window { ... }; }`. This keeps engine
  symbols from colliding with SDL's or third-party globals.

## GPU Resource Memory Tracking

Every GPU resource type (`Buffer`, `RenderTexture`, and any future type -
vertex/index/uniform buffers, textures, etc.) must register with
`GpuMemoryTracker` (`src/Renderer/Memory/GpuMemoryTracker.h`) so the engine
always has an accurate, live picture of exactly what GPU memory is
allocated, of what kind, and where - a Unity-Memory-Profiler-style live
object registry, not just an aggregate byte counter. Follow these rules
whenever touching GPU resource lifetime code:

- **Identify resources by handle, never by pointer or string.**
  `GpuResourceHandle` is a cheap 8-byte POD (index + generation), generated
  automatically by `GpuMemoryTracker::Track()` - calling code never
  invents/assigns its own id. Handles are meant to be copied/compared/
  stored by the thousands with no real cost, unlike a `std::string`, which
  is comparatively large and unpredictable memory-wise.
- **The tracked record must always reflect the CURRENT actual allocation -
  never a stale, construction-time snapshot.** Any lifecycle method that
  destroys and recreates a resource's underlying VMA allocation (e.g.
  `RenderTexture::Resize()`, which internally does `Destroy()` +
  `Create()`) is creating a genuinely new allocation, and MUST `Untrack()`
  the old handle and `Track()` a fresh one reflecting the new size/location
  as part of that same operation. A resource's `Handle()` is therefore NOT
  guaranteed stable across its lifetime - only guaranteed valid for
  whatever the resource's CURRENT allocation actually is. Never assume a
  handle captured once stays correct after a resize/recreate; always read
  `Handle()` again afterwards if you need it. This was verified with a
  dedicated runtime test (create -> resize -> confirm the old handle is
  gone, the new one is tracked, and the byte count reflects the new size,
  with no duplicate/leaked entry) - re-verify this way whenever this code
  path changes.
- **Track the size VMA actually gave you, not the size you requested.**
  Use the `VmaAllocationInfo::size` returned by `vmaCreateBuffer`/
  `vmaCreateImage` (VMA may allocate more than requested due to alignment),
  and classify the real memory location via `ClassifyGpuMemoryLocation()`
  (reads the allocation's actual `VkMemoryPropertyFlags` from VMA) rather
  than assuming it matches whatever `BufferMemoryUsage` was requested -
  VMA's actual choice can legitimately differ (e.g. falling back to plain
  host-visible system RAM instead of a shared device-local+host-visible
  heap).
- **Human-readable debug names are Editor-only and live in a completely
  separate table from the hot resource record.** Pass names as a plain
  `const char*` (never `std::string`) through an optional `debugName`
  parameter, and only ever store/attach them via
  `GpuMemoryTracker::SetDebugName()`, which is guarded by
  `#if GTE_ENABLE_EDITOR` in `GpuMemoryTracker.h` - this compiles the name
  table out ENTIRELY (not just unused) in a non-Editor/release build, so a
  shipped game carries zero string cost for this. Never add a name/string
  field to `GpuResourceRecord` itself. If a resource's debug name must
  survive a resize/recreate (see above), store the `const char*` on the
  resource itself and re-apply it via `SetDebugName()` every time it
  re-tracks - this requires the caller-supplied string to have static
  storage duration (e.g. a string literal), since only the pointer is kept,
  not a copy.
- **Own the tracker via `std::shared_ptr`, never a raw pointer/reference.**
  `Renderer` owns the one `GpuMemoryTracker` and hands a `shared_ptr` copy
  to every `Buffer`/`RenderTexture` it creates, so tracking stays valid no
  matter how `Renderer`/`VulkanAllocator` get moved later - a raw
  pointer/reference into `VulkanAllocator` or `Renderer` itself would risk
  dangling after a move (the underlying Vulkan handles survive moves fine,
  but the C++ wrapper objects can relocate). Any new GPU resource type
  added later should follow this same pattern, not invent its own.

This document will be extended as more conventions are established.
