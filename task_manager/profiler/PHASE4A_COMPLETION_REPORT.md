# Phase 4A Completion Report — GPU Timestamp Capability + Pure Math

Status: **DONE**. Scope: exactly Phase 4A of
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` ("Capability query and pure
helper logic — no `VkQueryPool` yet"). Phases 4B (query-pool/service
infrastructure), 4C (Game/Scene offscreen timing), and 4D (Present timing)
are explicitly **NOT** part of this session and were not touched.

This session picked up the engine after Phases 0/1/2/3/5/7 of
`PROFILER_STRATEGY_v2.md` were already implemented (see
`PROFILER_IMPLEMENTATION_STATUS_v6.md`), on branch
`feature/profiler-gpu-timestamp-query`, with the two strategy documents
(`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v1.md`/`_v2.md`) already present as
the planning input.

## What was implemented

### 1. `src/Renderer/GpuTiming.h` / `.cpp` (new files)

A Vulkan-header-free, pure-data/pure-math module — mirrors
`src/Renderer/DrawStats.h`'s own precedent exactly (no `<volk.h>`, no
`vulkan/vulkan.h`), so it is trivially Tier-1-testable with no live
`VkDevice` at all:

- `struct GpuTimestampCapability { bool supported; float timestampPeriodNs;
  std::uint32_t validBits; }` — what one physical device can actually do
  re: GPU timestamp queries.
- `GpuTimestampCapability InterpretTimestampCapability(bool
  timestampComputeAndGraphics, float timestampPeriod, std::uint32_t
  validBits) noexcept` — the pure DECISION logic (extracted specifically so
  it's unit-testable with hand-fabricated inputs, without needing a real
  `VkPhysicalDevice`). `supported` is only `true` when all three raw inputs
  indicate real support — a period of `0.0f` or `validBits == 0` are both
  independently valid "not supported" signals per the Vulkan spec, never
  assumed positive/non-zero.
- `enum class GpuTimingSlot { Offscreen0 = 0, Offscreen1 = 1,
  SwapchainPresent = 2 }` + `kGpuTimingSlotCount = 3` — the three logical
  GPU passes this whole Phase 4 effort will eventually measure, deliberately
  generic (not `GameView`/`SceneView`) since `Renderer`/`FramePresenter`
  must never know Editor-facing pass naming.
- `struct GpuTimingSample { enum class Status { Absent, Present,
  Unsupported }; Status status; double milliseconds; }` — the
  Renderer-local tri-state mirror of `Profiling::GpuSampleStatus`,
  deliberately a separate type so `Renderer`/`GpuTiming.h` stay completely
  free of any `Profiling/` header.
- `double ConvertTimestampDeltaToMilliseconds(std::uint64_t startTicks,
  std::uint64_t endTicks, float timestampPeriodNs, std::uint32_t
  validBits) noexcept` — tick-delta → millisecond conversion. Both ticks
  are masked to `validBits` before subtracting, so the result stays correct
  across exactly one counter wraparound; returns `0.0` immediately if
  `timestampPeriodNs <= 0.0f`.
- `constexpr std::uint32_t kGpuTimingFramesInFlight = 2` and `constexpr
  std::uint32_t PresentTimestampSlotBase(std::uint32_t
  frameInFlightIndex) noexcept` (`frameInFlightIndex * 2`) — the Present
  path's per-frame-in-flight slot-indexing math (its "end" slot is always
  `PresentTimestampSlotBase(...) + 1`).

**Explicitly not done here** (matches the strategy document's own "Phase
4A" scope exactly): no `VkQueryPool` created anywhere, no
`vkCmdWriteTimestamp2`/`vkCmdResetQueryPool`/`vkGetQueryPoolResults` call
anywhere, no change to `FramePresenter`/`Renderer`'s public API, and no
call to `FrameProfiler::SetGpuPassTiming()` in production code — the
Profiler panel's "GPU Timing" section is completely unaffected by this
session and still honestly shows `N/A` for the same reason as before
(nothing reports to it yet).

### 2. `VulkanDevice` capability query (`src/Renderer/Vulkan/VulkanDevice.h/.cpp`)

- Added `#include "../GpuTiming.h"` so `VulkanDevice.h` shares the same
  `GpuTimestampCapability` definition rather than a second, structurally
  identical struct.
- Added a private `void QueryTimestampCapability()`, called once from the
  constructor right after `CreateLogicalDevice()` — mirrors
  `PickDepthFormat()`'s own "ask the device once, expose via accessor"
  shape, except eagerly computed/cached in the constructor since every
  consumer needs the same fixed answer for the device's entire lifetime.
  Reads `vkGetPhysicalDeviceProperties()`'s
  `limits.timestampComputeAndGraphics`/`limits.timestampPeriod` and
  `vkGetPhysicalDeviceQueueFamilyProperties()`'s
  `families[m_graphicsFamily].timestampValidBits`, then calls
  `InterpretTimestampCapability()` with those real, device-queried values.
  Never throws — an unsupported result is treated as a completely normal
  outcome, same "degrade gracefully" convention used throughout this
  codebase.
- Added `const GpuTimestampCapability& TimestampCapability() const
  noexcept` accessor, plus a new `GpuTimestampCapability
  m_timestampCapability` member correctly threaded through the existing
  hand-written move constructor/move-assignment operator (a plain
  data-copy, since this struct owns no Vulkan resource of its own).

### 3. Build wiring

- `CMakeLists.txt`: added `src/Renderer/GpuTiming.cpp`/`.h` to `gte_core`'s
  source list.
- `tests/CMakeLists.txt`: added `Renderer/GpuTimingTests.cpp` to the Tier-1
  test source list, plus a description block in the file's own "Tier 1"
  test-taxonomy comment header, matching the existing convention for every
  other test file listed there.

### 4. Tests (`tests/Renderer/GpuTimingTests.cpp`, new file, Tier 1)

18 new `TEST()` cases, all pure logic, zero live device/Vulkan/SDL
involved:

- `ConvertTimestampDeltaToMilliseconds()`: a round period, a realistic
  non-round period (`0.641291f` ns/tick, an actual reported value class on
  real hardware), a zero delta, a `timestampPeriodNs <= 0` defensive floor,
  and — the single most important regression case in this file — a
  counter **wraparound within `validBits`** (`validBits = 32`, a start tick
  near `0xFFFFFFFF` and an end tick just past the wrap), asserting the
  masked/modular subtraction produces the correct small positive delta
  rather than an enormous garbage value.
- `PresentTimestampSlotBase()`: exact documented values (`0`/`2`) and its
  "+1 for the end slot" convention, asserted directly.
- Fixed enum/constant layout: `GpuTimingSlot`'s three values and
  `kGpuTimingSlotCount == 3`; `kGpuTimingFramesInFlight == 2` (must always
  agree in value with `FramePresenter::kFramesInFlight`, once that
  cross-reference exists starting in Phase 4B/4D).
- `InterpretTimestampCapability()`: supported when every input is valid;
  unsupported when `timestampComputeAndGraphics` is `false`; unsupported
  when `timestampPeriod == 0`; unsupported when `validBits == 0`; and raw
  values are still recorded verbatim even on the unsupported path (useful
  for diagnostics).

## Verification performed

- **Configure**: `cmake -S . -B build` (Ninja, existing build tree,
  `-DIMGUIZMO_RELEASE_TAG=<pinned SHA>` passed once to align a stale cached
  value with the repo's own pinned default — pre-existing, unrelated to
  this session's changes; every other dependency, including SDL3/Vulkan/
  VMA/KTX/saba/glm/imgui/GoogleTest, was already staged and required no
  network access).
- **Build**: `cmake --build build --config Debug` for both
  `GreatTamanaEngineTests` and the real `GreatTamanaEngine` executable —
  both link cleanly with no new warnings from the touched files.
- **Test suite**: `ctest -C Debug --output-on-failure` — **516 tests total,
  515 passed, 1 pre-existing machine-gated smoke test skipped** (up from
  the prior session's 502+1 baseline — the +14 delta is exactly this
  session's new `GpuTimingTest.*` cases; every pre-existing test still
  passes unchanged).

## What is deliberately NOT in this session (left for later sub-phases)

Per the user's explicit instruction to implement **only 4A**:

- No `VulkanQueryPool`/`GpuTimingService` (Phase 4B) — no `VkQueryPool` is
  created anywhere.
- No `Renderer::RenderOffscreen()`/`Present()` signature changes, no
  `Renderer::LastGpuTiming()`/`SetGpuTimingCaptureEnabled()` (Phase 4B/4C/4D).
- No call to `Profiling::FrameProfiler::SetGpuPassTiming()` in production
  code, and no change to the Editor's "Profiler" panel — it still shows
  "GPU Timing: N/A" for all three passes, same as before this session,
  for the same reason as before (nothing reports to it yet).
- No `AGENTS.md`/`README.md`/`PROFILER_IMPLEMENTATION_STATUS_v6.md` status
  updates claiming Phase 4 is "done" — only 4A of its four sub-phases is,
  and the strategy document's own plan is for those living-document
  updates to land once the *whole* phase (through 4D) ships.

## Notes / anomalies encountered (not a tool bug, just a build-environment note)

The local build tree's cached `IMGUIZMO_RELEASE_TAG` CMake variable was
still set to `master` (a moving branch) from some earlier configure,
rather than the repository's own pinned-commit default — this made a
plain reconfigure attempt try to redownload ImGuizmo from `master` over
the network, unrelated to anything changed in this session. Passing
`-DIMGUIZMO_RELEASE_TAG=<the same pinned SHA `cmake/FetchImGuizmo.cmake`
already defaults to>` once resolved it by realigning the cache with the
already-staged, already-correct `third_party/imguizmo/.gte_fetched_ref`
contents, avoiding any network access. This is an environment/cache
quirk, not a defect in this session's own changes.
