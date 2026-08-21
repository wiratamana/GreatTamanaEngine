#include "ProjectPanelData.h"

#include <algorithm>
#include <cctype>
#include <exception>

namespace gte {

namespace {

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Directories first, then case-insensitive alphabetical within each group -
// matches Unity's own Project window default ordering.
bool CompareEntries(const ProjectEntry& a, const ProjectEntry& b)
{
    if (a.isDirectory != b.isDirectory) {
        return a.isDirectory && !b.isDirectory;
    }
    return ToLowerAscii(a.name) < ToLowerAscii(b.name);
}

std::vector<ProjectEntry> ScanChildren(const std::filesystem::path& dirAbsPath, const std::string& dirRelativePath)
{
    std::vector<ProjectEntry> result;

    std::error_code dirEc;
    std::filesystem::directory_iterator it(
        dirAbsPath, std::filesystem::directory_options::skip_permission_denied, dirEc);
    if (dirEc) {
        // Can't even open this directory right now (deleted, or a
        // permissions issue, between the parent scan finding it and us
        // recursing into it) - just report it as empty rather than failing
        // the whole scan.
        return result;
    }

    const std::filesystem::directory_iterator end;
    while (it != end) {
        std::error_code entryEc;
        const std::filesystem::directory_entry entry = *it;

        const bool isDir = entry.is_directory(entryEc);
        if (entryEc) {
            // The entry vanished (or became unreadable) between being
            // listed and us stat()-ing it - skip it, not fatal.
            it.increment(entryEc);
            continue;
        }

        ProjectEntry projectEntry;
        projectEntry.name = PathToUtf8(entry.path().filename());
        projectEntry.relativePath
            = dirRelativePath.empty() ? projectEntry.name : dirRelativePath + "/" + projectEntry.name;
        projectEntry.isDirectory = isDir;

        if (isDir) {
            projectEntry.children = ScanChildren(entry.path(), projectEntry.relativePath);
        } else {
            std::error_code sizeEc;
            const std::uintmax_t size = entry.file_size(sizeEc);
            projectEntry.sizeBytes = sizeEc ? 0 : size;
        }

        result.push_back(std::move(projectEntry));

        it.increment(entryEc);
        if (entryEc) {
            // The directory itself was removed out from under this
            // iteration - stop here and return whatever was already
            // collected rather than looping on a broken iterator.
            break;
        }
    }

    std::sort(result.begin(), result.end(), CompareEntries);
    return result;
}

} // namespace

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::vector<ProjectEntry> ScanProjectDirectory(const std::filesystem::path& rootPath)
{
    try {
        std::error_code existsEc;
        if (!std::filesystem::is_directory(rootPath, existsEc) || existsEc) {
            return {};
        }
        return ScanChildren(rootPath, std::string());
    } catch (const std::exception&) {
        // Belt-and-braces: every call above already uses the
        // std::error_code overload and shouldn't throw, but scanning a
        // live, externally-modified directory tree is exactly the kind of
        // place a rare edge case (or a future change to this function)
        // could slip one through anyway - never let it escape into ImGui/
        // the rest of the Editor.
        return {};
    }
}

bool EnsureProjectRootExists(const std::filesystem::path& rootPath)
{
    std::error_code ec;
    if (std::filesystem::is_directory(rootPath, ec) && !ec) {
        return true;
    }

    ec.clear();
    std::filesystem::create_directories(rootPath, ec); // Return value ignored - re-checked below either way.

    ec.clear();
    return std::filesystem::is_directory(rootPath, ec) && !ec;
}

std::filesystem::path ResolveDropTargetDirectory(
    const std::filesystem::path& rootPath, const std::string& selectedRelativePath)
{
    if (selectedRelativePath.empty()) {
        return rootPath;
    }

    const std::filesystem::path candidate = rootPath / Utf8ToPath(selectedRelativePath);

    std::error_code ec;
    if (std::filesystem::is_directory(candidate, ec) && !ec) {
        return candidate;
    }

    // Selected entry is a file, or no longer exists at all - fall back to
    // its parent directory (still guaranteed to be inside rootPath, since
    // candidate itself was built as rootPath / <relative path>), or
    // rootPath itself if that parent somehow came back empty.
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
        candidateName += Utf8ToPath(" (" + std::to_string(suffix) + ")");
        candidateName += extension;

        const std::filesystem::path candidate = destinationDir / candidateName;
        std::error_code existsEc;
        if (!std::filesystem::exists(candidate, existsEc)) {
            return candidate;
        }
    }

    // Every reasonable suffix is somehow taken - give up and hand back the
    // original (colliding) path; the caller will overwrite it, which is
    // still better than never completing the operation at all.
    return firstAttempt;
}

const ProjectEntry* FindEntryByRelativePath(const std::vector<ProjectEntry>& tree, const std::string& relativePath)
{
    if (relativePath.empty()) {
        return nullptr; // The root has no ProjectEntry of its own - see this function's doc comment.
    }

    for (const ProjectEntry& entry : tree) {
        if (entry.relativePath == relativePath) {
            return &entry;
        }

        // Only recurse into a subtree whose relativePath is a genuine
        // path-PREFIX of the target (followed by a '/'), never a same-named
        // sibling - e.g. looking for "Sub2/x" must never recurse into "Sub".
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
        return std::string(); // Top-level entry (or already empty) - its parent is the Project root itself.
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
