#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// PHASE2 (task_manager/frame-debugger-3/PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md)
// - BuildRealFrameDebuggerSnapshot() below reshapes these two ALREADY-REAL
// types into a real FrameDebuggerSnapshot; both are safe to include here
// since this whole file only ever compiles under GTE_ENABLE_EDITOR, exactly
// like FrameDebuggerCapture.h itself.
#include "FrameDebuggerCapture.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"

// task_manager/frame-debugger-2 campaign (PHASE1) - the pure, ImGui-free
// data model behind the Editor's "Frame Debugger" window (Panels/
// FrameDebuggerPanel.h) - mirrors JobsPanelData.h/ProfilerPanelData.h's
// own "small, dedicated, directly-testable reshaping module" precedent
// (see AGENTS.md, "Testability & Regression Safety").
//
// THIS CAMPAIGN IS GUI-ONLY: BuildPlaceholderFrameDebuggerSnapshot()
// below always returns an EMPTY snapshot, on purpose, forever, until a
// FUTURE campaign adds real frame/draw-call capture and either replaces
// this function's body or introduces a second, real builder function
// FrameDebuggerPanel switches to calling instead. Every struct here is
// already shaped exactly like the reference screenshot's real data would
// be, specifically so that future swap requires touching NOTHING under
// Panels/FrameDebuggerPanel.cpp - only this file (or a new sibling file)
// needs to change.
namespace gte {

// One texture property row (see the reference screenshot's "Textures"
// section, e.g. "_MainTex"). `valueLabel` is already a display-ready
// string (e.g. a texture's name, or "None") - never a real GPU handle;
// this struct is intentionally decoupled from any real Vulkan/RenderTexture
// type, exactly like RenderGraphPassSnapshot's own OWNED-string
// philosophy (see RenderGraphSnapshot.h).
struct FrameDebuggerTextureProperty {
    std::string name;
    std::string valueLabel;
};

// One vector property row (e.g. "_Color", "(1, 1, 1, 1)").
struct FrameDebuggerVectorProperty {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// One 4x4 matrix property row (e.g. "unity_MatrixVP"). Row-major, 16
// entries - values[0..3] is row 0, etc.
struct FrameDebuggerMatrixProperty {
    std::string name;
    std::array<float, 16> values{};
};

// Everything the Inspector's event-level section (bottom half of the
// right-hand pane) needs to display for ONE selected draw/event - see
// Panels/FrameDebuggerPanel.cpp's BuildEventDetailsSection() (PHASE6).
// Every field is a plain, already-formatted display string/value -
// deliberately NOT a real VkPipeline/shader-reflection handle of any
// kind (this campaign never reads one).
struct FrameDebuggerEventDetails {
    int eventIndex = -1; // Matches the owning FrameDebuggerEventNode::eventIndex.
    std::string shaderName;
    std::string passName;
    std::string blendMode;
    std::string zClip;
    std::string zTest;
    std::string zWrite;
    std::string cull;
    std::string stencilRef;
    std::string stencilComp;
    std::string stencilPass;
    std::string stencilFail;
    std::string stencilZFail;
    std::vector<FrameDebuggerTextureProperty> textures;
    std::vector<FrameDebuggerVectorProperty> vectors;
    std::vector<FrameDebuggerMatrixProperty> matrices;

    // Short, display-ready label for this event's own OPERATION KIND
    // (e.g. "Draw Mesh", "Clear", "SetRenderTarget") - matches the
    // reference screenshot's own "Event #2117: Draw Mesh" header
    // convention. Deliberately its own copy rather than re-deriving it
    // from the owning FrameDebuggerEventNode::name (which may carry
    // extra detail, e.g. "Draw Mesh pf_fence_02") - this struct is
    // self-contained on purpose (see FrameDebuggerData.h's own top-of-
    // file "OWNED-string philosophy" note), and BuildEventDetailsSection()
    // (PHASE6) needs no access back into the tree at all to render its
    // own header. Always empty in practice this campaign (never
    // populated, like every other field here) - appended at the END of
    // this struct's field list, not inserted in the middle, mirroring
    // BoneViewerWindow.h's own explicit "never insert a field in the
    // middle of a struct some call site might positionally
    // aggregate-initialize" precedent.
    std::string eventLabel;
};

// One row of the left-hand event tree (see the reference screenshot's
// "Camera.Render > Drawing > Render.OpaqueGeometry > ... > Draw Mesh ..."
// hierarchy). A GROUP node (isDrawCall == false, e.g. "Drawing",
// "Render.OpaqueGeometry") has children and no `details`; a LEAF node
// (isDrawCall == true, e.g. one "Draw Mesh pf_fence_02" row) has no
// children and, once real capture exists, a populated `details`.
// `details` is std::optional and almost always std::nullopt this
// campaign (there is never a real leaf node to populate it for, since
// BuildPlaceholderFrameDebuggerSnapshot() always returns zero nodes) -
// see FindEventDetailsByIndex() below for the one place that reads it.
struct FrameDebuggerEventNode {
    std::string name;
    bool isDrawCall = false;
    int eventIndex = -1; // Only meaningful when isDrawCall is true; a global, 0-based, Unity-style event counter.
    std::optional<FrameDebuggerEventDetails> details;
    std::vector<FrameDebuggerEventNode> children;
};

// Frame-level (not per-event) render-target info, shown in the
// Inspector's frame-level chrome (PHASE5) regardless of any event
// selection - e.g. the reference screenshot's "<No name>" / "866x487
// Default" row.
struct FrameDebuggerRenderTargetInfo {
    std::string name = "<No name>";
    int width = 0;
    int height = 0;
    std::string format = "Default";
};

// The whole displayable snapshot for one "frame" of Frame Debugger data.
// `totalEventCount` backs the stepper row's "N of M" label (see
// FormatFrameStepperLabel() below) - always 0 this campaign, since
// rootNodes is always empty.
struct FrameDebuggerSnapshot {
    std::vector<FrameDebuggerEventNode> rootNodes;
    int totalEventCount = 0;
    FrameDebuggerRenderTargetInfo renderTarget;
};

// Always returns a completely EMPTY snapshot (rootNodes empty,
// totalEventCount == 0, renderTarget left at its all-default state) -
// see this file's own top-of-file comment for why, and PHASE0's Locked
// Design Decision #2 for why the UI must never invent fake rows to fill
// the gap this leaves. THE single seam a future real-capture campaign
// replaces (either this function's body, or by introducing a second, real
// builder function FrameDebuggerPanel::Build() switches to calling
// instead of this one - either way, no other file needs to change).
FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot();

// Formats the stepper row's "N of M" label - 1-based display index,
// matching the reference screenshot's own "2117 of 2117" convention.
// Returns "0 of 0" whenever totalEventCount <= 0 (always true this
// campaign) rather than a divide-by-zero-adjacent "1 of 0" or similar -
// this is the one place that decision is made, so PHASE3's stepper row
// never needs its own special-casing.
std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount);

// Clamps `requested` into a valid [0, totalEventCount - 1] selection, or
// returns -1 (no valid selection possible) if totalEventCount <= 0.
// Pure integer arithmetic - the one place PHASE4's future real tree-row
// click handling, and any future prev/next-event navigation buttons,
// should route their own index math through, rather than each
// reimplementing the same clamp.
int ClampSelectedEventIndex(int requested, int totalEventCount);

// PHASE4 (task_manager/frame-debugger-3/PHASE4_PANEL_REAL_TREE_AND_FRAME_HISTORY_UI.md)
// - formats the NEW Frame-History mini-toolbar's label: "Frame 3 of 8" for a
// 0-based cursorIndex == 2 into an 8-slot history (1-based display,
// matching FormatFrameStepperLabel()'s own convention above) - this is a
// COMPLETELY SEPARATE axis from FormatFrameStepperLabel() (Locked Design
// Decision #4, PHASE0_MASTER_STRATEGY.md: "which CAPTURED FRAME (of up to
// FrameDebuggerHistory::kCapacity) is being viewed", never to be conflated
// with "which EVENT, within the currently-viewed captured frame, is
// selected"). Returns "Frame 0 of 0" whenever count <= 0 (a fresh, never-
// captured-into FrameDebuggerHistory) rather than a divide-by-zero-adjacent
// "Frame 1 of 0".
std::string FormatFrameHistoryLabel(int cursorIndex, int count);

// Recursively searches `snapshot.rootNodes` (and every descendant) for a
// leaf node (isDrawCall == true) whose eventIndex == eventIndex,
// returning its `details` if found. Returns std::nullopt if
// eventIndex < 0, if no such node exists, or if the matching node's own
// `details` was never populated - ALWAYS std::nullopt in production this
// campaign (BuildPlaceholderFrameDebuggerSnapshot() never produces a
// non-empty tree), but written as a REAL, correct, recursive lookup so a
// future campaign only needs to start populating FrameDebuggerEventNode::
// details for this to start returning real values - no caller-side code
// (Panels/FrameDebuggerPanel.cpp's BuildEventDetailsSection() call site,
// PHASE6) ever needs to change.
std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex);

// Formats a vector property's value as "(x, y, z, w)" - matches the
// reference screenshot's own "_Color  (1, 1, 1, 1)" display convention.
// Trims trailing zeros the same way std::to_string would NOT do on its
// own (e.g. "1" not "1.000000") by using a short, fixed "%g"-style
// formatting internally - see FrameDebuggerData.cpp for the exact
// formatting rule.
std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector);

// Formats a 4x4 matrix property as 4 space-joined rows of 4 numbers
// each, newline-separated - e.g. row-major
// "0.001 0 0 0\n0 0.0019 0 0\n0 0 0.00023 0.5\n0 0 0 1", matching the
// reference screenshot's own "unity_MatrixVP" grid. Caller decides how
// to lay the 4 lines out in ImGui (see BuildEventDetailsSection() below,
// which draws each row as its own ImGui::Text() call rather than one
// multi-line string, for cleaner monospace column alignment) - this
// function itself only needs to produce the 4 ready-to-split-on-'\n'
// lines of text; NEVER used as one giant multi-line ImGui::Text() call
// directly by that call site (ImGui text wrapping/alignment would look
// wrong that way).
std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix);

// PHASE2 (task_manager/frame-debugger-3/PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md)
// - the real, non-placeholder builder. Pure reshape of ONE frame's already-
// real data (`graphSnapshot` - see gte::rg::BuildRenderGraphSnapshot();
// `capture` - see PHASE1's FrameDebuggerCaptureContext, already Reset()/
// populated for the CURRENT frame by the caller) into a real,
// Game-View-only FrameDebuggerSnapshot - never a competing capture
// mechanism. No live VkDevice/Renderer/RenderTexture involved - exactly
// like BuildRenderGraphSnapshot() itself.
//
// Returns an EMPTY FrameDebuggerSnapshot{} (same honest "no frame captured
// yet" shape BuildPlaceholderFrameDebuggerSnapshot() already models) if
// `graphSnapshot` has no pass literally named "GameView" - e.g. queried
// before the very first frame ever rendered.
//
// frame-debugger-5 campaign, PHASE2
// (PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md) - this function
// no longer takes a caller-supplied `gpuSkinningPassNamesThisFrame` name
// list (REMOVED - see PHASE0_MASTER_STRATEGY.md's Locked Design Decision
// #2/#6). Every real compute dispatch this frame - GPU Skinning, every
// atmosphere LUT/volume pass, the Aerial Perspective Composite pass,
// Compute Blur Validation, and any future compute pass this engine ever
// adds - is now discovered GENERICALLY, purely via PHASE1's new
// `RenderGraphPassSnapshot::isComputePass` flag, never by a hand-maintained
// name list or a hardcoded literal string match.
//
// Tree shape produced otherwise: one root group node "Game View", with an
// OPTIONAL "Compute Dispatches (Pre-GameView)" child group (present only
// when at least one real, surviving `isComputePass == true` pass's own
// index in `graphSnapshot.passesInExecutionOrder` is STRICTLY LESS THAN
// the real "GameView" pass's own index - e.g. GPU Skinning, every
// atmosphere LUT pass), followed by exactly one LEAF for the real
// "GameView" pass itself, followed by an OPTIONAL "Compute Dispatches
// (Post-GameView)" child group (same rule, but STRICTLY GREATER THAN
// "GameView"'s own index - e.g. the Aerial Perspective Composite pass) -
// see PHASE0_MASTER_STRATEGY.md's Locked Design Decisions #1/#2/#6/#7/#8
// for the full reasoning (pass-level granularity, pass-scoped facts,
// Game-View-only scope, the split-group ordering rule). Each group is a
// SIBLING of "GameView" in the tree, never nested inside the other, and is
// only added at all when it has at least one real surviving child this
// frame (never an empty, misleading group).
//
// frame-debugger-6 campaign, PHASE2
// (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md) - BOTH groups
// above also now REQUIRE `pass.viewScope != rg::ViewScope::SceneView` (a
// real, PHASE1-stamped, structural field - never a pass-name/resource-suffix
// string comparison) in addition to `isComputePass`/`!isCulled`/execution-
// order position. This engine genuinely runs a SEPARATE per-view copy of
// several Atmosphere compute passes (Sky-View LUT, Aerial Perspective
// Volume, Aerial Perspective Composite) - one for the Game View, one for the
// Editor's own Scene View - under the exact same literal pass NAME, so
// without this extra check this tree used to show duplicate,
// indistinguishable leaves for the SAME name, and even leaked a genuinely
// Scene-View-only debug tool ("ComputeBlurValidation") into a tree that is
// documented as Game-View ONLY. `ViewScope::Shared` (e.g. the
// Transmittance/Multi-Scattering LUT passes, genuinely computed once per
// frame, not once per view) and `ViewScope::GameView` both still pass
// through unchanged.
//
// frame-debugger-6 campaign, PHASE4
// (PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md) - the "GameView" leaf
// itself is no longer always a childless leaf: it now also gains one real
// CHILD leaf per real per-draw attribution record captured this frame
// (`FrameDebuggerCaptureContext::DrawRecords()`, PHASE3 of this campaign),
// e.g. `"terrain (Entity 2)"`/`"SmokeTestCube (Entity 3)"` - an EXPLICIT,
// user-approved breaking change to the historical "one leaf per PASS, never
// one leaf per mesh/entity" rule (PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #1, `frame-debugger-2`'s own original rule). Each child leaf's own
// `eventIndex` is assigned strictly AFTER "GameView"'s own `eventIndex` and
// strictly BEFORE anything in the Post-GameView group, preserving the same
// "pre < GameView < post" monotonic-eventIndex invariant one level deeper
// (pre < GameView < GameView's own children < post). Each child leaf's own
// `FrameDebuggerEventDetails::passName` is deliberately NEVER the literal
// string "GameView" (see `BuildGameViewDrawRecordLeaf()`'s own doc comment in
// FrameDebuggerData.cpp for the full reasoning) - it would otherwise collide
// with `FrameDebuggerPanel::EnsurePreviewDescriptor()`'s existing
// `isViewingGameViewLeaf = (details->passName == "GameView")` exact-string
// check. Selecting a per-entity leaf still falls back to the existing
// whole-frame `compositedPreview`/`preview` image via the UNCHANGED
// `ChooseFrameDebuggerPreviewSource()` rule below (Locked Design Decision #4)
// - no isolated per-mesh preview image is attempted.
//
// `gameViewRenderTargetInfo` is DELIBERATELY a plain, already-resolved
// parameter rather than this function reaching into a live RenderTexture/
// Renderer itself - the phase document's own Step 3.1 point 4 asks for real
// width/height/format info, but this function must stay pure (no live
// VkDevice/Renderer). Only `width`/`height`/`format` are actually used from
// it - `name` is always overwritten to "GameView" for a non-empty result
// (left completely untouched - the caller's argument is simply ignored -
// for the empty-result "no GameView pass" case, matching
// BuildPlaceholderFrameDebuggerSnapshot()'s own all-default convention).
// Defaults to an all-default FrameDebuggerRenderTargetInfo{} so a caller
// that does not yet have real live extent/format data on hand (e.g. every
// Tier-1 test in tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp) can
// simply omit it.
FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(
    const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture,
    const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo = FrameDebuggerRenderTargetInfo{});

// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md, Step 3.3 "Step A" -
// v2/second-iteration review finding) - one real compute-dispatch pass's own
// FIRST Texture-kind write this frame, ready for FrameDebuggerHistory::
// CaptureFrame() (FrameDebuggerHistory.h/.cpp) to resolve into a real,
// retained GPU copy via rg::RenderGraph::DebugTextureSnapshotFor().
struct FrameDebuggerComputePassTextureWrite {
    std::string passName;
    std::string writeTextureName;
};

// Pure, CPU-side discovery (no live VkDevice/Renderer/RenderTexture
// involved) - walks `graphSnapshot.passesInExecutionOrder` directly (NOT
// the Editor's own already-built FrameDebuggerEventNode tree/display-string
// row labels - see this function's own originating phase document for why
// that alternative was explicitly rejected during this campaign's v2
// review) and collects one {pass.name, pass.writeNames[i]} pair for every
// real, SURVIVING (`isCulled == false`) compute-dispatch (`isComputePass ==
// true`) pass whose FIRST `writeKinds[i] == rg::ResourceKind::Texture`
// write is found. A pass with no `Texture`-kind write at all (a
// `Buffer`-only write, e.g. GPU Skinning's own output buffer; or - until a
// future campaign - a `VolumeTexture`-only write, e.g. the Aerial
// Perspective Volume pass) is simply excluded entirely from the result -
// never a fake/empty entry for it. `"GameView"` itself is never included
// (it is never `isComputePass == true`, since it is declared via plain
// `AddPass()`/`WriteColorAttachment()`, never `AddComputePass()`).
//
// KNOWN, ACCEPTED LIMITATION: only the FIRST `Texture`-kind write per pass
// is collected - no real pass in this engine writes more than one 2D
// texture today, but if a future pass ever did, only the first would get a
// retained preview. This is a deliberate, documented simplification, not a
// silent gap.
std::vector<FrameDebuggerComputePassTextureWrite> CollectComputePassTextureWrites(
    const rg::RenderGraphSnapshot& graphSnapshot);

// frame-debugger-5 campaign, PHASE4
// (PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md, Step 3.1) - the
// VolumeTexture-kind sibling of FrameDebuggerComputePassTextureWrite/
// CollectComputePassTextureWrites() above. A SEPARATE, independent
// collection pass rather than a filter option on the same result - a pass
// could, in principle, have BOTH a Texture-kind write and a VolumeTexture-
// kind write in a future engine (not true of any real pass today), so this
// deliberately does not assume "exactly one visual write kind per pass".
struct FrameDebuggerComputePassVolumeTextureWrite {
    std::string passName;
    std::string writeVolumeTextureName;
};

// Pure, CPU-side discovery (no live VkDevice/Renderer/RenderTexture/
// VolumeTexturePreviewRenderer involved) - walks
// `graphSnapshot.passesInExecutionOrder` directly, exactly like
// CollectComputePassTextureWrites() above, and collects one
// {pass.name, pass.writeNames[i]} pair for every real, SURVIVING
// (`isCulled == false`) compute-dispatch (`isComputePass == true`) pass
// whose FIRST `writeKinds[i] == rg::ResourceKind::VolumeTexture` write is
// found. A pass with no `VolumeTexture`-kind write at all (e.g. a
// `Texture`-kind write, or a `Buffer`-kind write) is simply excluded
// entirely from this result - never a fake/empty entry for it.
//
// KNOWN, ACCEPTED LIMITATION (mirrors CollectComputePassTextureWrites()'s
// own identical limitation): only the FIRST `VolumeTexture`-kind write per
// pass is collected.
std::vector<FrameDebuggerComputePassVolumeTextureWrite> CollectComputePassVolumeTextureWrites(
    const rg::RenderGraphSnapshot& graphSnapshot);

// frame-debugger-4 campaign, PHASE3
// (PHASE3_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md, Step 3.1) - the
// one genuinely PURE piece of decision logic buried inside PHASE1's own
// Panels/FrameDebuggerPanel.cpp EnsurePreviewDescriptor(): given whether a
// history entry exists at all, whether its retained textures
// (`preview`/`compositedPreview`/a selected compute-dispatch leaf's own
// retained preview) are actually available, and whether the
// currently-selected tree event is literally the "GameView" leaf, decide
// WHICH retained texture (if any) should be displayed.
// Extracted here - rather than left inline in that Tier-2, ImGui/Vulkan-
// coupled function - specifically so this one rule (Locked Design
// Decisions #5/#6, PHASE0_MASTER_STRATEGY.md) is directly Tier-1-testable
// with no live VkDevice/ImGui context at all; see
// tests/Editor/FrameDebuggerDataTests.cpp for the full set of covered input
// combinations.
//
// frame-debugger-5 campaign, PHASE3
// (PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md, Step 3.4) - grows one
// new case, `ComputePassPreview` - the currently-SELECTED leaf's own
// retained compute-pass output texture (FrameDebuggerHistoryEntry::
// computePassPreviews, FrameDebuggerHistory.h). Every OTHER existing input
// combination's existing output is UNCHANGED by this addition.
enum class FrameDebuggerPreviewSourceChoice {
    None, // Nothing to preview - no entry at all, or the "GameView" leaf is
        // selected but its own `preview` is (defensively) unpopulated.
    Preview, // The true pre-atmosphere-composite "GameView" copy.
    CompositedPreview, // The true post-atmosphere-composite, final image.
    ComputePassPreview, // NEW (PHASE3) - the selected leaf's own retained
        // compute-pass output texture.
};

// Pure decision function - no FrameDebuggerHistoryEntry/RenderTexture
// dependency at all, deliberately taking already-resolved plain booleans
// instead (mirrors this codebase's own established "extract a pure
// function that takes already-resolved plain values" convention - see
// AGENTS.md's "Testability & Regression Safety"). `hasEntry` is whether a
// currently-viewed FrameDebuggerHistoryEntry exists at all (false for a
// fresh, never-captured-into FrameDebuggerHistory); `hasPreview`/
// `hasCompositedPreview` are whether that entry's own two optional retained
// textures are actually populated; `isViewingGameViewLeaf` is whether the
// currently-selected event's own FrameDebuggerEventDetails::passName is
// literally "GameView" (false whenever nothing is selected, or any other
// leaf - including a compute-dispatch leaf - is).
//
// NEW (frame-debugger-5 campaign, PHASE3) `hasSelectedComputePassPreview` -
// whether the CURRENTLY-SELECTED leaf (whatever it is) has its own entry in
// the currently-viewed FrameDebuggerHistoryEntry::computePassPreviews (i.e.
// an entry whose `passName` matches the selected leaf's own real
// `FrameDebuggerEventDetails::passName`) - resolved by the CALLER
// (Panels/FrameDebuggerPanel.cpp's EnsurePreviewDescriptor()), never by this
// function itself (which stays free of any FrameDebuggerHistoryEntry/
// RenderTexture dependency, per this function's own pre-existing
// philosophy above).
//
// Rule (Locked Design Decisions #5/#6, PLUS PHASE3's new addition): the
// literal "GameView" leaf always shows the pre-composite `preview` (never
// `compositedPreview`/a compute-pass preview, even if either IS present -
// UNCHANGED by PHASE3); otherwise, a selected leaf with its own retained
// compute-pass preview WINS (NEW - PHASE3) over the post-composite
// `compositedPreview`; failing that, anything else (including nothing
// selected) prefers the post-composite `compositedPreview`, falling back to
// `preview` only when `compositedPreview` itself is absent for this
// particular captured frame (both UNCHANGED from before PHASE3).
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry, bool hasPreview,
    bool hasCompositedPreview, bool isViewingGameViewLeaf, bool hasSelectedComputePassPreview);

} // namespace gte
