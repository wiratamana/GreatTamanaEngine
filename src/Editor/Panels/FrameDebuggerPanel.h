#pragma once

namespace gte {

struct EditorContext;

// task_manager/frame-debugger-2 campaign (PHASE2) - the Editor's
// "Frame Debugger" window: a Unity-Frame-Debugger-style tool for
// inspecting one captured frame's draw-call/render-pass hierarchy. An
// ON-DEMAND FLOATING WINDOW (like BoneViewerWindow.h), NOT part of the
// default dock layout and NOT listed in EditorPanelCatalog.h (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6) - opened/closed
// via a checkable "Window > Frame Debugger" menu item
// (DockLayout.cpp's BuildDockspaceAndMenuBar()) that flips
// EditorContext::frameDebuggerWindowOpen directly; Build() below is a
// complete no-op whenever that bool is false, mirroring
// BoneViewerWindow::Build()'s own "no-op unless open" shape exactly,
// just gated on a shared EditorContext bool instead of a private member
// (see this class's own .cpp file for why: the thing that opens this
// window, DockLayout.cpp, has no reference to a FrameDebuggerPanel
// instance to call an Open()-style method on).
//
// THIS CAMPAIGN IS GUI-ONLY (see PHASE0_MASTER_STRATEGY.md's Non-Goals) -
// every value this window displays is a disabled control, a placeholder
// message, or an inert default; see FrameDebuggerData.h for the pure
// data model this class reads from.
//
// A small STATEFUL CLASS, not a stateless free function - mirrors
// RenderGraphPanel/ProfilerPanel/BoneViewerWindow's own precedent
// (AGENTS.md, "Editor Module Structure" pre-approves this exception):
// this panel owns its own "Enable" toggle, currently-selected event
// index, and splitter width across frames. Still called explicitly BY
// NAME from ImGuiEditorLayer::BuildUI() - no IEditorPanel interface
// introduced.
class FrameDebuggerPanel {
public:
    void Build(EditorContext& ctx);
};

} // namespace gte
