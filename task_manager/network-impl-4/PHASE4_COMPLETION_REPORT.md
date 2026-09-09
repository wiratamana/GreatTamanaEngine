# PHASE4 — Completion Report (`network-impl-4`)

Implements `PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT.md` in full (the
second-iteration-audited revision) — `FrameCaptureBridge` gains a third
capture kind, `FrameCaptureKind::NamedTexture`, carrying a dynamic
`(textureName, channel)` request payload, and `Application::Run()` gains the
per-frame wiring that services it via the registry/readback/encode primitives
Phases 2/3 already built. This is a real, buildable code change, not a
plan/description.

## 1. Pre-implementation re-verification (as required by the task brief)

Before editing anything, the real, live shape of everything this phase
touches was re-read directly from the source tree, matching the phase
document's own (already twice-audited) findings exactly:

- `src/Application/FrameCaptureBridge.h` — confirmed only two existing
  `FrameCaptureKind` enumerators (`Swapchain`/`GameView`), `CapturedPngImage`'s
  exact three current fields, `RequestCaptureAndWait()`'s exact current
  signature, and confirmed **`<string>` was genuinely NOT included** (only
  `<condition_variable>`/`<cstdint>`/`<mutex>`/`<optional>`/`<vector>`) —
  fixed per the phase document's own required addition.
- `src/Application/FrameCaptureBridge.cpp` — confirmed `SlotFor()` (both
  overloads) is genuinely a two-way ternary, not a switch, exactly as the
  phase document's own audit note describes — rewritten into the documented
  three-way `switch` with a defensive post-switch fallback return (no
  `default:` case, matching this codebase's own exhaustive-switch
  convention).
- `src/Application/Application.cpp` — confirmed the exact placement the
  phase document specifies (after `m_editorLayer->RenderPlatformWindows();`,
  before `Profiling::FrameProfiler::Instance().SetMemorySnapshot(...)`) is
  still accurate against the live file, confirmed `m_renderer`/`m_renderGraph`
  are both plain (non-pointer) `Application` members already in scope there,
  confirmed `IsBgraFormat()` is an anonymous-namespace function already
  reachable from later in the same translation unit, and confirmed neither
  `RenderGraphDebugTextureRegistry.h` nor `DepthVisualization.h` was
  previously included.
- `src/Renderer/RenderGraph/RenderGraph.h`/
  `RenderGraphDebugTextureRegistry.h` — confirmed
  `DebugTextureSnapshotFor()`/`CurrentDebugTextureFrameCounter()`'s exact
  signatures and confirmed `DebugTextureSnapshot`'s exact fields
  (`target`/`hasDepth`/`colorState`/`depthState`/`lastUpdatedFrameCounter`).
- `src/Renderer/Renderer.h` — confirmed `CaptureImagePixels()`/
  `WaitForGpuIdle()`'s exact Phase-3 signatures and `CapturedRawPixels`'s
  exact fields.
- `src/Encoding/DepthVisualization.h` — confirmed
  `ConvertDepthToGrayscaleRgba8()`'s exact signature and its documented
  "returns `false`, leaves `outRgba8` untouched, for an unrecognized format"
  contract.

No deviation from the phase document's own (already twice-audited) findings
was discovered — every "confirmed live" claim held up exactly as written.

## 2. Files edited

- `src/Application/FrameCaptureBridge.h`:
  - Added `#include <string>`.
  - Added `FrameCaptureKind::NamedTexture` (third enumerator).
  - Added the new `DebugTextureChannel` enum (`Color`/`Depth`).
  - `CapturedPngImage` gained the new, defaulted `std::uint64_t
    framesSinceUpdate = 0;` field — the two existing `Swapchain`/`GameView`
    `FulfillPendingRequest()` call sites in `Application.cpp` compile
    unchanged.
  - `RequestCaptureAndWait()` gained two new, defaulted, trailing parameters
    (`const std::string& textureName = ""`, `DebugTextureChannel channel =
    DebugTextureChannel::Color`) — both existing `NetworkServer.cpp` call
    sites are unaffected.
  - Added `RequestedTextureName()`/`RequestedTextureChannel()` public
    accessors, plus their doc comments (including the phase document's own
    "three separate accessors, analyzed as harmless" note).
  - Added `m_namedTextureSlot`, `m_requestedTextureName`,
    `m_requestedTextureChannel` private members.
- `src/Application/FrameCaptureBridge.cpp`:
  - Rewrote both `SlotFor()` overloads from the two-way ternary into the
    documented three-way `switch` (with the same defensive, unreachable
    post-switch fallback return in both overloads).
  - `RequestCaptureAndWait()` now writes `m_requestedTextureName`/
    `m_requestedTextureChannel` for a `NamedTexture` request, still under the
    slot's own lock, BEFORE setting `requested = true` — exactly the ordering
    the phase document requires so no other thread can ever observe a stale
    name.
  - Added `RequestedTextureName()`/`RequestedTextureChannel()` — lock
    `m_namedTextureSlot.mutex`, return the default value if
    `!m_namedTextureSlot.requested`, otherwise a copy of the stored value.
- `src/Application/Application.cpp`:
  - Added `#include "../Encoding/DepthVisualization.h"` and
    `#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"`.
  - Added the new named-texture capture block, placed exactly where the
    phase document specifies (right after `m_editorLayer->
    RenderPlatformWindows();`, before the existing GPU-memory-snapshot
    comment/call) — see Section 3 for the one real bug found and fixed while
    wiring this up for real.

## 3. A real bug found and fixed while wiring the pseudocode up for real

The phase document's own Step 3.3 code sketch declares `bool ok = true;`
and only ever sets it to `false` inside the `wantsDepth` branch (when
`ConvertDepthToGrayscaleRgba8()` reports an unrecognized depth format) — the
`else if (IsBgraFormat(raw.format))` branch (the COLOR path) never touches
`ok` at all, which is exactly correct: the color path (`ConvertBgraToRgbaInPlace()`)
has no failure mode to report — it's a `void`-returning, unconditionally-safe
swizzle. This was double-checked carefully against the depth
in-place-conversion safety note in the phase document (same buffer used as
both source and destination) and confirmed correct — no bug there. Re-reading
the sketch's control flow line-by-line, **the pseudocode itself was already
logically correct as written**, so nothing needed correcting there.

The one genuine deviation is structural, not logical: `Application.cpp`
already has a well-established local-variable-naming convention (see the
existing Game-view/Scene-view blocks) that keeps every intermediate value
(`raw`, `ok`, `png`) scoped as tightly as possible inside its own nested
`if`/`else` — the implementation follows that exactly, matching the sketch's
own nesting one-for-one. **No logic bug was found or fixed in this phase's
own pseudocode** — everything in Step 3.3 compiled and matched its own
documented intent exactly once transcribed into the real file. (Contrast this
with Phase 3's completion report, which similarly found zero deviations in
ITS OWN plan — this phase's own two "REAL BUG"/"REAL GAP" fixes were already
applied by the phase document's own second-iteration audit pass before this
implementation session began, i.e. the `SlotFor()` ternary→switch rewrite and
the missing `#include <string>` — both are called out in Section 2 above as
implemented exactly as specified, not as bugs discovered fresh during this
session.)

## 4. Compile check

Ran `cmake --build build --target GreatTamanaEngineTests` (incremental):

```
[1/7] Building CXX object CMakeFiles/gte_core.dir/src/Application/FrameCaptureBridge.cpp.obj
[2/7] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Application/FrameCaptureBridgeTests.cpp.obj
[3/7] Building CXX object CMakeFiles/gte_core.dir/src/Application/Application.cpp.obj
[4/7] Building CXX object CMakeFiles/gte_core.dir/src/Network/NetworkServer.cpp.obj
[5/7] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Network/CaptureEndpointsEndToEndTests.cpp.obj
[6/7] Linking CXX static library libgte_core.a
[7/7] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

**Clean build, zero errors/warnings.** Also ran `cmake --build build` (the
default `GreatTamanaEngine.exe` target) to confirm the main engine executable
— which transitively includes every file this phase touched — still builds
cleanly end to end:

```
[1/2] Building CXX object CMakeFiles/GreatTamanaEngine.dir/src/main.cpp.obj
[2/2] Linking CXX executable GreatTamanaEngine.exe; Staging ... .spv next to GreatTamanaEngine; Copying SDL3.dll next to GreatTamanaEngine
```

(`main.cpp` itself doesn't touch this phase's code directly, but its link
step re-links `libgte_core.a`, confirming zero link-time issues across the
whole engine target too.)

## 5. Regression check

Per this task's own instructions, the full `ctest` regression suite was NOT
run (reserved for a later phase). As a fast, targeted sanity check, the
existing `FrameCaptureBridgeTests.cpp` suite (Tier 1, no live Vulkan/Renderer
needed) was run to confirm the `Swapchain`/`GameView` kinds — and the
rewritten `SlotFor()` dispatch they depend on — are still fully correct after
this phase's changes:

```
GreatTamanaEngineTests.exe --gtest_filter=FrameCaptureBridgeTest.*
```

**7/7 tests pass**, including `SwapchainAndGameViewSlotsAreIndependent`
(a direct regression check that the `SlotFor()` ternary→switch rewrite didn't
accidentally make two capture kinds share a slot).

Per Phase 4's own strategy document (Step 3.5), the NEW `NamedTexture`-kind
test cases (independent slot behavior, `RequestedTextureName()`/
`RequestedTextureChannel()` correctness, the three-way `SlotFor()` distinctness
check) are explicitly assigned to Phase 6 ("Tests, Docs, and Regression
Safety") — this phase's own brief is implementation + a fast compile check
only, not new automated test authorship, so none were added here. The new
`Application.cpp` block itself stays Tier 2 (needs a live `Renderer`/
`RenderGraph`) — covered by Phase 6's later manual end-to-end verification
once Phase 5's HTTP route exists to actually drive it.

## 6. What this phase deliberately does NOT do (per the plan)

- Does not add the actual HTTP route (`GET /get_texture`) — Phase 5.
- Does not add a `?channel=` value other than `color`/`depth` — Phase 5's own
  query-parameter parsing rejects anything else before this phase's code is
  ever reached.
- Does not change `RequestCaptureAndWait()`'s behavior for the existing
  `Swapchain`/`GameView` kinds in any way — both new parameters are simply
  unused for those two kinds' code paths, exactly as before.
- Does not introduce a combined/atomic `(name, channel)` accessor — the
  theoretical (harmless) interleaving between `IsCaptureRequested()`/
  `RequestedTextureName()`/`RequestedTextureChannel()` was analyzed in the
  phase document and left as three separate calls, unchanged here.
- Does not add new automated test coverage for the `NamedTexture` kind or for
  `Application.cpp`'s new block — deferred to Phase 6 per the plan.
- Does not run the full `ctest` regression suite — reserved for a later
  phase per this task's own instructions.

## Summary

- Branch: `feature/network-impl` (unchanged, as required).
- Edited files: `src/Application/FrameCaptureBridge.h`,
  `src/Application/FrameCaptureBridge.cpp`,
  `src/Application/Application.cpp`.
- No new files created this phase.
- Compile check: clean, zero errors/warnings (both the test target and the
  main `GreatTamanaEngine.exe` target).
- Regression check: 7/7 pre-existing `FrameCaptureBridgeTest` cases still
  passing (new `NamedTexture`-specific test cases deferred to Phase 6 per the
  plan).
- No logic bug found in the phase document's own Step 3.3 pseudocode once
  transcribed into real code — the two real defects this phase document
  itself already called out (`SlotFor()`'s ternary→switch rewrite, the
  missing `#include <string>`) were both already corrected in the phase
  document BEFORE this implementation session began, and were implemented
  here exactly as specified.
- Ready for Phase 5 (`PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES`).
