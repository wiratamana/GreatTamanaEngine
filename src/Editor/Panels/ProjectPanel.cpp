#include "ProjectPanel.h"

#include "../MemoryPanelData.h" // FormatBytes() - reused for the file-size tooltip below.

#include <imgui.h>

#include <SDL3/SDL.h>

#include <exception>
#include <system_error>

namespace gte {

namespace {

constexpr std::chrono::milliseconds kRescanInterval{ 500 };
constexpr std::chrono::milliseconds kStatusMessageLifetime{ 4000 };

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

    m_lastScanTime = now;
    m_needsRescan = false;
}

void ProjectPanel::SetStatus(const std::string& message, bool isError)
{
    m_statusMessage = message;
    m_statusIsError = isError;
    m_statusSetTime = std::chrono::steady_clock::now();
}

void ProjectPanel::RenderEntry(const ProjectEntry& entry)
{
    ImGui::PushID(entry.relativePath.c_str());

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (entry.relativePath == m_selectedRelativePath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (entry.isDirectory) {
        const bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
            m_selectedRelativePath = entry.relativePath;
        }
        if (open) {
            for (const ProjectEntry& child : entry.children) {
                RenderEntry(child);
            }
            ImGui::TreePop();
        }
    } else {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
        ImGui::TreeNodeEx(entry.name.c_str(), flags);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            m_selectedRelativePath = entry.relativePath;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", FormatBytes(entry.sizeBytes).c_str());
        }
    }

    ImGui::PopID();
}

void ProjectPanel::CreateNewFolder()
{
    const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, m_selectedRelativePath);
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

void ProjectPanel::DeleteSelected()
{
    if (m_selectedRelativePath.empty()) {
        return;
    }

    const std::string selectedDisplay = m_selectedRelativePath;
    const std::filesystem::path target = m_rootPath / Utf8ToPath(m_selectedRelativePath);

    std::error_code ec;
    const std::uintmax_t removedCount = std::filesystem::remove_all(target, ec);

    if (ec) {
        SetStatus("Failed to delete \"" + selectedDisplay + "\": " + ec.message(), true);
    } else if (removedCount == 0) {
        SetStatus("\"" + selectedDisplay + "\" no longer exists.", true);
    } else {
        SetStatus("Deleted \"" + selectedDisplay + "\".", false);
    }

    m_selectedRelativePath.clear();
    m_needsRescan = true;
}

void ProjectPanel::RenderContextMenu()
{
    if (ImGui::BeginPopupContextWindow("ProjectContextMenu")) {
        if (ImGui::MenuItem("Refresh")) {
            m_needsRescan = true;
        }
        if (ImGui::MenuItem("New Folder", nullptr, false, m_rootExists)) {
            CreateNewFolder();
        }
        ImGui::Separator();
        const bool hasSelection = !m_selectedRelativePath.empty();
        if (ImGui::MenuItem("Delete Selected", nullptr, false, hasSelection && m_rootExists)) {
            DeleteSelected();
        }
        ImGui::EndPopup();
    }
}

void ProjectPanel::Build()
{
    EnsureRootAndMaybeRescan();

    m_panelVisible = ImGui::Begin("Project");
    if (m_panelVisible) {
        // Recorded every visible frame so HandleExternalFileDrop() always
        // hit-tests against THIS frame's actual rect - correct even if the
        // user just moved/resized/undocked the panel, or dragged it onto
        // its own OS window via ImGui's multi-viewport feature (GetWindowPos()
        // returns absolute desktop coordinates either way).
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        m_panelScreenX = pos.x;
        m_panelScreenY = pos.y;
        m_panelScreenWidth = size.x;
        m_panelScreenHeight = size.y;

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
        } else if (m_tree.empty()) {
            ImGui::TextDisabled("(empty - drag files in from Explorer, or right-click for options)");
        } else {
            for (const ProjectEntry& entry : m_tree) {
                RenderEntry(entry);
            }
        }

        // Right-click ANYWHERE in the panel (empty space or an existing
        // row alike, same as HierarchyPanel's own context menu - see its
        // comment) for Refresh/New Folder/Delete Selected.
        RenderContextMenu();

        if (!m_statusMessage.empty()) {
            if (std::chrono::steady_clock::now() - m_statusSetTime < kStatusMessageLifetime) {
                ImGui::Separator();
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
    if (screenX < m_panelScreenX || screenX > m_panelScreenX + m_panelScreenWidth || screenY < m_panelScreenY
        || screenY > m_panelScreenY + m_panelScreenHeight) {
        return; // Dropped somewhere else entirely.
    }

    if (!m_rootExists) {
        SetStatus("Cannot import - the \"Project\" folder is missing.", true);
        return;
    }

    try {
        const std::filesystem::path source = Utf8ToPath(sourcePathUtf8);

        std::error_code existsEc;
        if (!std::filesystem::exists(source, existsEc) || existsEc) {
            SetStatus("The dropped item no longer exists.", true);
            return;
        }

        const std::filesystem::path targetDir = ResolveDropTargetDirectory(m_rootPath, m_selectedRelativePath);
        const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, source.filename());

        std::error_code isDirEc;
        const bool sourceIsDirectory = std::filesystem::is_directory(source, isDirEc) && !isDirEc;

        std::error_code copyEc;
        if (sourceIsDirectory) {
            std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, copyEc);
        } else {
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, copyEc);
        }

        if (copyEc) {
            SetStatus("Failed to import \"" + PathToUtf8(source.filename()) + "\": " + copyEc.message(), true);
        } else {
            SetStatus("Imported \"" + PathToUtf8(destination.filename()) + "\" into Project.", false);
            m_needsRescan = true;
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
