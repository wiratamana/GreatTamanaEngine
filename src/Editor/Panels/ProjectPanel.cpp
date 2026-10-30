#include "ProjectPanel.h"

#include "../ActiveProjectAssemblyState.h"
#include "../EditorContext.h"
#include "../ImGuiUniqueId.h"
#include "../MemoryPanelData.h" // FormatBytes()
#include "../ProjectRootPath.h"
#include "../../Assets/AssetDatabase.h"
#include "../../Assets/AssetImporter.h"
#include "../../Core/EditorCapabilities.h" // AssetScaffoldKind
#include "../../Core/Logging.h"
#include "../../Encoding/Utf8Path.h"
#include "../../Profiling/ScopeTimer.h"

#include <imgui.h>

#include <algorithm>
#include <exception>
#include <system_error>

namespace gte {

namespace {

constexpr std::chrono::milliseconds kStatusMessageLifetime{ 4000 };

constexpr float kSplitterWidth = 6.0f;
constexpr float kMinPaneWidth = 100.0f;
constexpr float kFooterReserve = 26.0f; // Space always left below both panes for the status line.

void LogClusterMessage(const AssetImportResult& result)
{
    if (result.clusterMessage.empty()) {
        return;
    }
    if (result.clusterMessage.find("Warning:") != std::string::npos) {
        GTE_LOG_WARNING("MeshClustering", result.clusterMessage);
    } else {
        GTE_LOG_INFO("MeshClustering", result.clusterMessage);
    }
}

bool HasSubfolders(const ProjectEntry& entry)
{
    return std::any_of(entry.children.begin(), entry.children.end(), [](const ProjectEntry& child) {
        return child.isDirectory;
    });
}

} // namespace

ProjectPanel::ProjectPanel()
{
    m_rootPath = ResolveProjectRootDirectory();
}

void ProjectPanel::PumpScan()
{
    const auto now = ProjectScanRunner::Clock::now();
    if (std::optional<ProjectScanResult> done = m_scan.Poll(now)) {
        GTE_PROFILE_SCOPE("Project.ApplyScan");
        ApplyScanResult(std::move(*done));
    }
    if (!m_scan.IsDue(now)) {
        return;
    }
    GTE_PROFILE_SCOPE("Project.StartScan");
    ProjectScanRequest request;
    request.rootPath = m_rootPath;
    ActiveProjectSource active = ActiveProjectAssemblyState::Instance().GetActiveSource();
    if (active.hasActiveProject) {
        request.linkedFolderName = std::move(active.name);
        request.linkedFolderPath = std::move(active.assetsDirectory);
    }
    m_scan.Start(std::move(request));
}

void ProjectPanel::ApplyScanResult(ProjectScanResult&& result)
{
    if (result.failed) {
        SetStatus("Project scan failed: " + result.errorMessage, true); // Keep the last good tree.
        return;
    }
    m_hasScanResult = true;
    m_rootExists = result.rootExists;
    m_tree = std::move(result.tree);
    m_linkedFolderName = std::move(result.linkedFolderName);
    m_linkedFileNames = std::move(result.linkedFileNames);
    ReconcileCurrentFolderAfterRescan();
}

void ProjectPanel::ReconcileCurrentFolderAfterRescan()
{
    if (!m_rootExists) {
        m_currentFolderRelativePath.clear();
        return;
    }

    // Walk up to the nearest ancestor that still exists as a folder. The root (empty path) always ends the loop.
    while (!m_currentFolderRelativePath.empty()) {
        const ProjectEntry* found = FindEntryByRelativePath(m_tree, m_currentFolderRelativePath);
        if (found != nullptr && found->isDirectory) {
            return;
        }
        m_currentFolderRelativePath = ParentRelativePath(m_currentFolderRelativePath);
    }
}

const std::vector<ProjectEntry>* ProjectPanel::CurrentFolderChildren() const
{
    if (m_currentFolderRelativePath.empty()) {
        return &m_tree;
    }

    const ProjectEntry* folder = FindEntryByRelativePath(m_tree, m_currentFolderRelativePath);
    return (folder != nullptr && folder->isDirectory) ? &folder->children : &m_tree;
}

void ProjectPanel::SetStatus(const std::string& message, bool isError)
{
    m_statusMessage = message;
    m_statusIsError = isError;
    m_statusSetTime = std::chrono::steady_clock::now();
}

void ProjectPanel::SetAssetSelection(EditorContext& ctx, const std::string& relativePath, bool isDirectory)
{
    ctx.selection.SelectAsset(
        PathToUtf8(relativePath.empty() ? m_rootPath : (m_rootPath / PathFromUtf8(relativePath))),
        relativePath,
        isDirectory);
}

void ProjectPanel::RecordFolderDropZone(const std::string& relativePath)
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_folderDropZones.push_back(FolderDropZone{ relativePath, Rect{ min.x, min.y, max.x - min.x, max.y - min.y } });
}

void ProjectPanel::RenderLeftPaneFolder(EditorContext& ctx, int siblingIndex, const ProjectEntry& entry)
{
    if (!entry.isDirectory) {
        return; // The left pane lists folders only.
    }

    ScopedUniqueId idScope(siblingIndex, "ProjectPanel::RenderLeftPaneFolder", entry.relativePath.c_str());

    const bool hasSubfolders = HasSubfolders(entry);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (entry.relativePath == m_currentFolderRelativePath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasSubfolders) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    }

    const bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
    RecordFolderDropZone(entry.relativePath);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        m_currentFolderRelativePath = entry.relativePath;
        SetAssetSelection(ctx, entry.relativePath, true);
    }

    if (hasSubfolders && open) {
        for (std::size_t i = 0; i < entry.children.size(); ++i) {
            RenderLeftPaneFolder(ctx, static_cast<int>(i), entry.children[i]);
        }
        ImGui::TreePop();
    }
}

// Sibling of the "Project" root node; draws nothing when no project is active.
void ProjectPanel::RenderActiveProjectAssetsRow(EditorContext& ctx)
{
    if (m_linkedFolderName.empty()) {
        return;
    }

    const std::string label = "[Active Project] " + m_linkedFolderName;
    ImGui::TreeNodeEx(label.c_str(),
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);

    // BeginPopupContextItem renders on every frame the popup is open, so the flag below
    // is set for the popup's whole lifetime, not just the frame it opened.
    if (ImGui::BeginPopupContextItem("ProjectAssemblySourceContextMenu")) {
        m_suppressPaneContextMenuThisFrame = true;
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Render Pass...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::RenderPass;
                ctx.createAssetWindowOpen = true;
            }
            if (ImGui::MenuItem("Compute Shader...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ComputeShader;
                ctx.createAssetWindowOpen = true;
            }
            if (ImGui::MenuItem("Vertex/Fragment Shader Pair...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ShaderPair;
                ctx.createAssetWindowOpen = true;
            }
            if (ImGui::MenuItem("Screen Post-Process Pass...")) {
                ctx.createAssetWindowPendingKind = AssetScaffoldKind::ScreenPostProcessPass;
                ctx.createAssetWindowOpen = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::TreePush("ProjectAssemblySourceChildren");
    for (const std::string& fileName : m_linkedFileNames) {
        ImGui::BulletText("%s", fileName.c_str()); // Display only: no select, click or drag.
    }
    ImGui::TreePop();

    ImGui::TreePop(); // Matches the outer TreeNodeEx (DefaultOpen: always open).
}

void ProjectPanel::RenderLeftPane(EditorContext& ctx)
{
    RenderActiveProjectAssetsRow(ctx);
    ImGuiTreeNodeFlags rootFlags
        = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (m_currentFolderRelativePath.empty()) {
        rootFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool rootOpen = ImGui::TreeNodeEx("Project", rootFlags);
    RecordFolderDropZone(std::string());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        m_currentFolderRelativePath.clear();
        SetAssetSelection(ctx, std::string(), true);
    }

    if (rootOpen) {
        for (std::size_t i = 0; i < m_tree.size(); ++i) {
            RenderLeftPaneFolder(ctx, static_cast<int>(i), m_tree[i]);
        }
        ImGui::TreePop();
    }
}

void ProjectPanel::RenderBreadcrumb()
{
    if (ImGui::SmallButton("Project")) {
        m_currentFolderRelativePath.clear();
    }

    std::string accumulated;
    std::size_t start = 0;
    int segmentIndex = 0;
    while (start <= m_currentFolderRelativePath.size() && start != std::string::npos) {
        const std::size_t slash = m_currentFolderRelativePath.find('/', start);
        const std::string segment
            = m_currentFolderRelativePath.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (segment.empty()) {
            break; // Empty path: only the "Project" root button is shown.
        }
        accumulated = accumulated.empty() ? segment : accumulated + "/" + segment;

        ImGui::SameLine();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        {
            ScopedUniqueId idScope(segmentIndex++, "ProjectPanel::RenderBreadcrumb", segment.c_str());
            if (ImGui::SmallButton(segment.c_str())) {
                m_currentFolderRelativePath = accumulated;
            }
        }

        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
}

void ProjectPanel::RenderRightPaneEntry(EditorContext& ctx, int entryIndex, const ProjectEntry& entry)
{
    ScopedUniqueId idScope(entryIndex, "ProjectPanel::RenderRightPaneEntry", entry.relativePath.c_str());

    const bool isSelected = ctx.selection.IsAssetSelected(entry.relativePath);
    const std::string label = entry.isDirectory ? ("[Folder] " + entry.name) : entry.name;
    ImGui::Selectable(label.c_str(), isSelected);

    // Files drag out as their absolute UTF-8 path, so a receiving panel needs no knowledge of the Project root.
    if (!entry.isDirectory && ImGui::BeginDragDropSource()) {
        const std::string absolutePathUtf8 = PathToUtf8(m_rootPath / PathFromUtf8(entry.relativePath));
        ImGui::SetDragDropPayload(
            kProjectAssetDragDropPayloadType, absolutePathUtf8.c_str(), absolutePathUtf8.size() + 1);
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (entry.isDirectory) {
        RecordFolderDropZone(entry.relativePath);
    }

    if (entry.isDirectory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // Double-click opens the folder, same as clicking it in the left pane.
        m_currentFolderRelativePath = entry.relativePath;
        SetAssetSelection(ctx, entry.relativePath, true);
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        SetAssetSelection(ctx, entry.relativePath, entry.isDirectory);
    }

    if (!entry.isDirectory && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", FormatBytes(entry.sizeBytes).c_str());
    }
}

void ProjectPanel::RenderRightPane(EditorContext& ctx)
{
    RenderBreadcrumb();
    ImGui::Separator();

    const std::vector<ProjectEntry>* children = CurrentFolderChildren();
    if (children == nullptr || children->empty()) {
        ImGui::TextDisabled("(empty - drag files in from Explorer, or right-click for options)");
        return;
    }

    for (std::size_t i = 0; i < children->size(); ++i) {
        RenderRightPaneEntry(ctx, static_cast<int>(i), (*children)[i]);
    }
}

void ProjectPanel::CreateNewFolder()
{
    const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, m_currentFolderRelativePath);
    const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, PathFromUtf8("New Folder"));

    std::error_code ec;
    std::filesystem::create_directory(destination, ec);
    if (ec) {
        SetStatus("Failed to create folder: " + ec.message(), true);
    } else {
        SetStatus("Created \"" + PathToUtf8(destination.filename()) + "\".", false);
        m_scan.MarkDirty();
    }
}

void ProjectPanel::DeleteSelected(EditorContext& ctx)
{
    if (!ctx.selection.HasAssetSelection()) {
        return;
    }

    const std::string selectedDisplay = ctx.selection.SelectedAssetRelativePath();

    const std::filesystem::path target = m_rootPath / PathFromUtf8(selectedDisplay);

    std::error_code ec;
    const std::uintmax_t removedCount = std::filesystem::remove_all(target, ec);

    if (ec) {
        SetStatus("Failed to delete \"" + selectedDisplay + "\": " + ec.message(), true);
    } else if (removedCount == 0) {
        SetStatus("\"" + selectedDisplay + "\" no longer exists.", true);
    } else {
        SetStatus("Deleted \"" + selectedDisplay + "\".", false);
    }

    // Drop the selection if it pointed at the deleted item.
    ctx.selection.ClearAssetIfPath(selectedDisplay);

    // An open folder that was deleted is walked up to an existing ancestor after the next scan.
    m_scan.MarkDirty();
}

void ProjectPanel::RenderContextMenu(EditorContext& ctx, const char* popupId)
{
    if (m_suppressPaneContextMenuThisFrame) {
        return; // The active-project row's popup owns the right click this frame.
    }
    if (ImGui::BeginPopupContextWindow(popupId)) {
        if (ImGui::MenuItem("Refresh")) {
            m_scan.MarkDirty();
        }
        if (ImGui::MenuItem("New Folder", nullptr, false, m_rootExists)) {
            CreateNewFolder();
        }
        ImGui::Separator();
        const bool hasSelection = ctx.selection.HasAssetSelection();
        if (ImGui::MenuItem("Delete Selected", nullptr, false, hasSelection && m_rootExists)) {
            DeleteSelected(ctx);
        }
        ImGui::EndPopup();
    }
}

void ProjectPanel::Build(EditorContext& ctx)
{
    m_suppressPaneContextMenuThisFrame = false;

    const bool wasVisible = m_panelVisible;
    m_panelVisible = ImGui::Begin("Project");
    if (m_panelVisible) {
        if (!wasVisible) {
            m_scan.MarkDirty(); // Data may be old after a hidden period.
        }
        PumpScan(); // Before any pane is drawn: it may replace m_tree.
        m_folderDropZones.clear();
        m_leftPaneRect = Rect{};
        m_rightPaneRect = Rect{};

        if (ImGui::Button("Refresh")) {
            m_scan.MarkDirty();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", PathToUtf8(m_rootPath).c_str());

        ImGui::Separator();

        if (!m_hasScanResult) {
            ImGui::TextDisabled("Scanning...");
        } else if (!m_rootExists) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "The \"Project\" folder is missing and could not be (re)created:");
            ImGui::TextWrapped("%s", PathToUtf8(m_rootPath).c_str());
            ImGui::TextDisabled("(check that this location is writable, then click Refresh)");
        } else {
            // Keep the splitter inside the window as it is resized.
            const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
            const float maxLeftWidth = std::max(kMinPaneWidth, totalAvailWidth - kMinPaneWidth - kSplitterWidth);
            m_leftPaneWidth = std::clamp(m_leftPaneWidth, kMinPaneWidth, maxLeftWidth);

            const float paneAreaHeight = std::max(ImGui::GetContentRegionAvail().y - kFooterReserve, 40.0f);

            ImGui::BeginChild("ProjectLeftPane", ImVec2(m_leftPaneWidth, paneAreaHeight), true);
            RenderLeftPane(ctx);
            RenderContextMenu(ctx, "ProjectLeftPaneContextMenu");
            {
                const ImVec2 childPos = ImGui::GetWindowPos();
                const ImVec2 childSize = ImGui::GetWindowSize();
                m_leftPaneRect = Rect{ childPos.x, childPos.y, childSize.x, childSize.y };
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::Button("##ProjectPaneSplitter", ImVec2(kSplitterWidth, paneAreaHeight));
            if (ImGui::IsItemActive()) {
                m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            ImGui::SameLine();

            ImGui::BeginChild("ProjectRightPane", ImVec2(0.0f, paneAreaHeight), true);
            RenderRightPane(ctx);
            RenderContextMenu(ctx, "ProjectRightPaneContextMenu");
            {
                const ImVec2 childPos = ImGui::GetWindowPos();
                const ImVec2 childSize = ImGui::GetWindowSize();
                m_rightPaneRect = Rect{ childPos.x, childPos.y, childSize.x, childSize.y };
            }
            ImGui::EndChild();
        }

        if (!m_statusMessage.empty()) {
            if (std::chrono::steady_clock::now() - m_statusSetTime < kStatusMessageLifetime) {
                const ImVec4 color
                    = m_statusIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 0.85f, 0.6f, 1.0f);
                ImGui::TextColored(color, "%s", m_statusMessage.c_str());
            } else {
                m_statusMessage.clear();
            }
        }
    }
    ImGui::End();
}

void ProjectPanel::HandleExternalFileDrop(float screenX, float screenY, const std::string& sourcePathUtf8)
{
    if (!m_panelVisible || !m_hasScanResult || sourcePathUtf8.empty()) {
        return; // Panel not on screen, or no drop zones recorded yet.
    }

    if (!m_rootExists) {
        SetStatus("Cannot import - the \"Project\" folder is missing.", true);
        return;
    }

    const std::optional<std::string> target = ResolveDropTarget(
        m_folderDropZones, screenX, screenY, m_leftPaneRect, m_rightPaneRect, m_currentFolderRelativePath);
    if (!target.has_value()) {
        return; // Landed outside both panes.
    }

    try {
        const std::filesystem::path source = PathFromUtf8(sourcePathUtf8);

        std::error_code existsEc;
        if (!std::filesystem::exists(source, existsEc) || existsEc) {
            SetStatus("The dropped item no longer exists.", true);
            return;
        }

        const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, *target);

        std::error_code isDirEc;
        const bool sourceIsDirectory = std::filesystem::is_directory(source, isDirEc) && !isDirEc;

        // Name collisions are checked against the name the import will actually write.
        const std::filesystem::path desiredName = sourceIsDirectory ? source.filename() : ImportedFileName(source);
        const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, desiredName);

        if (sourceIsDirectory) {
            std::error_code copyEc;
            std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, copyEc);
            if (copyEc) {
                SetStatus("Failed to import \"" + PathToUtf8(source.filename()) + "\": " + copyEc.message(), true);
            } else {
                SetStatus("Imported \"" + PathToUtf8(destination.filename()) + "\" into Project.", false);
                m_scan.MarkDirty();
            }
        } else {
            AssetDatabase database; // Scratch: ImportAssetFile() needs somewhere to register.
            const AssetImportResult result = ImportAssetFile(database, source, destination, MeshImportOptions{});
            LogClusterMessage(result);
            SetStatus(result.message, !result.success);
            if (result.success) {
                m_scan.MarkDirty();
            }
        }
    } catch (const std::exception& e) {
        // The source is outside engine control (removable or network drive); never crash the editor.
        SetStatus(std::string("Failed to import dropped item: ") + e.what(), true);
    }
}

AssetImportResult ProjectPanel::ImportExternalFile(
    const std::filesystem::path& sourceAbsolutePath, const std::string& destinationRelativeFolder)
{
    AssetImportResult result;

    if (!EnsureProjectRootExists(m_rootPath)) {
        result.success = false;
        result.message = "Cannot import - the \"Project\" folder is missing.";
        return result;
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(sourceAbsolutePath, existsEc) || existsEc) {
        result.success = false;
        result.message = "The source file does not exist: \"" + PathToUtf8(sourceAbsolutePath) + "\".";
        return result;
    }
    std::error_code isDirEc;
    if (std::filesystem::is_directory(sourceAbsolutePath, isDirEc) && !isDirEc) {
        result.success = false;
        result.message = "The source path is a directory, not a file - only single-file imports are supported.";
        return result;
    }

    // Empty destinationRelativeFolder means the Project root.
    std::filesystem::path relFolder = PathFromUtf8(destinationRelativeFolder);
    // has_root_path() also catches drive-relative ("C:Temp") and root-relative ("\x") paths that is_absolute() misses.
    if (relFolder.is_absolute() || relFolder.has_root_path()) {
        result.success = false;
        result.message = "destination_folder must be a plain path relative to the Project root, not absolute "
                          "or drive/root-relative.";
        return result;
    }
    const std::filesystem::path normalizedRelFolder = relFolder.lexically_normal();
    if (!normalizedRelFolder.empty() && normalizedRelFolder.begin()->wstring() == L"..") {
        result.success = false;
        result.message = "destination_folder must not escape the Project root (no \"..\" segments).";
        return result;
    }

    const std::filesystem::path targetDir = (m_rootPath / normalizedRelFolder).lexically_normal();

    std::error_code dirEc;
    std::filesystem::create_directories(targetDir, dirEc); // Best effort; ImportAssetFile() below is the real check.

    const std::filesystem::path destination
        = MakeUniqueDestinationPath(targetDir, ImportedFileName(sourceAbsolutePath));

    AssetDatabase database; // Scratch: ImportAssetFile() needs somewhere to register.
    result = ImportAssetFile(database, sourceAbsolutePath, destination, MeshImportOptions{});
    LogClusterMessage(result);
    if (result.success) {
        m_scan.MarkDirty();
    }
    return result;
}

} // namespace gte
