#pragma once

#include "../ProjectPanelData.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace gte {

// Unity-style "Project" panel: a live view of a "Project" folder living
// right next to the built .exe (created automatically the first time this
// panel runs, if missing - see EnsureProjectRootExists() in
// ProjectPanelData.h), PLUS the ability to import a file/folder into it by
// dragging it in from the OS (Windows Explorer, ...) - see
// HandleExternalFileDrop() below. Only compiled/used when
// GTE_ENABLE_PROJECT_PANEL is ON (see the root CMakeLists.txt) - the whole
// point of that separate switch (distinct from GTE_ENABLE_EDITOR) is that
// this feature touches the real filesystem and is still actively evolving,
// so it can be turned off independently of the rest of the Editor.
//
// This is a CLASS, not a free-function panel builder like
// HierarchyPanel/InspectorPanel/etc. (see AGENTS.md, "Editor Module
// Structure": "a future panel that genuinely needs its own persistent state
// across frames... may become a small class instead of a free function") -
// it genuinely needs to remember several things across frames: the root
// path, its own last-known on-screen rect (for hit-testing an external OS
// drop - see HandleExternalFileDrop()), a throttled/cached scan of the
// directory tree, the currently selected entry, and a transient status
// message. It is still called explicitly by name from
// ImGuiEditorLayer::BuildUI()/ProcessEvent(), exactly like every
// free-function panel - there is no IEditorPanel interface here either.
//
// Deliberately never caches directory_entry/std::filesystem::path handles
// across frames, or holds a pointer/reference into a previous scan's tree -
// only ever a plain ProjectEntry snapshot (rebuilt wholesale by
// ScanProjectDirectory() on a throttle - see EnsureRootAndMaybeRescan())
// plus a plain std::string for the current selection (its OWN
// relativePath, never a pointer into the tree). This is what makes "the
// user deletes something inside Project externally while the Editor is
// running" a complete non-event: the next scan just quietly omits it, and a
// selection that no longer resolves to anything simply stops being
// highlighted/resolves to the Project root (see ResolveDropTargetDirectory())
// - there is nothing anywhere holding a stale handle that could ever be
// dereferenced.
class ProjectPanel {
public:
    ProjectPanel();

    // Builds the "Project" ImGui window for this frame - call once per
    // frame from ImGuiEditorLayer::BuildUI() (gated behind
    // `#if GTE_ENABLE_PROJECT_PANEL`).
    void Build();

    // Called by ImGuiEditorLayer::ProcessEvent() whenever the OS reports a
    // file (or folder) dropped onto ANY of this process's windows (the main
    // window, or an ImGui multi-viewport "platform window" - see
    // ImGuiEditorLayer.cpp) - SDL_EVENT_DROP_FILE. `screenX`/`screenY` are
    // that drop's position in absolute desktop/screen coordinates (the
    // caller has already added the source SDL window's own screen position
    // to the event's window-relative coordinates), and `sourcePathUtf8` is
    // the absolute path (UTF-8, as SDL always provides) of the dropped
    // file/folder on disk.
    //
    // A no-op if that position doesn't fall within this panel's own
    // last-known on-screen rect (recorded during Build() - i.e. the drop
    // landed somewhere else entirely, not on "Project"), or if the Project
    // root itself doesn't currently exist and couldn't be recreated. Copies
    // (never moves) the dropped item into whichever folder is currently
    // selected in "Project" (or the Project root, if nothing/a file is
    // selected - see ResolveDropTargetDirectory()), auto-renaming to avoid
    // clobbering an existing same-named item (see MakeUniqueDestinationPath()).
    // Never throws - every failure (source vanished before the copy could
    // start, a permissions error mid-copy, ...) is reported only as a
    // transient in-panel status message (see Build()), exactly like every
    // other failure mode this panel handles.
    void HandleExternalFileDrop(float screenX, float screenY, const std::string& sourcePathUtf8);

private:
    void EnsureRootAndMaybeRescan();
    void RenderEntry(const ProjectEntry& entry);
    void RenderContextMenu();
    void SetStatus(const std::string& message, bool isError);
    void CreateNewFolder();
    void DeleteSelected();

    std::filesystem::path m_rootPath;
    bool m_rootExists = false;

    std::vector<ProjectEntry> m_tree;
    std::chrono::steady_clock::time_point m_lastScanTime{};
    bool m_needsRescan = true;

    // The currently selected entry's OWN relativePath (see ProjectEntry) -
    // never a pointer/index into m_tree, so a rescan that reshuffles/drops
    // entries can never leave this dangling; it just stops matching
    // anything (see RenderEntry()) or gracefully resolves to the Project
    // root (see ResolveDropTargetDirectory()).
    std::string m_selectedRelativePath;

    // This panel's own last-known on-screen rect (absolute desktop/screen
    // coordinates, matching how HandleExternalFileDrop()'s screenX/screenY
    // are computed) and whether it was actually visible last frame -
    // recorded at the top of Build(), read by HandleExternalFileDrop() to
    // hit-test an incoming OS drop.
    bool m_panelVisible = false;
    float m_panelScreenX = 0.0f;
    float m_panelScreenY = 0.0f;
    float m_panelScreenWidth = 0.0f;
    float m_panelScreenHeight = 0.0f;

    // A short-lived line shown at the bottom of the panel after an
    // operation (import/New Folder/Delete/a failure) - self-clears a few
    // seconds after being set (see Build()), so it never needs an explicit
    // dismiss button.
    std::string m_statusMessage;
    bool m_statusIsError = false;
    std::chrono::steady_clock::time_point m_statusSetTime{};
};

} // namespace gte
