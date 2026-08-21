#pragma once

#include "../ProjectPanelData.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace gte {

struct EditorContext;

// Unity/Windows-Explorer-style two-pane "Project" panel: a live view of a
// "Project" folder living right next to the built .exe (created
// automatically the first time this panel runs, if missing - see
// EnsureProjectRootExists() in ProjectPanelData.h), PLUS the ability to
// import a file/folder into it by dragging it in from the OS (Windows
// Explorer, ...) - see HandleExternalFileDrop() below. Only compiled/used
// when GTE_ENABLE_PROJECT_PANEL is ON (see the root CMakeLists.txt) - the
// whole point of that separate switch (distinct from GTE_ENABLE_EDITOR) is
// that this feature touches the real filesystem and is still actively
// evolving, so it can be turned off independently of the rest of the
// Editor.
//
// **Two panes, like Unity/Explorer:**
//   - LEFT ("folder tree"): every directory in "Project", recursively,
//     folders only (no files) - clicking one both selects it AND makes it
//     the "open"/current folder (m_currentFolderRelativePath), same as
//     clicking a folder in Explorer's left tree.
//   - RIGHT ("contents"): the immediate children (files AND subfolders) of
//     whichever folder is currently open, behind a small breadcrumb
//     showing the open folder's path. A single click just selects an entry
//     (m_selectedRelativePath, for Delete/highlight); double-clicking a
//     subfolder there navigates INTO it (same as Explorer).
// A draggable splitter (m_leftPaneWidth) sits between them, exactly like a
// normal Explorer/Unity window.
//
// **Inspector integration:** selecting ANY entry (a folder in either pane,
// or a file in the right pane) also writes ctx.selectedAsset*/
// ctx.inspectorSelectionKind (see EditorContext.h and SetAssetSelection()
// below) - this is what makes the Editor's "Inspector" panel show that
// entry's metadata (and, for a supported image file, a live texture
// preview - see Panels/InspectorPanel.cpp) the same way selecting an entity
// in "Hierarchy" makes Inspector show its components. The two selections
// (ECS entity vs. Project asset) are otherwise completely independent -
// see InspectorSelectionKind's own doc comment.
//
// This is a CLASS, not a free-function panel builder like
// HierarchyPanel/InspectorPanel/etc. (see AGENTS.md, "Editor Module
// Structure": "a future panel that genuinely needs its own persistent state
// across frames... may become a small class instead of a free function") -
// it genuinely needs to remember several things across frames: the root
// path, both panes' own last-known on-screen rects and every folder row's
// own on-screen hit-box (for hit-testing an external OS drop - see
// HandleExternalFileDrop()), a throttled/cached scan of the directory tree,
// which folder is currently open vs. selected, the splitter position, and a
// transient status message. It is still called explicitly by name from
// ImGuiEditorLayer::BuildUI()/ProcessEvent(), exactly like every
// free-function panel - there is no IEditorPanel interface here either.
//
// Deliberately never caches directory_entry/std::filesystem::path handles
// across frames, or holds a pointer/reference into a previous scan's tree -
// only ever a plain ProjectEntry snapshot (rebuilt wholesale by
// ScanProjectDirectory() on a throttle - see EnsureRootAndMaybeRescan())
// plus plain std::strings for the current selection/open folder (their OWN
// relativePath, never a pointer into the tree). This is what makes "the
// user deletes something inside Project externally while the Editor is
// running" a complete non-event: the next scan just quietly omits it, a
// selection that no longer resolves to anything simply stops being
// highlighted, and an open folder that vanished is walked back up to its
// nearest still-existing ancestor (see ReconcileCurrentFolderAfterRescan())
// - there is nothing anywhere holding a stale handle that could ever be
// dereferenced.
class ProjectPanel {
public:
    ProjectPanel();

    // Builds the "Project" ImGui window for this frame - call once per
    // frame from ImGuiEditorLayer::BuildUI() (gated behind
    // `#if GTE_ENABLE_PROJECT_PANEL`). `ctx` is written whenever the user
    // selects an entry (see the class comment above) so InspectorPanel can
    // show it - see EditorContext::selectedAssetAbsolutePath.
    void Build(EditorContext& ctx);

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
    // Resolves the actual destination folder via ProjectPanelData's
    // ResolveDropTarget() (see its own doc comment for the exact
    // left-pane/right-pane/specific-folder-row priority rules) using
    // whatever this panel recorded while rendering its LAST visible frame -
    // a no-op if that resolves to nothing at all (the drop landed outside
    // both panes entirely - the toolbar, the splitter, ...), or if the
    // Project root itself doesn't currently exist and couldn't be
    // recreated. Copies (never moves) the dropped item into the resolved
    // folder, auto-renaming to avoid clobbering an existing same-named item
    // (see MakeUniqueDestinationPath()). Never throws - every failure
    // (source vanished before the copy could start, a permissions error
    // mid-copy, ...) is reported only as a transient in-panel status
    // message (see Build()), exactly like every other failure mode this
    // panel handles.
    void HandleExternalFileDrop(float screenX, float screenY, const std::string& sourcePathUtf8);

private:
    void EnsureRootAndMaybeRescan();
    void ReconcileCurrentFolderAfterRescan();

    void RenderLeftPane(EditorContext& ctx);
    void RenderLeftPaneFolder(EditorContext& ctx, const ProjectEntry& entry);
    void RenderRightPane(EditorContext& ctx);
    void RenderRightPaneEntry(EditorContext& ctx, const ProjectEntry& entry);
    void RenderBreadcrumb();
    void RenderContextMenu(EditorContext& ctx, const char* popupId);
    void RecordFolderDropZone(const std::string& relativePath);

    const std::vector<ProjectEntry>* CurrentFolderChildren() const;

    void SetStatus(const std::string& message, bool isError);
    void CreateNewFolder();
    void DeleteSelected(EditorContext& ctx);

    // Writes `relativePath`/`isDirectory` into both this panel's own
    // selection (m_selectedRelativePath) AND ctx's asset-inspector fields
    // (ctx.selectedAsset*/ctx.inspectorSelectionKind) - the one place that
    // does both together, so every selection site below (left-pane folder
    // click, right-pane single/double-click, the "Project" root node)
    // can never let the two drift apart. `relativePath` empty means the
    // Project root itself.
    void SetAssetSelection(EditorContext& ctx, const std::string& relativePath, bool isDirectory);

    std::filesystem::path m_rootPath;
    bool m_rootExists = false;

    std::vector<ProjectEntry> m_tree;
    std::chrono::steady_clock::time_point m_lastScanTime{};
    bool m_needsRescan = true;

    // Which folder is currently "open" - its immediate children are what
    // the right pane shows (empty string means the Project root itself).
    // Written by clicking a folder in the left pane, or double-clicking a
    // subfolder in the right pane. Reconciled after every rescan (see
    // ReconcileCurrentFolderAfterRescan()) by walking up to the nearest
    // still-existing ancestor, so a deleted-out-from-under-you open folder
    // degrades gracefully instead of showing a permanently empty pane.
    std::string m_currentFolderRelativePath;

    // The currently selected entry's OWN relativePath (see ProjectEntry) -
    // never a pointer/index into m_tree, so a rescan that reshuffles/drops
    // entries can never leave this dangling; it just stops matching
    // anything (see RenderRightPaneEntry()/RenderLeftPaneFolder()). Distinct
    // from m_currentFolderRelativePath: selecting a file (or a subfolder,
    // via a single click) in the right pane does NOT navigate into it -
    // only double-clicking a subfolder there, or clicking a folder in the
    // left pane, does. Kept in sync with EditorContext::
    // selectedAssetRelativePath by SetAssetSelection() above.
    std::string m_selectedRelativePath;

    // The draggable splitter's left-pane width, in pixels - persisted
    // across frames like a normal Explorer/Unity window remembers its own
    // split position for the session. Clamped every frame in Build() to
    // stay sane as the window itself is resized.
    float m_leftPaneWidth = 220.0f;

    // Every folder row's own on-screen hit-box for whichever frame was last
    // rendered (left-pane tree rows AND right-pane subfolder rows alike),
    // plus each pane's own overall rect - rebuilt from scratch at the start
    // of every visible Build() call, read by HandleExternalFileDrop() (via
    // ProjectPanelData::ResolveDropTarget()) to route an incoming OS drop
    // to the right specific folder.
    std::vector<FolderDropZone> m_folderDropZones;
    Rect m_leftPaneRect;
    Rect m_rightPaneRect;

    bool m_panelVisible = false;

    // A short-lived line shown at the bottom of the panel after an
    // operation (import/New Folder/Delete/a failure) - self-clears a few
    // seconds after being set (see Build()), so it never needs an explicit
    // dismiss button.
    std::string m_statusMessage;
    bool m_statusIsError = false;
    std::chrono::steady_clock::time_point m_statusSetTime{};
};

} // namespace gte
