#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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
// file should actually land in, given a target relativePath (identified the
// same way as ProjectEntry::relativePath above - empty means "the Project
// root itself"). If the target names a directory that still exists, that
// directory is the result; if it names a file (or something that no longer
// exists - e.g. deleted the instant after being selected/targeted), its
// parent directory is used instead; an empty target falls back to
// `rootPath` itself. This is what makes "drop a file onto/inside a folder"
// land inside that folder rather than always the root, while still
// degrading gracefully (never throwing, always resolving to somewhere under
// `rootPath`) if the target turns out to be stale.
std::filesystem::path ResolveDropTargetDirectory(
    const std::filesystem::path& rootPath, const std::string& targetRelativePath);

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

// Finds the ProjectEntry (anywhere in the tree, at any depth) whose
// relativePath exactly matches `relativePath`, recursing only into
// subtrees whose own relativePath is a genuine path-prefix of it (never a
// same-named sibling, e.g. looking for "Sub2/x" never recurses into
// "Sub"). Returns nullptr if nothing matches - a completely normal,
// expected outcome (the entry was renamed/deleted/never existed), not an
// error; callers (ProjectPanel) are expected to fall back gracefully (e.g.
// to the Project root) rather than treat this as exceptional. `relativePath`
// itself being empty always returns nullptr, since the Project root has no
// ProjectEntry representation of its own - `tree` (the vector this was
// called with) already *is* the root's own children.
const ProjectEntry* FindEntryByRelativePath(const std::vector<ProjectEntry>& tree, const std::string& relativePath);

// The parent of a ProjectEntry::relativePath value - "Sub/nested.txt" ->
// "Sub", "top.txt" -> "" (the Project root), "" -> "" (the root's own
// "parent" is itself, as far as this helper is concerned). Plain string
// splitting on the last '/' rather than routing through
// std::filesystem::path::parent_path(), since relativePath's on-disk
// representation is implementation-defined once round-tripped through a
// real path object (Windows may normalize '/' to its own preferred
// separator) - this must stay the exact forward-slash-joined form
// ProjectEntry::relativePath already uses everywhere else, so plain string
// manipulation is actually the more correct choice here, not a shortcut.
std::string ParentRelativePath(const std::string& relativePath);

// A plain, ImGui-agnostic rectangle in absolute desktop/screen coordinates -
// deliberately not ImVec2/ImVec4, so this header (and ResolveDropTarget()
// below) has zero ImGui dependency and stays Tier-1-testable, matching this
// whole file's existing "pure logic, ImGui-free" split from
// Panels/ProjectPanel.cpp (see AGENTS.md, "Testability & Regression
// Safety").
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

// One folder's on-screen hit-box for THIS frame, as recorded by
// ProjectPanel while rendering both the left folder-tree pane and any
// subfolder rows inside the right content pane (see ResolveDropTarget()
// below) - `relativePath` empty means the Project root itself.
struct FolderDropZone {
    std::string relativePath;
    Rect rect;
};

// Resolves which folder a file/folder dropped at screen position (x, y)
// should actually land in, given everything ProjectPanel recorded while
// rendering the two-pane "Project" window THIS frame: every folder's own
// on-screen hit-box (`zones` - left-pane tree rows AND right-pane subfolder
// rows alike), the two panes' own overall rects, and which folder is
// currently "open" (shown in the right pane). Returns std::nullopt if
// (x, y) isn't inside either pane at all - the caller should then ignore
// the drop entirely (it landed on the toolbar, the splitter, title bar,
// ...).
//
// Priority, matching Windows Explorer/Unity's own drag-and-drop behavior:
//   1. Landed directly on a specific folder's row (in EITHER pane) - that
//      folder is the target, regardless of which pane it was in. Checked
//      first so dropping ONTO a subfolder row inside the right pane still
//      lands inside that subfolder, not the currently open folder.
//   2. Otherwise, landed somewhere in the right pane (empty space between/
//      below rows) - the currently open folder is the target.
//   3. Otherwise, landed somewhere in the left pane but not on any specific
//      folder row - the Project root is the target.
std::optional<std::string> ResolveDropTarget(const std::vector<FolderDropZone>& zones, float x, float y,
    const Rect& leftPaneRect, const Rect& rightPaneRect, const std::string& currentFolderRelativePath);

} // namespace gte
