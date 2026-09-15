#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

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

} // namespace gte
