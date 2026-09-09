# PHASE4 — `FrameCaptureBridge` Named-Texture Support + `Application::Run()` Wiring

> **Second-iteration audit note (this revision):** this file was re-checked
> against the LIVE `src/Application/FrameCaptureBridge.h`/`.cpp` and the live
> `src/Application/Application.cpp`/`Application.h` byte for byte (this phase
> had not been through a second-iteration pass before now, unlike Phase 0-3,
> which already carry their own audit-note history). Two real, worth-fixing
> defects were found and are fixed in this revision; everything else checked
> out correct:
> 1. **REAL BUG — `SlotFor()`'s existing implementation is a two-way TERNARY,
>    not a switch statement, so "gain a case ... arm" was simply not
>    actionable as written.** Confirmed live, both overloads:
>    `return kind == FrameCaptureKind::Swapchain ? m_swapchainSlot :
>    m_gameViewSlot;`. There is no `switch`/`case` anywhere in the existing
>    function for a third arm to be added to — an implementer following the
>    previous wording literally would have had nothing to attach a `case` to
>    and would have had to invent the rewrite anyway, with a real chance of
>    getting the fallback/return-path shape wrong. **Fixed:** Step 3.2 below
>    now shows the exact, complete replacement body (a real three-way
>    `switch`, mirroring this same codebase's own established "no `default:`,
>    exhaustive switch, defensive fallback return after it" convention — see
>    `NetworkRoutes.cpp`'s planned `ExecuteTimingModeToString()` in Phase 5,
>    or `ToProfilingGpuSampleStatus()` in `Application.cpp` — for both the
>    `const` and non-`const` overloads).
> 2. **REAL GAP — `FrameCaptureBridge.h` does not include `<string>` today**
>    (confirmed live: only `<condition_variable>`/`<cstdint>`/`<mutex>`/
>    `<optional>`/`<vector>`), yet this phase adds a `std::string
>    m_requestedTextureName` member plus a `const std::string&
>    textureName` parameter and a `std::string RequestedTextureName() const`
>    return type — none of the currently-included headers is guaranteed to
>    drag in `<string>` transitively. Without an explicit `#include <string>`
>    this simply does not compile. **Fixed** — added to Step 3.1 below.
> 3. **Confirmed correct, no change needed:** `Application.h`'s `m_renderer`/
>    `m_renderGraph` members are both plain, non-pointer members of
>    `Application` (declared back to back, `m_renderer` then `m_renderGraph`)
>    and therefore both unconditionally in scope anywhere inside
>    `Application::Run()`, including the new block this phase adds — no
>    forwarding/threading-through is needed. Also confirmed live: the exact
>    placement this phase specifies (after `m_editorLayer->
>    RenderPlatformWindows();`, before
>    `Profiling::FrameProfiler::Instance().SetMemorySnapshot(...)`) is still
>    accurate against the current file — nothing has moved.
> 4. **A benign, pre-existing-in-spirit race was analyzed and found NOT to be
>    a bug, but is now called out explicitly (Step 3.2) so a future reviewer
>    doesn't have to re-derive this from scratch.** `Application::Run()`'s new
>    block (Step 3.3) calls `IsCaptureRequested()`, then separately
>    `RequestedTextureName()`, then separately `RequestedTextureChannel()` —
>    three independent lock/unlock cycles rather than one atomic read. In
>    principle a concurrent timeout + a brand-new request from a different
>    caller could interleave between these three calls. This is analyzed in
>    detail in Step 3.2 below and shown to be self-consistent (whichever
>    request is actually current by the time `FulfillPendingRequest()`/
>    `FailPendingRequest()` runs is always the one that gets serviced,
>    correctly) — not a data race, not a correctness bug, and not something
>    this phase needs to redesign around. Left as three separate accessors,
>    matching the phase's original design, rather than introducing a new
>    combined-read API purely for its own sake.
>
> Everything else in this file (the exact existing `FrameCaptureBridge`
> shape/fields, `RequestCaptureAndWait()`'s real locking discipline,
> `CapturedPngImage`'s real current fields, `Renderer::CaptureImagePixels()`/
> `WaitForGpuIdle()`'s Phase-3 signatures, `RenderGraph::
> DebugTextureSnapshotFor()`/`CurrentDebugTextureFrameCounter()`'s Phase-2
> signatures, `Encoding::ConvertDepthToGrayscaleRgba8()`'s Phase-3 semantics,
> and the Step 3.3 code sketch's own variable-scoping/ordering) was re-checked
> against the live source tree and against each dependency phase's own
> (already twice-audited) document, and found accurate — no further changes
> were needed there.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2 (registry query
API), Phase 3 (`Renderer::CaptureImagePixels()`/`WaitForGpuIdle()`,
`Encoding::ConvertDepthToGrayscaleRgba8()`).

## Step 1: The Goal (Where are we going?)

Extend `FrameCaptureBridge` (`src/Application/FrameCaptureBridge.h/.cpp`) —
the one sanctioned cross-thread bridge a Network route handler may touch —
with a THIRD capture kind whose request carries a dynamic payload (a
texture name + which channel), and wire `Application::Run()` to actually
service it: look the name up in the registry (Phase 2), and if known,
`WaitForGpuIdle()` + `CaptureImagePixels()` (Phase 3) + encode (color
straight through the existing BGRA→RGBA/PNG path, depth through the new
grayscale conversion) + `FulfillPendingRequest()`. If not yet known this
session, the request simply stays pending — the bridge's own existing fixed
timeout resolves it either way (a late-arriving texture that renders for
the first time a few frames after the request arrived still succeeds).

## Step 2: The Situation (Where are we now?)

- `FrameCaptureBridge` (confirmed live, see `PHASE0_MASTER_STRATEGY.md`'s
  Step 2 excerpt) today has EXACTLY two fixed kinds
  (`FrameCaptureKind::{Swapchain,GameView}`), each backed by its own,
  separately-named `Slot` member (`m_swapchainSlot`/`m_gameViewSlot`) — no
  dynamic per-request payload of any kind, since neither existing kind
  needs one. This phase adds a THIRD kind and a THIRD slot
  (`m_namedTextureSlot`), plus — uniquely for this one kind — a small piece
  of REQUEST payload (the requested name + channel) that must survive
  alongside the slot's own request/fulfilled/result bookkeeping, protected
  by the exact same per-slot mutex that already exists.
- **`SlotFor(FrameCaptureKind kind)` (both overloads) is, TODAY, a plain
  two-way TERNARY, not a switch** — confirmed live, byte for byte:
  ```cpp
  FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind)
  {
      return kind == FrameCaptureKind::Swapchain ? m_swapchainSlot : m_gameViewSlot;
  }
  ```
  (the `const` overload is identical). This phase must REPLACE this ternary
  with a real three-way dispatch (a `switch`, per this codebase's own
  established exhaustive-switch-with-defensive-fallback convention) — see
  Step 3.2 for the exact corrected code. Do not attempt to bolt a third case
  onto a ternary; there is nowhere for it to go.
- `RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds =
  3000)` is the ONLY entry point a route handler calls today — its
  signature must grow to accept the new payload for the `NamedTexture` kind
  without breaking the two EXISTING call sites in `NetworkServer.cpp`'s
  `RegisterCaptureRoute()` (Phase 5 owns the actual route; this phase only
  owns the bridge's own API shape) — see 3.1 below for the exact,
  backward-compatible signature change (two new, DEFAULTED parameters).
- **`FrameCaptureBridge.h` does not `#include <string>` today** (confirmed
  live: only `<condition_variable>`/`<cstdint>`/`<mutex>`/`<optional>`/
  `<vector>`) — this phase's new `std::string m_requestedTextureName` member,
  `const std::string& textureName` parameter, and `std::string
  RequestedTextureName() const` return type all require it. See Step 3.1.
- `Application::Run()` already has TWO existing, structurally analogous
  "is a capture pending, and if so can I positively determine right now
  that it will never succeed" fast-fail blocks — the Game-view one
  (`if (gameTarget == nullptr && m_captureBridge.IsCaptureRequested(
  FrameCaptureKind::GameView)) { m_captureBridge.FailPendingRequest(...); }`,
  confirmed live). This phase's own named-texture block follows the exact
  same SHAPE (check → look something up → either fail-fast on a positively-
  known-bad condition, or do nothing and let the timeout keep ticking) —
  the one, deliberate difference (per `PHASE0_MASTER_STRATEGY.md`'s Locked
  Design Decision 1) is that a SUCCESSFUL named-texture capture is also
  serviced from this SAME check (unlike GameView, whose success path lives
  in a totally different, narrower `if` block later in the function) —
  because Locked Design Decision 1 means there is no regime-specific
  "only inside the offscreen block" placement constraint here at all: this
  phase's whole capture-or-fail-or-wait decision can live in ONE place,
  run unconditionally once per `Run()` iteration.
- **Confirmed live, exact placement**: the new block goes after
  `m_editorLayer->RenderPlatformWindows();` and before
  `Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(
  m_renderer.GetMemoryTotals()));` — both statements still sit exactly where
  Step 3.3 below assumes, immediately before `Profiling::FrameProfiler::
  Instance().EndFrame();` closes out the frame. `Application.h` declares
  `m_renderer` and `m_renderGraph` as plain (non-pointer) members, one right
  after the other, so both are unconditionally in scope at this point in
  `Run()` — nothing needs to be threaded through as a parameter.
- `Renderer::CaptureImagePixels()`/`WaitForGpuIdle()` (Phase 3) and
  `RenderGraph::DebugTextureSnapshotFor()` (Phase 2) are the only two new
  things this phase's `Application.cpp` code needs to call — everything
  else (PNG encoding, `FulfillPendingRequest()`) already exists. `IsBgraFormat()`
  (an anonymous-namespace free function defined near the very top of
  `Application.cpp`, confirmed live) is already reachable from anywhere later
  in the same translation unit, including this phase's new block — no new
  include/forward-declare is needed to call it.
- `DebugTextureSnapshot::target` (`RenderTarget` — Phase 1) already carries
  BOTH the color half (`image`/`imageView`/`extent`/`format`) and the
  optional depth half (`depthImage`/`depthImageView`/`depthFormat`) of a
  named texture — `snapshot.hasDepth` says whether the depth half is
  actually meaningful. No new data needs adding anywhere for this phase to
  read a depth channel — it was already captured by Phase 1/2 in full.
  (Worth noting for a future reviewer: Locked Design Decision 7's
  `NotifyDebugTextureStateOverride()` correction hook only ever overwrites a
  snapshot's `colorState` — never `depthState` — because none of the four
  graph-external manual-finalize call sites Phase 3 wires up ever touches a
  depth image's layout; only a texture's COLOR half is ever externally
  sampled/presented. This means `snapshot->depthState`, as read by this
  phase's own block below, needs no equivalent correction and is already
  exactly right as `ExecuteCompiledGraph()` last left it.)

## Step 3: The Plan

### 3.1 — `FrameCaptureBridge.h` changes

**First, a required include (new in this revision):** add `#include
<string>` to `FrameCaptureBridge.h`'s existing include block (alongside
`<condition_variable>`/`<cstdint>`/`<mutex>`/`<optional>`/`<vector>`) —
required for `m_requestedTextureName`/the new parameter/return types below;
none of the file's current includes is guaranteed to bring it in
transitively.

```cpp
enum class FrameCaptureKind {
    Swapchain,
    GameView,
    // network-impl-4 campaign, Phase 4 - GET /get_texture. Unlike the two
    // above, a request of THIS kind carries a dynamic payload (which
    // texture, which channel) - see RequestedTextureName()/
    // RequestedTextureChannel() below.
    NamedTexture,
};

// network-impl-4 campaign, Phase 4 - which half of a named texture a
// GET /get_texture request wants. Color is the default (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 5).
enum class DebugTextureChannel {
    Color,
    Depth,
};
```

`CapturedPngImage` (already declared in `FrameCaptureBridge.h`, currently
`{ std::vector<std::uint8_t> pngBytes; int width; int height; }`) gains ONE
new field, defaulted so the two EXISTING `FulfillPendingRequest()` call
sites (`Swapchain`/`GameView`, `Application.cpp`) do not need to change at
all:

```cpp
struct CapturedPngImage {
    std::vector<std::uint8_t> pngBytes;
    int width = 0;
    int height = 0;
    // network-impl-4 campaign, Phase 4 - PHASE0_MASTER_STRATEGY.md's Locked
    // Design Decision 4. Meaningful ONLY for FrameCaptureKind::NamedTexture
    // (left at its default, 0, for Swapchain/GameView, which have no
    // equivalent "how many frames old" concept and whose own route handler
    // never reads this field at all - see Phase 5). Computed by
    // Application::Run() at the exact moment it services the request, as
    // `RenderGraph::CurrentDebugTextureFrameCounter() -
    // snapshot.lastUpdatedFrameCounter` (see 3.3 below).
    std::uint64_t framesSinceUpdate = 0;
};
```

`RequestCaptureAndWait()`'s own declaration gains two new, DEFAULTED
trailing parameters (keeps `RequestCaptureAndWait(FrameCaptureKind::
Swapchain)`/`(FrameCaptureKind::GameView)`'s existing two call sites in
`NetworkServer.cpp` compiling completely unchanged, exactly mirroring how
`network-impl-2`/`network-impl-3` always extend an existing signature with
a defaulted parameter rather than a breaking change):

```cpp
RequestResult RequestCaptureAndWait(FrameCaptureKind kind, int timeoutMilliseconds = 3000,
    const std::string& textureName = "", DebugTextureChannel channel = DebugTextureChannel::Color);
```

(`textureName`/`channel` are meaningful ONLY when `kind ==
FrameCaptureKind::NamedTexture` — ignored otherwise, exactly like an unused
parameter for the other two kinds; document this explicitly on the
declaration.)

New public accessors (called from the MAIN thread only, mirroring
`IsCaptureRequested()`'s own "cheap, side-effect-free read" doc comment —
place these right next to it):

```cpp
// Valid ONLY when IsCaptureRequested(FrameCaptureKind::NamedTexture) is
// currently true - the exact (textureName, channel) the currently-pending
// request asked for. Returns a default-constructed
// {"", DebugTextureChannel::Color} if nothing is currently pending for
// this kind - never garbage.
//
// NOTE: this is intentionally TWO separate accessors (plus
// IsCaptureRequested() as a third, prior call) rather than one combined
// atomic read. A concurrent request-of-a-different-name racing in between
// these calls is possible in principle but harmless in practice: whichever
// request is actually pending by the time this phase's own Application.cpp
// block reaches FulfillPendingRequest()/FailPendingRequest() (Step 3.3) is
// always the SAME request whose name/channel were just read here - a
// completion always targets "whatever this slot's current request is",
// never a stale one, since RequestCaptureAndWait() only marks a request
// complete for the specific slot state a caller is actually waiting on. No
// redesign is needed here purely to remove this theoretical interleaving.
std::string RequestedTextureName() const;
DebugTextureChannel RequestedTextureChannel() const;
```

New private members, alongside `m_swapchainSlot`/`m_gameViewSlot`:

```cpp
Slot m_namedTextureSlot;
// Guarded by m_namedTextureSlot.mutex - see .cpp for the exact locking
// discipline (set once, by RequestCaptureAndWait(), right before it marks
// `requested = true`; read by RequestedTextureName()/RequestedTextureChannel()
// under the same lock).
std::string m_requestedTextureName;
DebugTextureChannel m_requestedTextureChannel = DebugTextureChannel::Color;
```

### 3.2 — `FrameCaptureBridge.cpp` changes

**`SlotFor(FrameCaptureKind kind)` (both the `const` and non-`const`
overloads) must be REWRITTEN from a two-way ternary into a real three-way
dispatch — there is no ternary shape that adds a third arm cleanly.**
Confirmed live, the current body is exactly:

```cpp
FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind)
{
    return kind == FrameCaptureKind::Swapchain ? m_swapchainSlot : m_gameViewSlot;
}
```

Replace BOTH overloads with:

```cpp
FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind)
{
    switch (kind) {
    case FrameCaptureKind::Swapchain: return m_swapchainSlot;
    case FrameCaptureKind::GameView: return m_gameViewSlot;
    case FrameCaptureKind::NamedTexture: return m_namedTextureSlot;
    }
    return m_namedTextureSlot; // Unreachable - every enumerator handled above (no default: case, mirroring this codebase's own exhaustive-switch convention, e.g. RenderGraphTypes.h). Silences a "not all control paths return a value" warning on a compiler that doesn't prove the switch exhaustive on its own.
}

const FrameCaptureBridge::Slot& FrameCaptureBridge::SlotFor(FrameCaptureKind kind) const
{
    switch (kind) {
    case FrameCaptureKind::Swapchain: return m_swapchainSlot;
    case FrameCaptureKind::GameView: return m_gameViewSlot;
    case FrameCaptureKind::NamedTexture: return m_namedTextureSlot;
    }
    return m_namedTextureSlot; // Unreachable - see the non-const overload's own comment above.
}
```

- `RequestCaptureAndWait()`'s existing body (re-read live before editing)
  already does roughly: lock the slot's mutex; if already `requested`,
  return `{alreadyPending = true}` immediately; otherwise set
  `requested = true`, unlock, wait on the condition variable for
  `timeoutMilliseconds` (or until `fulfilled` becomes true), then build and
  return the appropriate `RequestResult`. This phase adds, BEFORE setting
  `requested = true` (still under the same lock, so no other thread can
  observe a request with a stale name): if `kind ==
  FrameCaptureKind::NamedTexture`, `m_requestedTextureName = textureName;
  m_requestedTextureChannel = channel;`.
- `RequestedTextureName()`/`RequestedTextureChannel()`: lock
  `m_namedTextureSlot.mutex`, return a copy of `m_requestedTextureName`/
  `m_requestedTextureChannel` (return the default-constructed values if
  `!m_namedTextureSlot.requested` — mirrors this method's own doc comment).

### 3.3 — `Application.cpp` wiring

Add `#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"`
(for `rg::DebugTextureSnapshot`) and
`#include "../Encoding/DepthVisualization.h"`. (`rg::ResourceState` is
already pulled in transitively via `RenderGraphDebugTextureRegistry.h`'s own
`#include "RenderGraphBarrierPlanner.h"`, and is also directly included by
Phase 3's own `Application.cpp` changes — either way, no separate include is
needed here for it.)

Add ONE new, self-contained block, placed AFTER the existing Present block
(after `m_editorLayer->RenderPlatformWindows();`, before the
`Profiling::FrameProfiler::Instance().SetMemorySnapshot(...)` call — i.e.
as late in the frame as convenient, since `WaitForGpuIdle()` deliberately
waits for EVERYTHING this frame regardless of exactly where it's called
from — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 1. Confirmed
live: this is still exactly where these two statements sit today.):

```cpp
// network-impl-4 campaign, Phase 4
// (task_manager/network-impl-4/PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT.md) -
// GET /get_texture's own capture path. Placed here (unconditionally, once
// per Run() iteration, AFTER every render-graph Execute() call this frame)
// so RenderGraph::DebugTextureSnapshotFor() sees the freshest possible
// registry state, and so WaitForGpuIdle() below waits out every submission
// this frame - both regimes - before the readback below runs. Cheap,
// side-effect-free when nothing is pending (IsCaptureRequested() is a
// plain bool read).
if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture)) {
    const std::string requestedName = m_captureBridge.RequestedTextureName();
    const DebugTextureChannel requestedChannel = m_captureBridge.RequestedTextureChannel();

    if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
        const bool wantsDepth = (requestedChannel == DebugTextureChannel::Depth);
        if (wantsDepth && !snapshot->hasDepth) {
            // Positively known, permanent-for-this-registration failure -
            // fail fast (409) rather than waiting out the full timeout,
            // exactly mirroring the Game-view "no panel visible"
            // fast-fail's own reasoning.
            m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
        } else {
            // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 4 -
            // computed BEFORE WaitForGpuIdle()/the readback below, from the
            // snapshot as it was at the moment this request was actually
            // serviced (never re-queried afterward - a capture that takes a
            // few extra milliseconds to read back must not report itself
            // as "0 frames old" merely because the CURRENT counter moved on
            // in the meantime; it genuinely reflects THIS snapshot's own
            // last-write frame, compared against "now").
            const std::uint64_t framesSinceUpdate =
                m_renderGraph.CurrentDebugTextureFrameCounter() - snapshot->lastUpdatedFrameCounter;

            m_renderer.WaitForGpuIdle();

            const VkImage image = wantsDepth ? snapshot->target.depthImage : snapshot->target.image;
            const VkImageAspectFlags aspect = wantsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            const VkFormat format = wantsDepth ? snapshot->target.depthFormat : snapshot->target.format;
            const rg::ResourceState state = wantsDepth ? snapshot->depthState : snapshot->colorState;

            Renderer::CapturedRawPixels raw =
                m_renderer.CaptureImagePixels(image, aspect, format, snapshot->target.extent, state);

            bool ok = true;
            if (wantsDepth) {
                // Safe to write in-place into the SAME buffer it reads from
                // (see this section's own note immediately below the code
                // sample): every pixel's 4 input bytes are fully read into a
                // local temporary BEFORE any of that pixel's own 4 output
                // bytes are written, and input/output share the exact same
                // per-pixel byte offset (no shift) - never a different
                // pixel's range.
                ok = Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data());
            } else if (IsBgraFormat(raw.format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
            }

            if (!ok) {
                // Depth format this device negotiated isn't one
                // ConvertDepthToGrayscaleRgba8() recognizes - see
                // PHASE3's own accepted, documented, narrow risk.
                m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
            } else {
                std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                    CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
            }
        }
    }
    // else: this name has never been registered yet this session - leave
    // the request pending; either it starts rendering within the bridge's
    // existing fixed timeout (a later Run() iteration's own check above
    // then succeeds), or the caller eventually gets HTTP 504 - exactly the
    // same accepted "main thread hasn't produced this yet" bucket
    // network-impl-2's own PHASE0_MASTER_STRATEGY.md already documents.
}
```

**Important correctness note on `ConvertDepthToGrayscaleRgba8()`'s
in-place-looking call above:** it is passed `raw.pixels.data()` as BOTH the
source AND destination buffer. This is safe, and provably so (not merely
"probably fine"): Phase 3's real implementation reads a whole 4-byte `word`
from `rawDepth + i*4` into a local variable BEFORE writing any of
`outRgba8 + i*4`'s 4 bytes for that SAME `i` — and, critically, input and
output use the IDENTICAL per-pixel byte offset (`i*4` on both sides, no
shift/stride difference between the two buffers, since both are tightly
packed `width*height*4` byte arrays addressed the same way). This means
every iteration's read-range and write-range are the EXACT SAME 4 bytes —
there is no cross-pixel aliasing to worry about at all (pixel `i`'s write
never touches any other pixel `i'`'s still-unread input), regardless of
whether the loop reads-then-writes or (hypothetically) writes-then-reads —
the only thing that would break this is if a future edit made input/output
have DIFFERENT strides/offsets per pixel, which would turn this from "same
range" into genuine overlap. If Phase 3's real, live implementation is ever
changed to read one pixel while writing to a DIFFERENT pixel's offset (e.g.
processing in reverse, or downsampling), this call site MUST switch to a
separate destination buffer immediately — re-verify this exact "same
offset on both sides" property directly against the live
`DepthVisualization.cpp` source before relying on this in-place call again
at implementation time.

### 3.4 — What this phase deliberately does NOT do

- Does not add the actual HTTP route — Phase 5.
- Does not add a `?channel=` value other than `color`/`depth` — Phase 5's
  own query-parameter parsing rejects anything else as a 400, before ever
  reaching this phase's code (so `DebugTextureChannel` only ever needs
  these two enumerators).
- Does not change `RequestCaptureAndWait()`'s behavior for the existing
  `Swapchain`/`GameView` kinds in any way — both new parameters are
  entirely ignored for those two kinds' own code paths (already true by
  construction, since neither reads `textureName`/`channel` at all).
- Does not introduce a combined/atomic `(name, channel)` accessor to close
  the theoretical interleaving noted in Step 3.1/3.2's own comments — that
  interleaving was analyzed and found harmless (see those sections), so no
  new API surface is added purely to remove it.

### 3.5 — Tests

- Extend `tests/Application/FrameCaptureBridgeTests.cpp` (Tier 1 — a fake
  producer/consumer thread pair, no live Vulkan/Renderer needed, exactly
  like the existing tests for `Swapchain`/`GameView`): add a case
  requesting `FrameCaptureKind::NamedTexture` with a specific name/channel,
  asserting `RequestedTextureName()`/`RequestedTextureChannel()` report
  back exactly what was requested while pending, and that
  `FulfillPendingRequest()`/`FailPendingRequest()` correctly wake the
  waiting thread with the expected `RequestResult`, mirroring the existing
  `Swapchain`/`GameView` test shapes exactly.
- **New test case (this revision): a pending `NamedTexture` request for one
  name must make a SECOND, concurrent `NamedTexture` request for a
  DIFFERENT name return `alreadyPending == true` immediately (HTTP 503),
  never block, and never overwrite the first request's own recorded
  name/channel.** This is the concrete, testable consequence of
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 1 ("at most one such
  stall can ever be pending at once") and of this phase's own single,
  shared `m_namedTextureSlot` (there is deliberately NOT one slot per
  texture name) — worth its own explicit assertion so a future refactor
  that mistakenly tries to give `NamedTexture` per-name slots gets caught
  immediately by a failing test rather than only being noticed via a
  confusing production 503.
- **New test case (this revision): `SlotFor()` must still return three
  MUTUALLY DISTINCT `Slot` objects** (assert via requesting all three kinds
  concurrently — e.g. from three simulated threads — and confirming none of
  them observe each other's `requested`/`result` state) — a regression test
  for the ternary→switch rewrite in Step 3.2, since a copy/paste mistake
  there (e.g. two `case` labels accidentally returning the same slot) would
  otherwise silently make two capture kinds interfere with each other.
- `Application.cpp`'s own new block stays Tier 2 (needs a live
  `Renderer`/`RenderGraph`) — covered by Phase 6's manual end-to-end
  verification once Phase 5's route exists to actually drive it.
- **Fast compile check**: `cmake --build build` must succeed before moving
  to Phase 5.
