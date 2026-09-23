#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

// editor-core-separation-1 campaign, PHASE6
// (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - the REAL,
// gte_editor-owned implementation of ISceneIOCapability
// (Core/EditorCapabilities.h), delegating to src/Editor/SceneIO.h's real free
// SaveScene()/LoadScene()/DefaultScenePath() functions. Constructed once,
// from Application's own constructor (a temporary home - see
// Application.cpp's own PHASE6 comment; Phase 16 of this campaign relocates
// this wiring into EditorHost instead) and registered via
// Application::SetSceneIOCapability() (PHASE5).
//
// This HEADER deliberately carries ZERO dependency on SceneIO.h/
// ProjectRootPath.h (only this class's own .cpp does, exactly mirroring
// FrameDebuggerReplayPasses.cpp/FrameDebuggerDrawRecording.cpp's own PHASE2
// precedent of "the real body needing the complete type lives in a
// gte_editor-only .cpp, never in a header a gte_core-destined file might
// include") - only the WIRING call site (constructing an instance of this
// class + calling SetSceneIOCapability()) needs to stay behind a temporary
// `#if GTE_ENABLE_EDITOR` in Application.cpp, since this class's own method
// bodies (EditorSceneIOCapability.cpp) are only compiled into the build at
// all when the Editor module is (see CMakeLists.txt's still-conditional
// Editor source list, un-conditionalized for real only at Phase 9).
class EditorSceneIOCapability : public ISceneIOCapability {
public:
    bool SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage) override;
    bool LoadScene(
        Game& game, Renderer& renderer, const std::filesystem::path& scenePath, std::string& outErrorMessage) override;
    std::filesystem::path DefaultScenePath() const override;
};

} // namespace gte
