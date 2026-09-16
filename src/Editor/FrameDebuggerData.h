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
// populated for the CURRENT frame by the caller; `gpuSkinningPassNamesThisFrame`
// - the CURRENT frame's real AnimationSystem::GpuSkinningDispatchRequest::name
// values, already resolved to plain strings by the caller so this function
// itself never needs to #include AnimationSystem.h) into a real,
// Game-View-only FrameDebuggerSnapshot - never a competing capture
// mechanism. No live VkDevice/Renderer/RenderTexture involved - exactly
// like BuildRenderGraphSnapshot() itself.
//
// Returns an EMPTY FrameDebuggerSnapshot{} (same honest "no frame captured
// yet" shape BuildPlaceholderFrameDebuggerSnapshot() already models) if
// `graphSnapshot` has no pass literally named "GameView" - e.g. queried
// before the very first frame ever rendered.
//
// Tree shape produced otherwise: one root group node "Game View", with an
// optional "GPU Skinning" child group (present only when
// `gpuSkinningPassNamesThisFrame` is non-empty, containing one LEAF per
// matching real pass, in `graphSnapshot`'s own execution order) followed by
// exactly one LEAF for the real "GameView" pass itself, followed by an
// OPTIONAL final LEAF for the real "AtmosphereAerialPerspectiveCompositePass"
// (present only when that exact pass name is found in `graphSnapshot` this
// frame) - see PHASE0_MASTER_STRATEGY.md's Locked Design Decisions #1/#2/#6/#7
// for the full reasoning (pass-level granularity, pass-scoped facts,
// Game-View-only scope), and the `frame-debugger-4` campaign's own
// PHASE0_MASTER_STRATEGY.md for why this specific trailing leaf exists.
//
// `gameViewRenderTargetInfo` is DELIBERATELY a plain, already-resolved
// parameter rather than this function reaching into a live RenderTexture/
// Renderer itself - the phase document's own Step 3.1 point 4 asks for real
// width/height/format info, but this function must stay pure (no live
// VkDevice/Renderer), exactly like the `gpuSkinningPassNamesThisFrame`
// parameter immediately above resolves the same tension for GPU-skinning
// pass names. Only `width`/`height`/`format` are actually used from it -
// `name` is always overwritten to "GameView" for a non-empty result (left
// completely untouched - the caller's argument is simply ignored - for the
// empty-result "no GameView pass" case, matching
// BuildPlaceholderFrameDebuggerSnapshot()'s own all-default convention).
// Defaults to an all-default FrameDebuggerRenderTargetInfo{} so a caller
// that does not yet have real live extent/format data on hand (e.g. every
// Tier-1 test in tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp) can
// simply omit it.
FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(
    const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture,
    const std::vector<std::string>& gpuSkinningPassNamesThisFrame,
    const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo = FrameDebuggerRenderTargetInfo{});

} // namespace gte
