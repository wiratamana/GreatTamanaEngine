#include "ProjectPanelData.h"

#include "../Assets/AssetImporter.h"
#include "../Encoding/Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace gte {

namespace {

bool LessNoCase(const std::string& a, const std::string& b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
}

// Directories first, then case-insensitive by name.
bool CompareEntries(const ProjectEntry& a, const ProjectEntry& b)
{
    if (a.isDirectory != b.isDirectory) {
        return a.isDirectory;
    }
    return LessNoCase(a.name, b.name);
}

// True for symlinks, junctions and any other reparse point. Never followed by the scan.
bool IsLink(const std::filesystem::path& path, std::filesystem::file_type type)
{
#ifdef _WIN32
    // libstdc++ reports junctions as plain directories, so ask the OS directly.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return true;
    }
#else
    static_cast<void>(path);
#endif
#ifdef _MSC_VER
    if (type == std::filesystem::file_type::junction) {
        return true;
    }
#endif
    return type == std::filesystem::file_type::symlink;
}

std::vector<ProjectEntry> ScanChildren(const std::filesystem::path& dirAbsPath, const std::string& dirRelativePath,
    int depth, const std::atomic<bool>* cancel)
{
    std::vector<ProjectEntry> result;
    if (depth >= kMaxProjectScanDepth || (cancel != nullptr && cancel->load(std::memory_order_relaxed))) {
        return result;
    }

    std::error_code ec;
    std::filesystem::directory_iterator it(dirAbsPath, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        return result; // Vanished or unreadable: report empty, keep the rest of the scan.
    }

    for (const std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        const std::filesystem::directory_entry& entry = *it;

        std::error_code entryEc;
        const std::filesystem::file_type type = entry.symlink_status(entryEc).type();
        if (entryEc || type == std::filesystem::file_type::not_found) {
            continue; // Vanished between listing and stat.
        }

        ProjectEntry projectEntry;
        projectEntry.name = PathToUtf8(entry.path().filename());
        projectEntry.relativePath
            = dirRelativePath.empty() ? projectEntry.name : dirRelativePath + "/" + projectEntry.name;
        // A link is a plain leaf: not a directory, no size, no children.
        if (IsLink(entry.path(), type)) {
            projectEntry.isDirectory = false;
        } else if (type == std::filesystem::file_type::directory) {
            projectEntry.isDirectory = true;
            projectEntry.children = ScanChildren(entry.path(), projectEntry.relativePath, depth + 1, cancel);
        } else {
            std::error_code sizeEc;
            const std::uintmax_t size = entry.file_size(sizeEc);
            projectEntry.sizeBytes = sizeEc ? 0 : size;
        }

        result.push_back(std::move(projectEntry));
    }

    std::sort(result.begin(), result.end(), CompareEntries);
    return result;
}

} // namespace

std::vector<ProjectEntry> ScanProjectDirectory(const std::filesystem::path& rootPath, const std::atomic<bool>* cancel)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(rootPath, ec) || ec) {
        return {};
    }
    return ScanChildren(rootPath, std::string(), 0, cancel);
}

std::vector<std::string> ListFileNames(const std::filesystem::path& directory)
{
    std::vector<std::string> names;
    std::error_code ec;
    std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec);
    for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc) {
            names.push_back(PathToUtf8(it->path().filename()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool EnsureProjectRootExists(const std::filesystem::path& rootPath)
{
    std::error_code ec;
    if (std::filesystem::is_directory(rootPath, ec) && !ec) {
        return true;
    }

    ec.clear();
    std::filesystem::create_directories(rootPath, ec); // Result re-checked below.

    ec.clear();
    return std::filesystem::is_directory(rootPath, ec) && !ec;
}

std::filesystem::path ImportedFileName(const std::filesystem::path& sourceFile)
{
    std::filesystem::path name = sourceFile.filename();
    std::string extension = PathToUtf8(sourceFile.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (IsImportableAsKtx2Texture(extension) || IsImportableAsMeshAsset(extension)
        || IsImportableAsMotionAsset(extension)) {
        name.replace_extension(".gta");
    }
    return name;
}

std::filesystem::path ResolveDropTargetDirectory(
    const std::filesystem::path& rootPath, const std::string& selectedRelativePath)
{
    if (selectedRelativePath.empty()) {
        return rootPath;
    }

    const std::filesystem::path candidate = rootPath / PathFromUtf8(selectedRelativePath);

    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec) && !ec) {
        return candidate;
    }

    // A file or a vanished folder: use its parent, which is still under rootPath.
    const std::filesystem::path parent = candidate.parent_path();
    return parent.empty() ? rootPath : parent;
}

std::filesystem::path MakeUniqueDestinationPath(
    const std::filesystem::path& destinationDir, const std::filesystem::path& desiredName)
{
    std::error_code ec;
    const std::filesystem::path firstAttempt = destinationDir / desiredName;
    if (!std::filesystem::exists(firstAttempt, ec)) {
        return firstAttempt;
    }

    const std::filesystem::path stem = desiredName.stem();
    const std::filesystem::path extension = desiredName.extension();

    constexpr int kMaxAttempts = 1000;
    for (int suffix = 1; suffix <= kMaxAttempts; ++suffix) {
        std::filesystem::path candidateName = stem;
        candidateName += PathFromUtf8(" (" + std::to_string(suffix) + ")");
        candidateName += extension;

        const std::filesystem::path candidate = destinationDir / candidateName;
        std::error_code existsEc;
        if (!std::filesystem::exists(candidate, existsEc)) {
            return candidate;
        }
    }

    // All suffixes taken: return the colliding path rather than loop forever.
    return firstAttempt;
}

const ProjectEntry* FindEntryByRelativePath(const std::vector<ProjectEntry>& tree, const std::string& relativePath)
{
    if (relativePath.empty()) {
        return nullptr; // The root has no entry of its own.
    }

    for (const ProjectEntry& entry : tree) {
        if (entry.relativePath == relativePath) {
            return &entry;
        }

        // Recurse only into a true path prefix followed by '/', never a same-named sibling.
        const bool isGenuinePrefix = relativePath.size() > entry.relativePath.size()
            && relativePath.compare(0, entry.relativePath.size(), entry.relativePath) == 0
            && relativePath[entry.relativePath.size()] == '/';
        if (entry.isDirectory && isGenuinePrefix) {
            const ProjectEntry* found = FindEntryByRelativePath(entry.children, relativePath);
            if (found != nullptr) {
                return found;
            }
        }
    }

    return nullptr;
}

std::string ParentRelativePath(const std::string& relativePath)
{
    const std::size_t lastSlash = relativePath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return std::string(); // Top-level entry: the parent is the root.
    }
    return relativePath.substr(0, lastSlash);
}

std::optional<std::string> ResolveDropTarget(const std::vector<FolderDropZone>& zones, float x, float y,
    const Rect& leftPaneRect, const Rect& rightPaneRect, const std::string& currentFolderRelativePath)
{
    for (const FolderDropZone& zone : zones) {
        if (zone.rect.Contains(x, y)) {
            return zone.relativePath;
        }
    }

    if (rightPaneRect.Contains(x, y)) {
        return currentFolderRelativePath;
    }

    if (leftPaneRect.Contains(x, y)) {
        return std::string(); // Project root.
    }

    return std::nullopt;
}

} // namespace gte
