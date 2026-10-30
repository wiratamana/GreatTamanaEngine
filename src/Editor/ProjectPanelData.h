#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// One file or folder inside the Project panel. Plain data, rebuilt by every scan.
struct ProjectEntry {
    std::string name;         // File or folder name only, UTF-8.
    std::string relativePath; // Slash-separated, relative to the Project root, UTF-8.
    bool isDirectory = false;
    std::uintmax_t sizeBytes = 0;       // Files only.
    std::vector<ProjectEntry> children; // Directories only.
};

// Deepest folder level a scan descends into; deeper folders are listed empty.
inline constexpr int kMaxProjectScanDepth = 32;

// Scans `rootPath` into a tree: directories first, then files, case-insensitive by name.
// Links are listed as plain entries and never followed. `cancel` is checked once per
// directory (nullptr = never). Never throws. Safe to call from a job.
std::vector<ProjectEntry> ScanProjectDirectory(
    const std::filesystem::path& rootPath, const std::atomic<bool>* cancel = nullptr);

// Sorted names of the regular files directly inside `directory`. Never throws. Safe to call from a job.
std::vector<std::string> ListFileNames(const std::filesystem::path& directory);

// Makes sure `rootPath` is a directory, creating it if needed. Returns false if it still is not. Never throws.
bool EnsureProjectRootExists(const std::filesystem::path& rootPath);

// Directory a new item lands in for `targetRelativePath`: the folder itself, else its parent,
// else `rootPath` (empty path or stale target).
std::filesystem::path ResolveDropTargetDirectory(
    const std::filesystem::path& rootPath, const std::string& targetRelativePath);

// File name an import writes: ".gta" for every format the importer converts, else unchanged.
std::filesystem::path ImportedFileName(const std::filesystem::path& sourceFile);

// `desiredName` inside `destinationDir` if free, else "name (1).ext", "name (2).ext", ...
// After 1000 taken names it returns the colliding path.
std::filesystem::path MakeUniqueDestinationPath(
    const std::filesystem::path& destinationDir, const std::filesystem::path& desiredName);

// Finds the entry with exactly this relativePath at any depth; nullptr if absent or empty.
const ProjectEntry* FindEntryByRelativePath(const std::vector<ProjectEntry>& tree, const std::string& relativePath);

// Parent of a relativePath: "a/b.txt" -> "a", "top.txt" -> "", "" -> "".
std::string ParentRelativePath(const std::string& relativePath);

// ImGui-free rectangle in absolute screen coordinates.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool Contains(float pointX, float pointY) const
    {
        return pointX >= x && pointX <= x + width && pointY >= y && pointY <= y + height;
    }
};

// One folder row's hit-box for the last drawn frame; empty relativePath = the Project root.
struct FolderDropZone {
    std::string relativePath;
    Rect rect;
};

// Folder a drop at screen position (x, y) lands in, or nullopt outside both panes.
// Priority: a folder row, else the right pane (open folder), else the left pane (root).
std::optional<std::string> ResolveDropTarget(const std::vector<FolderDropZone>& zones, float x, float y,
    const Rect& leftPaneRect, const Rect& rightPaneRect, const std::string& currentFolderRelativePath);

} // namespace gte
