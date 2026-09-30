# PHASE3 — `BlitSpec`, `PassKind::Blit`, `PassRecord::blitCommand`, and 3 pure helper functions

Read `PHASE0_MASTER_STRATEGY.md` in full first (especially Locked Decision 1
— this whole phase exists because of it). Read the source document Part
B.2's full "DESIGN" section (lines 125-380). Read `PHASE2_COMPLETION_REPORT.md`
first.

**Nothing in this phase makes `PassKind::Blit` PRODUCIBLE by any real pass.**
This is pure vocabulary/data addition — a new enum value, a new struct, a
new optional field, and 3 pure functions. No `RenderGraphBuilder` method
changes here (that is PHASE5). This ordering is deliberate and required —
see PHASE0's own "Why this shape" section for PHASE3/PHASE4/PHASE5's
ordering rationale.

## Step 1: The Goal

1. `PassKind` gains a third enumerator, `Blit`.
2. `BlitSpec` (a small, typed, POD-shaped struct — src/dst texture handles,
   filter, depth flags, optional sub-region) exists in `RenderGraphTypes.h`.
3. `PassRecord::blitCommand` (`std::optional<BlitSpec>`) exists.
4. `ToString(PassKind)` (`RenderGraphTypes.cpp`) is updated so it stays a
   genuinely exhaustive, `default:`-less switch — closing the gap this
   campaign's own analysis found (PHASE0 Step 2): without this,
   `RenderGraphMetadata.cpp`'s `GET /render_graph` JSON `"kind"` field would
   silently report `"Unknown"` for every real Blit pass.
5. Three new, pure, Tier-1-testable free functions exist beside `BlitSpec`
   (Locked Decision 1): `ResolveEffectiveBlitFilter()`, `ResolveBlitRegion()`,
   `IsValidBlitRegion()` — each independently unit-tested with plain data,
   zero `VkDevice`/`VkImage` involved.

## Step 2: The Situation

- `src/Renderer/RenderGraph/RenderGraphTypes.h`:
  - Lines 378-381: `enum class PassKind : std::uint8_t { Graphics, Compute,
    };` — add `Blit,` as the third value, after `Compute,`. Update the doc
    comment immediately above it (lines 368-377), which ALREADY says "a
    future kind (e.g. a future pure-blit/copy pass) be added later without
    every call site having to re-litigate" — add one sentence recording that
    this is now that kind, and pointing at `BlitSpec`/`PassRecord::blitCommand`
    below.
  - Line 383: `const char* ToString(PassKind kind) noexcept;` — declaration,
    unchanged (implementation is in the `.cpp`, see below).
  - `ResourceUsage` (lines 552-604) and `DispatchByKind()` (lines 638+) are
    NOT touched by this phase at all — `BlitSpec` uses plain `TextureHandle`
    fields directly (`src`/`dst`), it does not add a 4th `ResourceKind`.
  - `PassRecord` (line 724 onward) — add `std::optional<BlitSpec>
    blitCommand;` as a new field, appended at the END of the struct (never
    inserted in the middle — see this file's own header comment, and how
    every prior campaign's field addition to this exact struct already
    follows this rule, e.g. `renderPassEvent` at line 837,
    `colorAttachments` after it). Doc comment: "Only meaningful when `kind
    == PassKind::Blit` — see `RenderGraphBuilder::AddBlitPass()` (PHASE5).
    `std::nullopt` for every other pass."
  - Add `struct BlitSpec { ... }` — place it BEFORE `struct PassRecord`
    (since `PassRecord::blitCommand` needs the complete type), immediately
    after `ColorAttachmentDesc`/`FindMismatchedColorAttachmentExtent()`
    (lines 702-722) is a reasonable spot — both are small, pass-shape-
    adjacent structs. Exact shape (copy the source document's own Part B.2
    code block verbatim, it is already complete and correct):
    ```cpp
    struct BlitSpec {
        TextureHandle src;
        TextureHandle dst;
        VkFilter filter = VK_FILTER_LINEAR;
        bool srcIsDepth = false;
        bool dstIsDepth = false;
        VkOffset3D srcRegionMin{};
        VkOffset3D srcRegionMax{};
        VkOffset3D dstRegionMin{};
        VkOffset3D dstRegionMax{};
    };
    ```
    Copy the source document's own doc comment on the region fields verbatim
    (lines 141-154 of the source document) — it already correctly specifies
    the "{0,0,0}/{0,0,0} is the one meaningful degenerate/sentinel value"
    rule that `ResolveBlitRegion()` below implements.
  - Immediately after `BlitSpec`, add the 3 pure helper function
    DECLARATIONS (bodies go in `RenderGraphTypes.cpp` — mirrors
    `FindMismatchedColorAttachmentExtent()`'s own declared-in-`.h`/
    defined-in-`.cpp` split exactly):
    ```cpp
    // Resolves BlitSpec::filter into the EFFECTIVE filter a real
    // vkCmdBlitImage call must use: verbatim when neither srcIsDepth nor
    // dstIsDepth is set, forced to VK_FILTER_NEAREST otherwise (Vulkan
    // disallows linear filtering against a depth/stencil format
    // unconditionally - there is no valid caller intent to trust either
    // build configuration with here, unlike ResolveBlitRegion()/
    // IsValidBlitRegion() below). The ONE official place BlitSpec::filter
    // is ever turned into a real Vulkan filter value - see RenderGraph.cpp's
    // own execution branch, which calls this rather than reading
    // spec.filter directly.
    VkFilter ResolveEffectiveBlitFilter(const BlitSpec& spec) noexcept;

    // One resolved [min, max) 3D region, in the SAME coordinate space
    // vkCmdBlitImage's own VkOffset3D srcOffsets[2]/dstOffsets[2] expect.
    struct ResolvedBlitRegion {
        VkOffset3D min{};
        VkOffset3D max{};
    };

    // Resolves ONE side (src or dst) of a BlitSpec's region fields against
    // that side's own physically resolved 2D extent: the all-zero sentinel
    // (regionMin AND regionMax both {0,0,0}) resolves to the FULL extent
    // ({0,0,0} to {extent.width, extent.height, 1}); any other value is
    // passed through UNCHANGED (validation is IsValidBlitRegion()'s job
    // below, not this function's - this function only ever expands the
    // sentinel, it never rejects/clamps anything).
    ResolvedBlitRegion ResolveBlitRegion(
        VkOffset3D regionMin, VkOffset3D regionMax, VkExtent2D resolvedExtent) noexcept;

    // Debug-assert-level validation (source document's own "REGION
    // VALIDATION" bullet, Part B.2) against an ALREADY-RESOLVED region
    // (i.e. called AFTER ResolveBlitRegion() above, never against raw,
    // possibly-sentinel input) - true only if max is STRICTLY greater than
    // min on every one of the 3 axes, AND the whole region's min/max both
    // fall within [0, resolvedExtent] on x/y and within [0, 1] on z (a 2D
    // texture's own resolved extent has no meaningful 3rd dimension beyond
    // exactly 1). A release build is permitted to trust the caller and
    // never call this at all (this engine's general "asserts are the
    // safety net, not a runtime check" convention) - see RenderGraph.cpp's
    // own execution branch for where this is actually asserted.
    bool IsValidBlitRegion(const ResolvedBlitRegion& region, VkExtent2D resolvedExtent) noexcept;
    ```
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp`:
  - Lines 67-77, `ToString(PassKind)` — add
    `case PassKind::Blit: return "Blit";` as a third case, immediately after
    `case PassKind::Compute:`.
  - Add the 3 function bodies at the end of the file (or immediately after
    `FindMismatchedColorAttachmentExtent()`'s own body, lines 140-152 — a
    reasonable, thematically-consistent spot):
    - `ResolveEffectiveBlitFilter`: `return (spec.srcIsDepth ||
      spec.dstIsDepth) ? VK_FILTER_NEAREST : spec.filter;` — genuinely this
      simple, do not over-engineer it.
    - `ResolveBlitRegion`: check the all-zero sentinel on BOTH `regionMin`
      AND `regionMax` (all 6 components `== 0`) — if true, return
      `ResolvedBlitRegion{ VkOffset3D{0,0,0}, VkOffset3D{
      static_cast<std::int32_t>(resolvedExtent.width),
      static_cast<std::int32_t>(resolvedExtent.height), 1 } }`; otherwise
      return `ResolvedBlitRegion{ regionMin, regionMax }` unchanged.
    - `IsValidBlitRegion`: `return region.max.x > region.min.x &&
      region.max.y > region.min.y && region.max.z > region.min.z &&
      region.min.x >= 0 && region.min.y >= 0 && region.min.z >= 0 &&
      static_cast<std::uint32_t>(region.max.x) <= resolvedExtent.width &&
      static_cast<std::uint32_t>(region.max.y) <= resolvedExtent.height &&
      region.max.z <= 1;` (adjust exact clamping/casting as needed once you
      have the real compiler in front of you — the INTENT is: strictly
      ordered on every axis, non-negative minimums, and the max corner never
      exceeds the physically resolved extent).
- Test precedent for ALL THREE new functions:
  `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` already has
  `FindMismatchedColorAttachmentExtent()`'s own test block — use it as the
  exact template for structure/style (plain data in, plain data/bool out,
  no fixture class needed).

## Step 3: The Plan

1. Add `PassKind::Blit` + updated doc comment (`RenderGraphTypes.h`).
2. Add `BlitSpec` + its 3 companion pure-function declarations
   (`RenderGraphTypes.h`).
3. Add `PassRecord::blitCommand` (`RenderGraphTypes.h`).
4. Add `ToString(PassKind)`'s new case + the 3 function bodies
   (`RenderGraphTypes.cpp`).
5. Tests (`RenderGraphTypesTests.cpp`) — add, at minimum:
   - `ToStringPassKindBlitReturnsBlit` (or fold into an existing
     `ToString(PassKind)` test if one already exists — search first).
   - `ResolveEffectiveBlitFilterReturnsSpecFilterWhenNeitherSideIsDepth` —
     both flags false, some non-default `filter` value (e.g.
     `VK_FILTER_CUBIC_IMG` or simply confirm `VK_FILTER_LINEAR` passes
     through when explicitly set) — asserts the returned filter equals
     `spec.filter` EXACTLY.
   - `ResolveEffectiveBlitFilterForcesNearestWhenSrcIsDepth` — `srcIsDepth =
     true`, `filter` left at (or explicitly set to) `VK_FILTER_LINEAR` —
     asserts `VK_FILTER_NEAREST` is returned.
   - `ResolveEffectiveBlitFilterForcesNearestWhenDstIsDepth` — same, for
     `dstIsDepth = true`.
   - `ResolveBlitRegionAllZeroSentinelResolvesToFullExtent` — `regionMin`/
     `regionMax` both default-constructed `VkOffset3D{}`, some non-trivial
     `VkExtent2D` (e.g. `{512, 256}`) — asserts `min == {0,0,0}` and
     `max == {512, 256, 1}`.
   - `ResolveBlitRegionNonSentinelPassesThroughUnchanged` — an explicit,
     non-zero, valid sub-region — asserts the returned region equals the
     input exactly, untouched.
   - `IsValidBlitRegionRejectsInvertedAxis` — construct a
     `ResolvedBlitRegion` where (for example) `max.x <= min.x` — asserts
     `false`.
   - `IsValidBlitRegionRejectsOutOfBoundsMax` — a region whose `max.x`/
     `max.y` exceeds the given `resolvedExtent` — asserts `false`.
   - `IsValidBlitRegionAcceptsFullResolvedExtent` — feed
     `ResolveBlitRegion()`'s own sentinel-resolved output straight into
     `IsValidBlitRegion()` for some real extent — asserts `true` (this is
     the one test that proves the two functions correctly compose with each
     other, which the execution branch, PHASE6, will rely on).
6. Confirm (`search_in_dir` for `PassKind::Graphics` and `PassKind::Compute`
   as an exhaustive-switch pattern anywhere ELSE in `src/Renderer/` — e.g.
   any other `ToString`-shaped or `switch (kind)`-shaped function this
   review might have missed) that `RenderGraphTypes.cpp`'s `ToString()` is
   the ONLY place inside `src/Renderer/` (excluding
   `FrameDebuggerData.cpp`, which PHASE4 owns) that needs this exact fix. If
   another exhaustive `switch (PassKind ...)` is found anywhere else in
   `src/Renderer/` or `src/Core/` or `src/Application/`, STOP and use
   `ask_questions` — PHASE0's own Step 2 grep did not find one, so this
   would be new information.

## Step 4: Verification

1. `ask_questions` first for any genuine ambiguity.
2. Incremental build: `cmake --build build`. Note: this WILL surface any
   accidentally-non-exhaustive `switch (PassKind ...)` elsewhere in the
   engine as a compiler WARNING (not an error, per PHASE0 Step 2 — no
   `-Werror` on this repo's own code) — read the full build output, don't
   just check for "0 errors"; if a new `-Wswitch`-class warning appears
   anywhere, treat it as a real finding and use `ask_questions` before
   deciding how to handle it (this campaign's own PHASE4 already owns
   `FrameDebuggerData.cpp`'s fix — a warning appearing there is expected and
   fine to leave for PHASE4; a warning anywhere else is NOT expected).
3. Targeted test run:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R RenderGraphTypesTest --output-on-failure`
4. Write `PHASE3_COMPLETION_REPORT.md` into this same folder — explicitly
   list every new `case`/switch site touched, and explicitly confirm
   `PassKind::Blit` is NOT yet producible by any real, running pass anywhere
   in the engine (a grep for `PassKind::Blit` outside
   `RenderGraphTypes.h`/`.cpp`/this test file should find nothing).
5. `git_add` + `git_commit`.
