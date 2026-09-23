#pragma once

#include "../Renderer/Memory/GpuMemoryTracker.h"

#include <string>

namespace gte {

// editor-core-separation-1 campaign, PHASE4
// (task_manager/editor-core-separation-1/PHASE4_GPU_MEMORY_TRACKER_BUCKET_A_EXTRACTION.md) -
// the Editor-owned home for GPU resource human-readable debug names,
// displayed by the Editor's "Memory" panel (src/Editor/Panels/MemoryPanel.cpp)
// and the Frame Debugger (src/Editor/FrameDebuggerDrawRecording.cpp).
// GpuMemoryTracker (src/Renderer/Memory/GpuMemoryTracker.h, a gte_core type)
// stores ZERO bytes of name/string data itself - this class installs itself
// as that tracker's ONE GpuMemoryTracker::DebugNameObserver and keeps its
// OWN name table, keyed by GpuResourceHandle, entirely on this side of the
// gte_core/gte_editor boundary.
//
// All-static/global, mirroring Logger/SdlMemoryTracker/ImGuiMemoryTracker's
// own established "this dependency use" precedent (see AGENTS.md, "CPU
// Dependency Memory Tracking") - there is only ever ONE GpuMemoryTracker
// instance alive per process (owned by Renderer, shared via shared_ptr -
// see GpuMemoryTracker.h's own class comment), so a single global overlay
// needs no per-instance plumbing of its own, and every call site that wants
// a name (MemoryPanel.cpp/FrameDebuggerDrawRecording.cpp) only ever has a
// bare GpuResourceHandle in hand anyway, never a GpuMemoryTracker&.
//
// MUST be installed (via Install()) BEFORE the first GPU resource this
// process ever names is created, or that resource's name is silently
// dropped (never observed, since nothing was listening yet). The ONE real
// call site, `CreateEditorLayer()` (src/Editor/ImGuiEditorLayer.cpp), calls
// Install() as the very first statement, BEFORE constructing
// `ImGuiEditorLayer` itself (whose own constructor's member-initializer
// list immediately creates its "GameView"/"SceneView" RenderTextures - too
// late to install from inside that constructor's BODY). This is safe:
// `Renderer`'s own constructor, and every other `Application` member
// constructed before the Editor layer (`AtmosphereLutRenderer`,
// `VolumeTexturePreviewRenderer`), create their own named GPU resources
// LAZILY, on first real use, well after the Editor layer already exists -
// confirmed by reading every one of their constructors during this phase,
// not assumed.
class EditorGpuMemoryNameOverlay {
public:
    // Installs this overlay as `tracker`'s ONE debug-name observer. Safe to
    // call more than once (idempotent) - mirrors SdlMemoryTracker::Install()'s
    // own "safe to call more than once" precedent.
    static void Install(GpuMemoryTracker& tracker);

    // Looks up whatever name was last attached to `handle` via
    // GpuMemoryTracker::SetDebugName() - returns an empty string for an
    // unnamed/invalid/unknown/untracked handle, or if Install() was never
    // called at all.
    static const std::string& GetDebugName(GpuResourceHandle handle);

    // TEST-ONLY: clears every currently-remembered name. Never called by
    // production code - exists purely so independent TEST() cases (which
    // may each construct their own short-lived GpuMemoryTracker and
    // innocently produce the exact same GpuResourceHandle{0,1} as an
    // earlier, unrelated test - this overlay's storage is intentionally a
    // single global/static table, see the class comment above) never see a
    // stale name bleed across tests.
    static void ResetForTesting();

private:
    static void OnDebugNameObserved(void* userData, GpuResourceHandle handle, const char* name);
};

} // namespace gte
