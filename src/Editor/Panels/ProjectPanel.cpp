#include "ProjectPanel.h"

#include "../EditorContext.h"
#include "../../Assets/AssetImporter.h"
#include "../MemoryPanelData.h" // FormatBytes() - reused for the file-size tooltip below.

#include <imgui.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <system_error>

namespace gte {

namespace {

constexpr std::chrono::milliseconds kRescanInterval{ 500 };
constexpr std::chrono::milliseconds kStatusMessageLifetime{ 4000 };

constexpr float kSplitterWidth = 6.0f;
constexpr float kMinPaneWidth = 100.0f;
constexpr float kFooterReserve = 26.0f; // Space always left below both panes for the status line.

bool HasSubfolders(const ProjectEntry& entry)
{
    return std::any_of(entry.children.begin(), entry.children.end(), [](const ProjectEntry& child) {
        return child.isDirectory;
    });
}

} // namespace

ProjectPanel::ProjectPanel()
{
    // SDL_GetBasePath() returns the directory containing the running
    // executable (with a trailing separator), UTF-8 encoded, owned by SDL
    // (never freed by us) - "Project" lives right next to the .exe, per
    // this feature's spec. Falls back to the current working directory if
    // SDL can't determine it for some reason, rather than leaving
    // m_rootPath empty (which would otherwise resolve to the process's
    // current directory anyway via the "Project" relative path below, but
    // this makes the fallback explicit rather than incidental).
    const char* basePath = SDL_GetBasePath();
    m_rootPath = Utf8ToPath(basePath != nullptr ? basePath : "./") / "Project";
}

void ProjectPanel::EnsureRootAndMaybeRescan()
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_needsRescan && (now - m_lastScanTime) < kRescanInterval) {
        return;
    }

    // Re-checked (and, if missing, recreated) on every throttled rescan -
    // not just once at startup - so a "Project" folder deleted externally
    // while the Editor is running gets quietly recreated (or at least
    // re-detected as missing) within one rescan interval, rather than
    // requiring an engine restart.
    m_rootExists = EnsureProjectRootExists(m_rootPath);
    m_tree = m_rootExists ? ScanProjectDirectory(m_rootPath) : std::vector<ProjectEntry>{};

    // Rebuilds the *.gta guid<->path index from whatever's actually on
    // disk right now, on the exact same throttle as m_tree above - see
    // GetAssetDatabase()'s own doc comment (ProjectPanel.h).
    if (m_rootExists) {
        m_assetDatabase.RefreshFromDirectory(m_rootPath);
    } else {
        m_assetDatabase.Clear();
    }

    ReconcileCurrentFolderAfterRescan();

    m_lastScanTime = now;
    m_needsRescan = false;
}

void ProjectPanel::ReconcileCurrentFolderAfterRescan()
{
    if (!m_rootExists) {
        m_currentFolderRelativePath.clear();
        return;
    }

    // Walk up to the nearest ancestor that still exists as a directory in
    // the freshly-scanned tree - covers both "the open folder itself was
    // deleted" and "it was replaced by a same-named file" without ever
    // leaving the right pane stuck showing a folder that no longer exists.
    // Terminates: the Project root itself (empty relativePath) is always
    // considered valid, so this can never loop forever.
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
        PathToUtf8(relativePath.empty() ? m_rootPath : (m_rootPath / Utf8ToPath(relativePath))),
        relativePath,
        isDirectory);
}

void ProjectPanel::RecordFolderDropZone(const std::string& relativePath)
{
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    m_folderDropZones.push_back(FolderDropZone{ relativePath, Rect{ min.x, min.y, max.x - min.x, max.y - min.y } });
}

void ProjectPanel::RenderLeftPaneFolder(EditorContext& ctx, const ProjectEntry& entry)
{
    if (!entry.isDirectory) {
        return; // Left pane is folders-only, like Unity/Explorer's own tree.
    }

    ImGui::PushID(entry.relativePath.c_str());

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
        for (const ProjectEntry& child : entry.children) {
            RenderLeftPaneFolder(ctx, child);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void ProjectPanel::RenderLeftPane(EditorContext& ctx)
{
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
        for (const ProjectEntry& entry : m_tree) {
            RenderLeftPaneFolder(ctx, entry);
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
            break; // Nothing left to walk (m_currentFolderRelativePath was empty - already just "Project" above).
        }
        accumulated = accumulated.empty() ? segment : accumulated + "/" + segment;

        ImGui::SameLine();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        ImGui::PushID(segmentIndex++);
        if (ImGui::SmallButton(segment.c_str())) {
            m_currentFolderRelativePath = accumulated;
        }
        ImGui::PopID();

        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
}

void ProjectPanel::RenderRightPaneEntry(EditorContext& ctx, const ProjectEntry& entry)
{
    ImGui::PushID(entry.relativePath.c_str());

    const bool isSelected = ctx.selection.IsAssetSelected(entry.relativePath);
    const std::string label = entry.isDirectory ? ("[Folder] " + entry.name) : entry.name;
    ImGui::Selectable(label.c_str(), isSelected);

    if (entry.isDirectory) {
        RecordFolderDropZone(entry.relativePath);
    }

    if (entry.isDirectory && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        // Double-click a subfolder here to navigate INTO it - the same
        // "open" action as clicking it in the left pane, so both panes
        // always agree on what's open.
        m_currentFolderRelativePath = entry.relativePath;
        SetAssetSelection(ctx, entry.relativePath, true);
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        SetAssetSelection(ctx, entry.relativePath, entry.isDirectory);
    }

    if (!entry.isDirectory && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", FormatBytes(entry.sizeBytes).c_str());
    }

    ImGui::PopID();
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

    for (const ProjectEntry& entry : *children) {
        RenderRightPaneEntry(ctx, entry);
    }
}

void ProjectPanel::CreateNewFolder()
{
    const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, m_currentFolderRelativePath);
    const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, Utf8ToPath("New Folder"));

    std::error_code ec;
    std::filesystem::create_directory(destination, ec);
    if (ec) {
        SetStatus("Failed to create folder: " + ec.message(), true);
    } else {
        SetStatus("Created \"" + PathToUtf8(destination.filename()) + "\".", false);
        m_needsRescan = true;
    }
}

void ProjectPanel::DeleteSelected(EditorContext& ctx)
{
    // Guarded by Selection::HasAssetSelection() itself (not just the
    // disabled menu item in RenderContextMenu()) - a genuine no-op rather
    // than acting on a stale/no-longer-highlighted path if this were ever
    // reached in a state where Kind() isn't Asset.
    if (!ctx.selection.HasAssetSelection()) {
        return;
    }

    const std::string selectedDisplay = ctx.selection.SelectedAssetRelativePath();

    const std::filesystem::path target = m_rootPath / Utf8ToPath(selectedDisplay);

    std::error_code ec;
    const std::uintmax_t removedCount = std::filesystem::remove_all(target, ec);

    if (ec) {
        SetStatus("Failed to delete \"" + selectedDisplay + "\": " + ec.message(), true);
    } else if (removedCount == 0) {
        SetStatus("\"" + selectedDisplay + "\" no longer exists.", true);
    } else {
        SetStatus("Deleted \"" + selectedDisplay + "\".", false);
    }

    // If the deleted item was also whatever Project (or Inspector) currently
    // has selected, clear that too via Selection's own gate-keeper method -
    // otherwise Inspector would keep showing metadata for something that
    // just stopped existing until the user picks something else, and
    // Project's own row highlight/hasSelection would keep pointing at a
    // relativePath that no longer resolves to anything.
    ctx.selection.ClearAssetIfPath(selectedDisplay);

    // The deleted item might have been the currently open folder itself
    // (or an ancestor of it) - ReconcileCurrentFolderAfterRescan() (run as
    // part of the rescan below) walks m_currentFolderRelativePath back up
    // to whatever still exists, so it's left alone here.
    m_needsRescan = true;
}

void ProjectPanel::RenderContextMenu(EditorContext& ctx, const char* popupId)
{
    if (ImGui::BeginPopupContextWindow(popupId)) {
        if (ImGui::MenuItem("Refresh")) {
            m_needsRescan = true;
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
    EnsureRootAndMaybeRescan();

    m_panelVisible = ImGui::Begin("Project");
    if (m_panelVisible) {
        if (ImGui::Button("Refresh")) {
            m_needsRescan = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", PathToUtf8(m_rootPath).c_str());

        ImGui::Separator();

        if (!m_rootExists) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "The \"Project\" folder is missing and could not be (re)created:");
            ImGui::TextWrapped("%s", PathToUtf8(m_rootPath).c_str());
            ImGui::TextDisabled("(check that this location is writable, then click Refresh)");
        } else {
            m_folderDropZones.clear();

            // Clamp the splitter to stay sane as the window itself is
            // resized (e.g. never wider than the window, never so narrow
            // either pane becomes unusable).
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
    if (!m_panelVisible || sourcePathUtf8.empty()) {
        return; // "Project" isn't even on screen this frame - not this panel's business.
    }

    if (!m_rootExists) {
        // The two panes/drop zones below aren't meaningfully rendered at
        // all this frame (see Build()) when the root is missing - nothing
        // sensible to hit-test against, so this is the one case handled
        // before even trying.
        SetStatus("Cannot import - the \"Project\" folder is missing.", true);
        return;
    }

    const std::optional<std::string> target = ResolveDropTarget(
        m_folderDropZones, screenX, screenY, m_leftPaneRect, m_rightPaneRect, m_currentFolderRelativePath);
    if (!target.has_value()) {
        return; // Landed outside both panes entirely - not this panel's business.
    }

    try {
        const std::filesystem::path source = Utf8ToPath(sourcePathUtf8);

        std::error_code existsEc;
        if (!std::filesystem::exists(source, existsEc) || existsEc) {
            SetStatus("The dropped item no longer exists.", true);
            return;
        }

        const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, *target);

        std::error_code isDirEc;
        const bool sourceIsDirectory = std::filesystem::is_directory(source, isDirEc) && !isDirEc;

        // For a file that's about to be gated into a *.gta wrapper (see
        // AssetImporter::ImportAssetFile() below), the uniqueness check
        // must collide against an EXISTING "name.gta" (e.g. a previously-
        // imported texture), not the source's own "name.png" - otherwise
        // re-dropping "rock.png" after "rock.gta" already exists would
        // resolve to "rock.gta" directly (a silent overwrite) instead of
        // "rock (1).gta".
        std::filesystem::path desiredName = source.filename();
        if (!sourceIsDirectory) {
            std::string extension = PathToUtf8(source.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (IsImportableAsKtx2Texture(extension)) {
                desiredName.replace_extension(".gta");
            }
        }
        const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, desiredName);

        if (sourceIsDirectory) {
            std::error_code copyEc;
            std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, copyEc);
            if (copyEc) {
                SetStatus("Failed to import \"" + PathToUtf8(source.filename()) + "\": " + copyEc.message(), true);
            } else {
                SetStatus("Imported \"" + PathToUtf8(destination.filename()) + "\" into Project.", false);
                m_needsRescan = true;
            }
        } else {
            // Files are gated through AssetImporter - see
            // HandleExternalFileDrop()'s own doc comment (ProjectPanel.h).
            const AssetImportResult result = ImportAssetFile(m_assetDatabase, source, destination);
            SetStatus(result.message, !result.success);
            if (result.success) {
                m_needsRescan = true;
            }
        }
    } catch (const std::exception& e) {
        // Same belt-and-braces reasoning as ScanProjectDirectory() - every
        // call above already uses the std::error_code overload, but a
        // drag-and-drop import touching a source this engine doesn't
        // control (an arbitrary OS file/folder, possibly on a removable/
        // network drive that could disappear mid-copy) must never be able
        // to crash the Editor.
        SetStatus(std::string("Failed to import dropped item: ") + e.what(), true);
    }
}

} // namespace gte
