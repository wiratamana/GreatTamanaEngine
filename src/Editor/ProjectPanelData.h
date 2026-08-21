#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gte {

// One file-or-folder entry inside the Editor's "Project" panel (see
// Panels/ProjectPanel.h) - built fresh from a real on-disk scan every time
// ScanProjectDirectory() below runs, never a long-lived handle/pointer into
// the filesystem. This is deliberate: the whole point of re-scanning into a
// brand new tree of plain data (rather than caching directory_entry/
// iterator state, or - worse - raw pointers into a previous scan's tree) is
// that if the user deletes something inside "Project" externally (Explorer,
// git, whatever) between scans, the NEXT scan simply omits it - there is
// nothing stale/dangling anywhere for the Editor to trip over. Same "plain
// data, rebuilt each time" philosophy AGENTS.md already applies to
// MemoryRow (see MemoryPanelData.h).
struct ProjectEntry {
    std::string name;         // File/folder name only (no path separators), UTF-8.
    std::string relativePath; // Slash-separated, relative to the Project root, UTF-8 - e.g. "Textures/rock.png".
    bool isDirectory = false;
    std::uintmax_t sizeBytes = 0; // Only meaningful for a file (0 for a directory).
    std::vector<ProjectEntry> children; // Only populated for a directory.
};

// Dear ImGui always expects UTF-8 text (see imgui.h's own docs), but
// std::filesystem::path's string()/a bare path(std::string) construction go
// through the OS's native narrow encoding instead - the current ANSI
// codepage on Windows, NOT UTF-8 - so every ProjectEntry::name/relativePath
// and every path built from a UTF-8 source (SDL_GetBasePath(), an
// SDL_EVENT_DROP_FILE path, a selection stored as ProjectEntry::relativePath)
// goes through these two helpers instead, never path::string()/a bare
// path(std::string) construction, to keep non-ASCII filenames (e.g.
// Japanese/accented names) displaying and round-tripping correctly. Both
// route through std::u8string (path's own C++20 UTF-8-aware constructor/
// accessor), not the deprecated std::filesystem::u8path() free function.
std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path Utf8ToPath(const std::string& utf8);

// Recursively scans `rootPath` (the Project folder itself - see
// ProjectPanel's own root resolution) and returns its immediate children as
// a tree of ProjectEntry, directories first then files, both
// case-insensitively alphabetical within each group - Unity's own Project
// window default ordering.
//
// Every filesystem call this makes is the std::error_code-taking overload,
// specifically so a file/folder that vanishes mid-scan (a real race with
// whatever external process/user is touching "Project" concurrently) is
// silently skipped rather than throwing - the caller never needs to guard
// this in a try/catch for that to hold (it wraps its own top-level body in
// one anyway, purely as a last-resort safety net). `rootPath` itself not
// existing (or not being a directory) simply produces an empty result
// rather than an error - see EnsureProjectRootExists() below for actually
// creating it.
std::vector<ProjectEntry> ScanProjectDirectory(const std::filesystem::path& rootPath);

// Makes sure `rootPath` exists as a real directory, creating it (and any
// missing parent directories) if it doesn't - returns true if `rootPath` is
// confirmed to exist as a directory by the time this returns (whether it
// already did, or was just created), false if it still doesn't (e.g. a
// permissions problem, or `rootPath`'s parent turning out to actually be a
// file) - the caller (ProjectPanel) is expected to show a clear in-panel
// message rather than crash/throw when this comes back false. Never throws.
bool EnsureProjectRootExists(const std::filesystem::path& rootPath);

// Resolves which absolute directory a "New Folder"/an externally-dropped
// file should actually land in, given the Project panel's currently
// selected entry (identified by its ProjectEntry::relativePath, as above -
// empty means "nothing selected"). If the selection names a directory that
// still exists, that directory is the target; if it names a file (or
// something that no longer exists - e.g. deleted the instant after being
// selected), its parent directory is used instead; an empty selection falls
// back to `rootPath` itself. This is what makes "drop a file while a folder
// is selected in Project" land inside that folder rather than always the
// root, while still degrading gracefully (never throwing, always resolving
// to somewhere under `rootPath`) if the selection is stale.
std::filesystem::path ResolveDropTargetDirectory(
    const std::filesystem::path& rootPath, const std::string& selectedRelativePath);

// Returns a destination path inside `destinationDir` for a file/folder
// named `desiredName` that is guaranteed not to already exist on disk:
// `desiredName` itself if free, otherwise "name (1).ext", "name (2).ext",
// ... (Windows Explorer's own convention) up to a generous bound, after
// which it gives up and returns the plain (colliding) path as a last resort
// rather than looping forever - the caller (a copy/create-folder operation)
// then just overwrites, which is still strictly better than crashing or
// never completing. Used by both "copy an externally-dropped file into
// Project" (never silently clobbers something already there) and
// "New Folder" (so a second "New Folder" click doesn't collide with the
// first).
std::filesystem::path MakeUniqueDestinationPath(
    const std::filesystem::path& destinationDir, const std::filesystem::path& desiredName);

} // namespace gte
