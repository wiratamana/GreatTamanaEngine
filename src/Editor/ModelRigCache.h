#pragma once

#include "../Assets/RigFile.h"

#include <filesystem>
#include <optional>
#include <string>

namespace gte {

// Loads + mtime-caches ONE model's complete RigFileData (per-vertex skin
// weights, bone hierarchy, morphs, rigid-body/joint physics setup,
// materials - see Assets/RigFile.h) straight from its source Mesh *.gta
// file's own METADATA section - the exact "read straight from source,
// reload only when mtime changes" logic BoneViewerWindow::EnsureDataLoaded()
// already implemented privately for its own internal use, extracted here so
// it can be shared, verbatim, by any OTHER Editor consumer that needs the
// same rig data WITHOUT also wanting a GPU mesh/pipeline (see
// task_manager/verlet-integration-2/PHASE0_MASTER_STRATEGY.md, Culprit A/E)
// - the same "never maintain two independent, silently-divergible copies of
// the same [...] pattern" precedent AGENTS.md already documents for
// bone-ancestor-chain walking (see "Skeletal Animation Pose Resolution").
//
// Deliberately does NOT touch the *.gta's PAYLOAD (the mesh vertex/index
// data) at all - that stays BoneViewerWindow's own private concern (its own
// GPU vertex/index buffers), since InspectorPanel (the other consumer of
// this class, PHASE4_INSPECTOR_MODEL_PART_SECTION.md) never needs a live
// mesh, only the metadata. A caller that ALSO needs the mesh payload (only
// BoneViewerWindow does) still calls ReadGtaFile() itself separately for
// that - this is a small, deliberate, documented duplication of ONE
// ReadGtaFile() disk read on an (infrequent, mtime-gated) reload event, not
// a duplication of the actual decode-and-cache-invalidation LOGIC, which is
// the part that actually matters (see Culprit E).
//
// Not tied to any one entity/window - a single instance can safely be asked
// about many different model paths over its lifetime (each tracked
// independently would be overkill for this campaign's actual need: exactly
// ONE shared instance, owned by ImGuiEditorLayer, is asked about whichever
// ONE path is relevant at a time - see BoneViewerWindow.h/InspectorPanel.h's
// own updated signatures). Deliberately a single-slot cache (like
// BoneViewerWindow's own m_cachedPath/m_cachedWriteTime before this
// extraction) rather than a path-keyed map - if a future need arises to
// track more than one model's rig data live at once, extend this class
// then, don't build that speculatively now.
class ModelRigCache {
public:
    // Returns a pointer to the cached RigFileData for `absoluteGtaPath`,
    // reloading from disk only if `absoluteGtaPath` differs from whichever
    // path was cached last, or the file's mtime has changed since then
    // (mirrors BoneViewerWindow::EnsureDataLoaded()'s own short-circuit
    // exactly). Returns nullptr if `absoluteGtaPath` doesn't exist, doesn't
    // resolve to a valid *.gta, or that *.gta's header does not declare
    // AssetType::Mesh - nullptr specifically means "not a loadable Mesh
    // asset at all", NOT "no bones" (a boneless mesh, or one imported
    // before rig extraction existed, still decodes to a valid, merely EMPTY
    // RigFileData - see RigFile.h's own doc comment - and this method
    // returns a valid pointer to that empty struct in that case, exactly
    // like DecodeRigDataFromBytes() itself never fails on a well-formed,
    // even entirely empty, RigFileData).
    //
    // The returned pointer is only valid until the NEXT call to
    // GetOrLoad() on this same instance (a subsequent call for a different
    // path, or the same path with a newer mtime, replaces the cached
    // value) - callers must not hold onto it across frames; re-call
    // GetOrLoad() every frame it's needed, exactly like
    // BoneViewerWindow::EnsureDataLoaded()'s own existing calling
    // convention already does for m_cachedIsValid-gated members.
    const RigFileData* GetOrLoad(const std::string& absoluteGtaPath);

private:
    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    std::optional<RigFileData> m_cachedRig; // std::nullopt means "last load attempt failed / not a Mesh *.gta".
};

} // namespace gte
