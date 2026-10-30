#pragma once

#include "../ProjectPanelData.h"
#include "../ProjectScanRunner.h"
#include "../../Assets/AssetImporter.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace gte {

struct EditorContext;

// Two-pane "Project" panel: folder tree left, open folder right; imports files dropped from the OS.
// Scans on a job worker, only while visible. Compiled only when GTE_ENABLE_PROJECT_PANEL is ON.
class ProjectPanel {
public:
    ProjectPanel();

    // Builds the ImGui window. Call once per frame; selecting an entry writes ctx.selection.
    void Build(EditorContext& ctx);

    // Imports a file or folder dropped from the OS at absolute desktop (screenX, screenY).
    // Drops outside both panes are ignored; names never overwrite; failures show as a status line.
    void HandleExternalFileDrop(float screenX, float screenY, const std::string& sourcePathUtf8);

    // Imports one file into `destinationRelativeFolder` of the Project root. Rejects absolute,
    // drive-relative and ".."-escaping folders without touching the disk.
    AssetImportResult ImportExternalFile(
        const std::filesystem::path& sourceAbsolutePath, const std::string& destinationRelativeFolder);

    const std::filesystem::path& GetRootPath() const noexcept { return m_rootPath; }

private:
    // Applies a finished scan, then starts the next one when due. Main thread, visible panel only.
    void PumpScan();
    void ApplyScanResult(ProjectScanResult&& result);
    void ReconcileCurrentFolderAfterRescan();

    void RenderLeftPane(EditorContext& ctx);
    void RenderLeftPaneFolder(EditorContext& ctx, int siblingIndex, const ProjectEntry& entry);
    void RenderRightPane(EditorContext& ctx);
    void RenderRightPaneEntry(EditorContext& ctx, int entryIndex, const ProjectEntry& entry);
    void RenderBreadcrumb();
    void RenderContextMenu(EditorContext& ctx, const char* popupId);
    void RecordFolderDropZone(const std::string& relativePath);

    // The "[Active Project] <Name>" row with its Create submenu.
    void RenderActiveProjectAssetsRow(EditorContext& ctx);

    const std::vector<ProjectEntry>* CurrentFolderChildren() const;

    void SetStatus(const std::string& message, bool isError);
    void CreateNewFolder();
    void DeleteSelected(EditorContext& ctx);

    // Single place that writes the Project selection. Empty `relativePath` = the Project root.
    void SetAssetSelection(EditorContext& ctx, const std::string& relativePath, bool isDirectory);

    std::filesystem::path m_rootPath;
    bool m_rootExists = false;

    // Latest scan snapshot. Replaced only by PumpScan(), before any pane is drawn.
    std::vector<ProjectEntry> m_tree;
    ProjectScanRunner m_scan; // Owns the job; its destructor waits for it.
    bool m_hasScanResult = false;

    // Open folder shown in the right pane; empty = root. Walked up to an existing ancestor after each scan.
    std::string m_currentFolderRelativePath;

    // Left-pane width in pixels; clamped every frame.
    float m_leftPaneWidth = 220.0f;

    // Folder row hit-boxes and pane rects of the last visible frame, used to route OS drops.
    std::vector<FolderDropZone> m_folderDropZones;
    Rect m_leftPaneRect;
    Rect m_rightPaneRect;

    bool m_panelVisible = false;

    // Short-lived status line; clears itself after a few seconds.
    std::string m_statusMessage;
    bool m_statusIsError = false;
    std::chrono::steady_clock::time_point m_statusSetTime{};

    // One-level, display-only listing of the active project's Assets folder.
    std::string m_linkedFolderName; // Empty = no active project.
    std::vector<std::string> m_linkedFileNames;

    // Set while the active-project row's popup is open, so the pane menu does not open on top of it.
    bool m_suppressPaneContextMenuThisFrame = false;
};

} // namespace gte
