# PHASE1 — Dual-Stage Retained Capture + Composite-Aware Preview Selection

## Parent -> PHASE0_MASTER_STRATEGY.md, read it first.

Campaign folder: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Risk level: **HIGHEST in this campaign** (see PHASE0's Step 3.5) — touches live Vulkan barrier/copy code and
a cross-file function-signature change. Read this entire document before writing any code.

---

## Step 1: The Goal (Where are we going?)

`FrameDebuggerHistory` retains TWO real images per captured frame instead of one — the true pre-atmosphere-
composite `"GameView"` copy (unchanged from today) and a NEW true post-composite `"GameViewComposited"`
copy — wired end-to-end from `ImGuiEditorLayer::BuildUI()` down to `FrameDebuggerHistory::CaptureFrame()`.
`FrameDebuggerPanel`'s Inspector preview box picks the RIGHT one to display based on which tree event is
currently selected: the literal `"GameView"` leaf shows the true pre-composite image; anything else
(including nothing selected) shows the true final, atmosphere-inclusive image. After this phase, simply
enabling the Frame Debugger and capturing a frame — with no further UI interaction at all — already shows
the atmosphere-scattering/aerial-perspective effect in the preview, closing this campaign's primary bug.

## Step 2: The Situation (Where are we now?)

See `PHASE0_MASTER_STRATEGY.md`'s Step 2 for the full root-cause investigation. The short version this
phase must fix:

- `ImGuiEditorLayer::BuildUI()` calls
  `m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, gpuSkinningPassNames);` — always the
  PRE-composite `m_gameView`, never the REAL final `m_gameViewComposited` (a `RenderTexture*` member,
  already updated earlier this exact frame by `SetGameViewCompositedTexture()`, and already consulted by
  this SAME function's own "Game" panel descriptor logic a few dozen lines earlier via a local
  `gameSource = (m_gameViewComposited != nullptr) ? m_gameViewComposited : &m_gameView` — re-derive this
  same fallback rule at the Frame Debugger call site, do not assume `m_gameViewComposited` is always
  non-null).
- `FrameDebuggerHistory::CaptureFrame(Renderer&, const FrameDebuggerSnapshot&, RenderTexture& gameViewSource)`
  makes exactly ONE retained GPU-to-GPU copy, into `FrameDebuggerHistoryEntry::preview`.
- `FrameDebuggerPanel::EnsurePreviewDescriptor()` unconditionally reads `entry->preview` with no awareness
  of which tree event is currently selected.
- `FrameDebuggerPanel::BuildInspectorPane()` already has a WORKING precedent for "hide the texture entirely
  for this specific kind of leaf" (`selectedEventIsGpuSkinning`, checking `details->passName == "GPU Skinning"`)
  — this phase's own new picking logic should read the same way, checking `details->passName == "GameView"`.

## Step 3: The Plan

### 3.1 `src/Editor/FrameDebuggerHistory.h` — new field + new signature

Add a new field to `FrameDebuggerHistoryEntry`, directly below `preview`:

```cpp
struct FrameDebuggerHistoryEntry {
    FrameDebuggerSnapshot snapshot;
    std::optional<RenderTexture> preview; // pre-composite "GameView" copy - unchanged behavior/doc comment.

    // NEW (frame-debugger-4 campaign, PHASE1) - a retained GPU copy of this
    // historical frame's real POST-atmosphere-composite "GameViewComposited"
    // output - the TRUE final image the "Game" panel / GET /get_game_view
    // actually show (see PHASE0_MASTER_STRATEGY.md's Step 2 root-cause
    // investigation). std::nullopt whenever CaptureFrame() below was called
    // with compositedGameViewSource == nullptr for THIS capture (e.g. a
    // capture taken before the atmosphere composite pass had ever produced
    // anything yet this session) - a real, honest "not available for this
    // particular captured frame" state, never a bug and never silently
    // substituted with something fake. Once populated for a given slot, a
    // LATER capture into that same slot may legitimately go back to
    // std::nullopt again if compositedGameViewSource is null on that later
    // call - this field's std::nullopt-ness is a property of the CAPTURE
    // that most recently wrote this slot, not a one-way ratchet.
    std::optional<RenderTexture> compositedPreview;
};
```

Also update `FrameDebuggerHistoryEntry`'s own EXISTING top-of-struct summary comment (the paragraph
immediately above `struct FrameDebuggerHistoryEntry {`, today reading "... plus a retained GPU copy of that
historical frame's real Game View output image. `preview` is std::nullopt only for a slot index
`FrameDebuggerHistory::CaptureFrame()` has never written into at all ... once written, it is ALWAYS
populated ...") — that paragraph describes ONLY `preview`'s own nullability rule today, and a reader skimming
just this class-level comment (rather than the new field-level comment above `compositedPreview` itself)
would otherwise wrongly assume EVERY field in this struct follows the same "once written, always populated"
rule. `compositedPreview` deliberately does NOT follow that rule (see its own comment above: it can
legitimately go back to `std::nullopt` on a later capture into the same slot) — extend this top-of-struct
comment with one more sentence making that distinction explicit, e.g.: "`compositedPreview` (below) instead
follows its own, different rule — see its own field-level comment." Skipping this would leave the class's own
summary comment actively misleading once this phase lands, which matters more than usual given this is the
campaign's flagged highest-risk phase.

Update `CaptureFrame()`'s declaration:

```cpp
// Called once per real capture trigger... [existing comment, extend with:]
// `compositedGameViewSource` is the CURRENT frame's real, post-atmosphere-
// composite final output (ImGuiEditorLayer's own m_gameViewComposited), or
// nullptr when no composited texture exists yet this session (see this
// method's own doc comment on FrameDebuggerHistoryEntry::compositedPreview
// above) - nullptr is a completely safe, ordinary input, never dereferenced.
void CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource,
    RenderTexture* compositedGameViewSource);
```

**Do not** give `compositedGameViewSource` a default argument — this campaign has exactly one real call
site (`FrameDebuggerPanel::TriggerCapture()`), and forcing that call site to be explicit about what it is
passing (rather than silently relying on an implicit default) is worth the one extra word of code, per this
codebase's own general preference for explicit-over-implicit at real call sites doing something
consequential.

### 3.2 `src/Editor/FrameDebuggerHistory.cpp` — dual-copy `CaptureFrame()` body

Restructure the existing single-copy body into a copy that runs for `gameViewSource` UNCONDITIONALLY
(exactly as today, zero behavior change for it) plus a SECOND copy that runs ONLY when
`compositedGameViewSource != nullptr`, both inside the SAME `renderer.ImmediateSubmit()` lambda (Locked
Design Decision #8, PHASE0). Concretely:

```cpp
void FrameDebuggerHistory::CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource)
{
    const int writeIndex = m_writeState.nextWriteIndex;
    FrameDebuggerHistoryEntry& entry = m_entries[static_cast<std::size_t>(writeIndex)];
    entry.snapshot = snapshot;

    const VkExtent2D extent = gameViewSource.Extent();
    char debugNameBuffer[48];
    std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerHistorySlot%d", writeIndex);
    entry.preview.emplace(renderer.CreateRenderTexture(static_cast<int>(extent.width),
        static_cast<int>(extent.height), gameViewSource.Format(), debugNameBuffer));

    // NEW - the second retained copy, only when a real composited source
    // exists this capture. Uses the COMPOSITED source's own extent/format
    // (which may legitimately differ in size from gameViewSource's own
    // extent between two captures if a resize landed asymmetrically - in
    // practice both always match the SAME Game View panel's current
    // content-region size, but this function must not assume that).
    // A distinct debug-name suffix ("Composited") keeps GPU-memory-debugger
    // tooling (if any reads RenderTexture debug names) able to tell the two
    // apart.
    const bool hasCompositedSource = (compositedGameViewSource != nullptr);
    char compositedDebugNameBuffer[48];
    if (hasCompositedSource) {
        std::snprintf(compositedDebugNameBuffer, sizeof(compositedDebugNameBuffer),
            "FrameDebuggerHistorySlot%dComposited", writeIndex);
        const VkExtent2D compositedExtent = compositedGameViewSource->Extent();
        entry.compositedPreview.emplace(renderer.CreateRenderTexture(static_cast<int>(compositedExtent.width),
            static_cast<int>(compositedExtent.height), compositedGameViewSource->Format(), compositedDebugNameBuffer));
    } else {
        entry.compositedPreview.reset();
    }

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const rg::ResourceState shaderRead = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const rg::ResourceState transferSrc = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);
    const rg::ResourceState transferDst = rg::RequiredStateFor(rg::ResourceAccess::TransferDst, false);
    const rg::ResourceState freshImageState{};

    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
        // Pre-composite copy - byte-for-byte the existing, already-correct
        // sequence, unchanged.
        rg::EmitImageBarrier(cmd, gameViewSource.Image(), range, shaderRead, transferSrc);
        rg::EmitImageBarrier(cmd, entry.preview->Image(), range, freshImageState, transferDst);
        VkImageCopy region{};
        region.srcSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent = VkExtent3D{ extent.width, extent.height, 1 };
        vkCmdCopyImage(cmd, gameViewSource.Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, entry.preview->Image(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        rg::EmitImageBarrier(cmd, gameViewSource.Image(), range, transferSrc, shaderRead);
        rg::EmitImageBarrier(cmd, entry.preview->Image(), range, transferDst, shaderRead);

        // NEW - post-composite copy, only when requested this capture.
        if (hasCompositedSource) {
            const VkExtent2D compositedExtent = compositedGameViewSource->Extent();
            rg::EmitImageBarrier(cmd, compositedGameViewSource->Image(), range, shaderRead, transferSrc);
            rg::EmitImageBarrier(cmd, entry.compositedPreview->Image(), range, freshImageState, transferDst);
            VkImageCopy compositedRegion{};
            compositedRegion.srcSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            compositedRegion.dstSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            compositedRegion.extent = VkExtent3D{ compositedExtent.width, compositedExtent.height, 1 };
            vkCmdCopyImage(cmd, compositedGameViewSource->Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                entry.compositedPreview->Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &compositedRegion);
            rg::EmitImageBarrier(cmd, compositedGameViewSource->Image(), range, transferSrc, shaderRead);
            rg::EmitImageBarrier(cmd, entry.compositedPreview->Image(), range, transferDst, shaderRead);
        }
    });

    m_writeState = AdvanceFrameDebuggerHistoryWriteState(m_writeState, kCapacity);
    m_cursor = ClampFrameDebuggerHistoryCursor(m_writeState.count - 1, m_writeState.count);
}
```

**Watch out for one real subtlety**: `entry.preview.emplace(...)`/`entry.compositedPreview.emplace(...)`
must both run BEFORE the `ImmediateSubmit()` lambda captures `entry` by reference — exactly as the existing
code already does for `entry.preview`, just now duplicated for `entry.compositedPreview` too. Do not reorder
this.

`VK_IMAGE_ASPECT_COLOR_BIT`/`range` is reused for both copies since both are always plain color images with
one mip/one layer — confirmed true for both `"GameView"` and `"GameViewComposited"` (`RenderTexture`'s own
`Create()` for a 2D color-only texture, no separate depth aspect on this specific texture handle either way
— the compositor's own depth READ comes from `viewRenderTexture.Target().depthImageView` passed
independently, unrelated to either of these two color copies).

### 3.3 `src/Editor/Panels/FrameDebuggerPanel.h` — new member + new `Build()` parameter

Add a new this-frame-only cached member, directly below `m_frameGameView`:

```cpp
RenderTexture* m_frameGameViewComposited = nullptr; // NEW - PHASE1 (frame-debugger-4). Nullable: mirrors
    // ImGuiEditorLayer::m_gameViewComposited's own "nullptr until the atmosphere
    // composite pass has produced something at least once this session" contract exactly
    // - see Build()'s own new parameter doc comment.
```

Change `Build()`'s declaration:

```cpp
// `compositedGameView` - the CURRENT frame's real, post-atmosphere-composite
// final Game View output (ImGuiEditorLayer's own m_gameViewComposited), or
// nullptr on a frame where no composited texture exists yet (mirrors
// `gameView`'s own "stashed for the rest of THIS call only" contract, just
// nullable) - see TriggerCapture()'s own doc comment for how this flows into
// FrameDebuggerHistory::CaptureFrame().
void Build(EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph, RenderTexture& gameView,
    RenderTexture* compositedGameView, const std::vector<std::string>& gpuSkinningPassNamesThisFrame);
```

In `Build()`'s body, alongside the existing `m_frameGameView = &gameView;` line, add:

```cpp
m_frameGameViewComposited = compositedGameView;
```

### 3.4 `src/Editor/Panels/FrameDebuggerPanel.cpp` — `TriggerCapture()`'s new argument

Change the one existing call:

```cpp
m_history.CaptureFrame(*m_frameRenderer, snapshot, *m_frameGameView);
```

to:

```cpp
m_history.CaptureFrame(*m_frameRenderer, snapshot, *m_frameGameView, m_frameGameViewComposited);
```

No other line in `TriggerCapture()` needs to change — `renderTargetInfo` (width/height/format) should
continue to be derived from `m_frameGameView` (the pre-composite source), NOT from
`m_frameGameViewComposited`, since `FrameDebuggerRenderTargetInfo` is documented/tested
(`RenderTargetInfoIsRealAndNamedGameView` in `FrameDebuggerSnapshotBuilderTests.cpp`) as describing the
`"GameView"`-named render target specifically; leave this exactly as-is.

### 3.5 `src/Editor/Panels/FrameDebuggerPanel.cpp` — `EnsurePreviewDescriptor()`'s new picking rule

This is the heart of the fix. Replace the top of the existing function (everything up to and including the
existing `hasPreview`/`ReleasePreviewDescriptor()`-and-return early-out) with a new block that FIRST decides
which retained `RenderTexture` (if any) should be shown this call, THEN falls through to the EXISTING
Channels/Levels neutral-check/dispatch logic unchanged, just reading from that decided pointer instead of
unconditionally from `entry->preview`:

```cpp
void FrameDebuggerPanel::EnsurePreviewDescriptor()
{
    const FrameDebuggerHistoryEntry* entry = m_history.CurrentEntry();

    // PHASE1 (frame-debugger-4 campaign) - decide WHICH of the two retained
    // textures this call should display, based on the currently-SELECTED
    // tree event (Locked Design Decision #5/#6, PHASE0_MASTER_STRATEGY.md):
    //   - the literal "GameView" leaf selected -> the TRUE pre-atmosphere-
    //     composite image (entry->preview), "as of the exact point this
    //     specific event finished" - preserves frame-debugger-3's own
    //     original Locked Design Decision #5 semantics for that one leaf.
    //   - anything else at all - including nothing selected (a fresh
    //     capture, or a Frame-History navigation that reset the selection),
    //     the "GPU Skinning" leaf (which itself is separately hidden behind
    //     "No Texture" by BuildInspectorPane()'s own existing
    //     selectedEventIsGpuSkinning check - this function does not need to
    //     special-case that itself), or PHASE2's brand-new "Aerial
    //     Perspective Composite" leaf once it exists - prefers the TRUE,
    //     final, atmosphere-inclusive entry->compositedPreview, falling back
    //     to entry->preview only when compositedPreview is std::nullopt for
    //     this particular captured frame (e.g. captured before the very
    //     first composite pass ever ran this session).
    // This ONE rule is deliberately written generically enough that PHASE2's
    // new tree leaf requires ZERO further change here - see
    // PHASE2_AERIAL_PERSPECTIVE_COMPOSITE_EVENT_TREE_LEAF.md's own Step 2.
    const RenderTexture* selectedSource = nullptr;
    if (entry != nullptr) {
        const std::optional<FrameDebuggerEventDetails> details =
            FindEventDetailsByIndex(entry->snapshot, m_selectedEventIndex);
        const bool viewingPreCompositeGameViewLeaf = details.has_value() && details->passName == "GameView";
        if (viewingPreCompositeGameViewLeaf) {
            selectedSource = entry->preview.has_value() ? &(*entry->preview) : nullptr;
        } else if (entry->compositedPreview.has_value()) {
            selectedSource = &(*entry->compositedPreview);
        } else if (entry->preview.has_value()) {
            selectedSource = &(*entry->preview);
        }
    }

    if (selectedSource == nullptr) {
        // Nothing to preview right now - see this function's own original
        // comment (no capture has ever happened yet, or - defensively -
        // neither retained texture is populated for the viewed slot).
        ReleasePreviewDescriptor();
        return;
    }

    // --- everything below is the EXISTING PHASE6 Channels/Levels logic,
    // unchanged except every "entry->preview" read is now "*selectedSource" ---

    const bool isNeutral =
        (m_channel == FrameDebuggerPreviewChannel::All) && (m_levelsBlack <= 0.0f) && (m_levelsWhite >= 1.0f);

    VkImageView desiredView = selectedSource->View();
    VkSampler desiredSampler = selectedSource->Sampler();

    if (!isNeutral && m_frameRenderer != nullptr) {
        const VkImageView sourceView = selectedSource->View();
        const bool dirty = (m_lastProcessedSourceView != sourceView) || (m_lastProcessedChannel != m_channel)
            || (m_lastProcessedLevelsBlack != m_levelsBlack) || (m_lastProcessedLevelsWhite != m_levelsWhite);
        if (dirty) {
            m_previewProcessor.RenderPreview(*m_frameRenderer, *selectedSource, m_channel, m_levelsBlack, m_levelsWhite);
            m_lastProcessedSourceView = sourceView;
            m_lastProcessedChannel = m_channel;
            m_lastProcessedLevelsBlack = m_levelsBlack;
            m_lastProcessedLevelsWhite = m_levelsWhite;
        }
        desiredView = m_previewProcessor.OutputView();
        desiredSampler = m_previewProcessor.OutputSampler();
    }

    if (m_previewDescriptor != VK_NULL_HANDLE && desiredView == m_lastKnownPreviewView) {
        return;
    }

    ReleasePreviewDescriptor();
    m_previewDescriptor = ImGui_ImplVulkan_AddTexture(desiredSampler, desiredView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_lastKnownPreviewView = desiredView;
}
```

Note `FrameDebuggerPreviewRenderer::RenderPreview()` already takes `const RenderTexture& source`
(confirmed in `FrameDebuggerPreviewProcessing.h`) — `*selectedSource` (a `const RenderTexture&` since
`selectedSource` is `const RenderTexture*`) binds directly, no `const_cast` needed anywhere in this
function.

**Do not touch** `Build()`'s existing "did the viewed history entry itself change underneath us" reset logic
(the block comparing `currentEntry->preview->View()` against `m_lastKnownRawPreviewView`, just above the
existing `EnsurePreviewDescriptor()` call site) — it must keep comparing `entry->preview` specifically (the
one field that is unconditionally populated on every real capture), not `compositedPreview` (which may
legitimately be absent) — this remains a correct, stable "did the slot change" signal exactly as documented
today, and needs zero changes for this phase.

**Also do not touch** `BuildInspectorPane()`'s existing `selectedEventIsGpuSkinning`/`showPreviewTexture`
logic — it stays exactly as it is; a compute-only leaf (GPU Skinning today, nothing else) still shows "No
Texture" regardless of which retained texture `EnsurePreviewDescriptor()` happened to prepare.

### 3.6 `src/Editor/ImGuiEditorLayer.cpp` — the one call-site fix

Locate the existing call (near `BuildUI()`'s own tail, after `BuildHierarchyPanel()`/`BuildInspectorPanel()`
etc. — search for `m_frameDebuggerPanel.Build(`):

```cpp
m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, gpuSkinningPassNames);
```

Replace with:

```cpp
// frame-debugger-4 campaign, PHASE1 - feeds the Frame Debugger BOTH the
// pre-composite "GameView" texture (m_gameView, always real) and the real
// post-atmosphere-composite final output (m_gameViewComposited, nullable
// until the composite pass has produced something at least once this
// session) - see FrameDebuggerHistory::CaptureFrame()'s own doc comment for
// exactly how these two flow into the ring buffer's own dual retained
// copies. Mirrors this SAME function's own earlier "Game" panel descriptor
// logic (the gameSource local a few dozen lines above) rather than
// introducing a second, differently-named local - m_gameViewComposited is a
// plain member, safe to read directly here too.
m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, m_gameViewComposited, gpuSkinningPassNames);
```

This compiles directly because `m_gameViewComposited` is already declared as `RenderTexture* m_gameViewComposited
= nullptr;` on this same class (confirmed) — no new member needed here, no ordering concern, since
`SetGameViewCompositedTexture()` is called by `Application.cpp` during the offscreen render-graph Execute()
call, which always completes before `BuildUI()` (and therefore this line) runs later the SAME frame.

**Re-verify at implementation time**: confirm the exact current line number and surrounding text via
`read_file`/`search_in_dir` before editing — this campaign's own investigation found it a few lines after
the "Game" panel descriptor block, but exact line numbers drift as other unrelated changes land on this
branch.

### 3.7 `src/Editor/NullEditorLayer.cpp` — confirm no change needed

`NullEditorLayer`'s `BuildUI()` override is a complete no-op (the Editor is compiled out entirely under
`GTE_ENABLE_EDITOR=OFF`) — it never calls `FrameDebuggerPanel::Build()` at all (that whole class doesn't
exist in that configuration). Confirm this remains true; no edit expected here.

### 3.8 Compile-check

After all of 3.1-3.6 land:

```
cmake --build build --target GreatTamanaEngine
cmake --build build --target GreatTamanaEngineTests
```

Both must succeed with zero errors. Do not run the full test suite or a full clean build yet — that is
PHASE3's job. A quick, live, manual smoke check is valuable here (not mandatory, but strongly recommended
given this phase's risk level): run the engine, open the Frame Debugger via `GET /frame_debugger/open` +
`GET /frame_debugger/enable?value=true`, and compare `GET /get_swapchain` (showing the Frame Debugger's own
preview box) against `GET /get_game_view` (the real final Game View) — they should now visibly match
(fog/haze included), where before this phase the Frame Debugger's own box would have shown a
noticeably-less-foggy image than `/get_game_view`.

If convenient during this same manual smoke check, also exercise two of the specific edge cases this phase's
own code comments call out but a plain open/enable/compare pass does not otherwise touch: (1) resize the
"Game" panel (or the window) so the Game View's own resolution changes, THEN trigger a second capture (the
"Capture" button, or `GET /frame_debugger/capture`) — confirm nothing crashes and both the pre-composite and
post-composite previews still display at the new resolution, directly exercising the "extents may legitimately
differ between two captures" case `FrameDebuggerHistory::CaptureFrame()`'s own new comments describe; (2)
click the `"GameView"` tree leaf explicitly and confirm the preview visibly becomes LESS foggy than the
default (nothing-selected) view, then deselect (or select a different leaf) and confirm it goes back to the
foggier, final image — directly confirming the picking rule actually flips both ways, not just that the
DEFAULT view happens to look right. The very-first-capture-before-any-composite-exists path
(`compositedGameViewSource == nullptr`) is real but practically unreachable in an ordinary live smoke test
(`SetGameViewCompositedTexture()` already runs with a real pointer from essentially the very first rendered
frame onward, per PHASE0_MASTER_STRATEGY.md's Step 2.1) — that path is intentionally left to be verified by
code review/reasoning about `compositedGameViewSource`'s nullable contract instead, not by a forced live
repro; don't spend time trying to contrive it.

### 3.9 What this phase deliberately does NOT do yet

- It does not add the new `"Aerial Perspective Composite"` tree leaf — `BuildRealFrameDebuggerSnapshot()`
  is untouched this phase, so there is currently no way to SELECT anything other than `"GameView"`/`"GPU
  Skinning"` leaves; the new `compositedPreview` field is fully wired and already visible as the DEFAULT
  view (nothing selected), but not yet reachable via an explicit tree click of its own dedicated leaf. That
  is PHASE2's entire job.
- It does not update any documentation (`AGENTS.md`/`docs/conventions/frame-debugger.md`/`TODO.md`) — that
  is PHASE3's job, done once after both PHASE1 and PHASE2 have landed so the docs describe the final,
  true state in one pass rather than twice.
- It does not add or modify any Tier-1 test file — `CaptureFrame()`/`EnsurePreviewDescriptor()` are both
  Tier-2 (need a live `VkDevice`/ImGui context), exactly like their pre-existing counterparts; PHASE3
  reviews whether anything newly PURE was introduced worth extracting into a testable free function (see
  that phase's own Step 3 for the specific candidate it identifies).

### 3.10 Completion report

Write `task_manager/frame-debugger-4/PHASE1_COMPLETION_REPORT.md` covering: the exact final diffs (or a
faithful summary) for each of the files in 3.1-3.6, confirmation both compile-check targets succeeded, and
the result of the recommended manual smoke check in 3.8 (screenshots/observations if a live engine session
was used this phase). Then commit (`git_add` + `git_commit`) code + report together.
