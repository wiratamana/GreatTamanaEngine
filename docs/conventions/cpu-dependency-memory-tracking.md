# CPU Dependency Memory Tracking

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

Alongside [`GpuMemoryTracker`](gpu-resource-memory-tracking.md) (above), the
engine also tracks how much CPU (host) memory its own third-party
dependencies are using - `SdlMemoryTracker`
(`src/Memory/SdlMemoryTracker.h`, always compiled) for SDL, and
`ImGuiMemoryTracker` (`src/Editor/ImGuiMemoryTracker.h`, Editor-only) for Dear
ImGui - both surfaced by the Editor's "Memory" panel
(`src/Editor/Panels/MemoryPanel.cpp`) as their own named CPU buckets,
alongside `Renderer::GetVmaHeapBudgets()` (the real, driver-reported GPU heap
usage/budget, distinct from `GpuMemoryTracker`'s own tally). Follow these
rules whenever touching this code or adding a tracker for a future
dependency:

- **Install the tracking allocator before the dependency's first call of any
  kind, not just before some "main" entry point.** Both SDL
  (`SDL_SetMemoryFunctions()`) and Dear ImGui
  (`ImGui::SetAllocatorFunctions()`) document this same constraint: swapping
  allocators after the library has already allocated something risks a later
  free using a DIFFERENT allocator than whatever alloc call originally served
  that pointer. `SdlMemoryTracker::Install()` is called at the very top of
  `Application::SdlContext`'s constructor (`Application.cpp`), before
  `SDL_Init()`; `ImGuiMemoryTracker::Install()` is called at the very top of
  `ImGuiEditorLayer`'s constructor (`ImGuiEditorLayer.cpp`), before
  `ImGui::CreateContext()`. A future tracker for a new dependency must find
  and hook that same "first call" point, not an approximate/later one.
- **The production `Install()` call site must itself be gated behind
  `#if GTE_ENABLE_EDITOR`, even if the tracker CLASS compiles in every
  build.** These trackers exist purely to feed the Editor's "Memory" panel -
  a release/shipped build has no panel to display them and must not pay
  their real per-allocation cost (an extra header write + atomic increment
  on EVERY single alloc/free of that dependency, for the rest of the
  process's lifetime) for nothing. `ImGuiMemoryTracker::Install()` gets this
  for free (its call site, `ImGuiEditorLayer`'s constructor, is only ever
  compiled when `GTE_ENABLE_EDITOR` is ON in the first place - see
  [Editor Module Structure](editor-module-structure.md)). `SdlMemoryTracker::Install()`
  does NOT get this for
  free, since `Application.cpp` compiles in every build - its call site in
  `Application::SdlContext`'s constructor is explicitly wrapped in
  `#if GTE_ENABLE_EDITOR` for exactly this reason. A future tracker for a
  dependency used outside `src/Editor/` (i.e. one whose install call site
  isn't naturally Editor-only compiled) must add this same explicit guard at
  its call site - don't assume "the class only matters to the Editor" is
  enough on its own to keep it out of a release build's runtime behavior.
- **`Install()` must be idempotent.** Both trackers guard themselves with a
  local `static bool installed` - calling `Install()` more than once (e.g.
  from a test, or if a future call site is added) is always a safe no-op, so
  no caller ever needs to guard its own call site.
- **These are necessarily static/process-global, not instance-based like
  `GpuMemoryTracker`.** `SDL_malloc_func`/`SDL_free_func` and
  `ImGuiMemAllocFunc`/`ImGuiMemFreeFunc` are plain C function pointers with
  no (or, for ImGui, an engine-unused) userdata slot to stash a `this` in -
  there is nowhere else the byte/count totals could live. This matches a
  constraint SDL's own `SDL_GetNumAllocations()` already has.
- **A hidden per-allocation header carries the LOGICAL size, since free-side
  callbacks are only ever handed the pointer, never a size.** Both trackers
  use the same fixed 16-byte header trick (see `SdlMemoryTracker.cpp`'s
  `AllocHeader`/`HeaderEncode()`/`HeaderDecode()`) - 16 bytes because that is
  this Windows target's guaranteed allocation alignment (the smaller of
  `alignof(std::max_align_t)` or `2*sizeof(void*)`), so offsetting the
  underlying allocator's own aligned block by exactly that many bytes
  preserves its alignment guarantee for the pointer handed back to the
  caller. A future tracker copying this pattern must keep the header size a
  multiple of that alignment, not just `sizeof(size_t)`.
- **Both trackers are genuinely Tier-1-testable despite touching a
  third-party library's own allocator.** Neither `SDL_malloc()`/`SDL_free()`
  nor `ImGui::MemAlloc()`/`MemFree()` need `SDL_Init()`, a live window, or an
  `ImGuiContext` to be called safely - see
  `tests/Memory/SdlMemoryTrackerTests.cpp`/
  `tests/Editor/ImGuiMemoryTrackerTests.cpp` for the pattern: capture
  `LiveBytes()`/`LiveAllocationCount()` BEFORE each test's own alloc/free
  calls and assert on the DELTA, never an assumed absolute baseline, since
  `Install()`'s process-global state persists across every test in the same
  binary.
