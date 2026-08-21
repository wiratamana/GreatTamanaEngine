#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace gte {

// Plain, ImGui-free description of whatever the Editor's "Inspector" panel
// needs to show for a Project-panel selection that ISN'T (or couldn't be
// shown as) a live image preview - name/extension/size/last-modified-time,
// all trivially available from std::filesystem alone, no GPU/image-decode
// involved. Built fresh on every selection (see BuildAssetMetadata() below),
// the same "rebuilt from disk each time, never a long-lived handle"
// philosophy as ProjectEntry (see ProjectPanelData.h) - a selected file
// being renamed/deleted out from under the Editor is simply reflected as
// `exists = false` on the next frame, never a dangling reference.
struct AssetMetadata {
    std::string name;      // File/folder name only, UTF-8 - e.g. "rock.png". Empty for the Project root itself.
    std::string extension; // Lowercase, WITH the leading dot - e.g. ".png". Empty if none/a folder.
    bool isDirectory = false;
    bool exists = false;          // False if the path no longer resolves to anything on disk.
    std::uintmax_t sizeBytes = 0; // Only meaningful for a file that exists (0 otherwise).
    bool hasLastWriteTime = false;
    std::string lastWriteTimeText; // Human-readable local time, e.g. "2026-08-21 11:37:54" - empty if unavailable.
};

// Gathers AssetMetadata for `absolutePath` (a real, absolute filesystem
// path - see EditorContext::selectedAssetAbsolutePath). Every filesystem
// call uses the std::error_code-taking overload, so a path that's vanished
// (or become unreadable) between being selected and this being called
// simply comes back with `exists = false` rather than throwing. Never
// throws.
AssetMetadata BuildAssetMetadata(const std::filesystem::path& absolutePath);

// True if `extensionLowercaseWithDot` (as produced by AssetMetadata::
// extension above, or any other lowercase ".ext" string) names a format
// stb_image can decode (see src/Editor/AssetPreviewTexture.h) - the
// Inspector uses this to decide whether to even attempt a texture preview
// before falling back to plain metadata. Deliberately a fixed allow-list
// matching stb_image's own documented supported formats, rather than
// "attempt a decode and see" - this is the cheap, Tier-1-testable pure
// check; AssetPreviewTexture is what actually attempts (and can still fail
// on, e.g. a corrupt/truncated file, in which case IT falls back too).
bool IsSupportedImageExtension(const std::string& extensionLowercaseWithDot);

} // namespace gte
