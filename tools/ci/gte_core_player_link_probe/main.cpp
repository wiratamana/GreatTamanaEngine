// tools/ci/gte_core_player_link_probe/main.cpp
//
// editor-core-separation-2 campaign, PHASE4 - a tiny, PERMANENT, checked-in
// program whose only job is to be linked against libgte_core.a ALONE (never
// gte_editor, never SDL, never ImGui) and have that link genuinely SUCCEED.
// See tools/ci/gte_core_player_link_probe/README.md for the full "why" and
// the exact command to run this by hand.
//
// This program's LINK STEP is never actually EXECUTED as part of the probe's
// three forced-link checks below (see README.md) - only COMPILED and LINKED.
// It forces the linker to pull RenderSystem.cpp.obj, Core.cpp.obj, and
// Network/NetworkServer.cpp.obj out of libgte_core.a's own archive (each one
// carries at least one of this campaign's own fixed gte_core -> gte_editor
// call sites) by taking the ADDRESS of one real, public, .cpp-defined method
// from each - forcing the WHOLE containing .o file to be extracted from the
// archive and its every remaining internal reference to be resolved, without
// this probe needing to safely, fully CONSTRUCT any of these (Core in
// particular needs a real ISurfaceProvider/Vulkan instance this tiny probe
// has no business standing up for) or ever calling anything that could
// crash.
//
// editor-core-separation-3 campaign, PHASE5
// (PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md, Step 3.2) added a FOURTH,
// genuinely EXECUTED bonus check after main()'s three original forced-link
// lines - constructing a real, headless gte::Core and calling
// Core::LoadPlugins()/Core::BuildFrame() against this probe's own real
// plugins/ folder, proving the plugin ABI boundary worked from inside a real
// (if surfaceless) gte::Core.
//
// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// that bonus check is DELETED outright - PluginHost (and Core::LoadPlugins()
// itself) no longer exist anywhere in this repository (both deleted by
// PHASE3 of this same campaign), so there is nothing left for it to call.
// This probe is back down to its original three forced-link-only checks -
// LINKING it still proves gte_core.a stays standalone-linkable; this .exe no
// longer does anything meaningful if actually EXECUTED (every remaining
// check only ever takes an address / constructs a socket-less server).

#include "Core/Core.h"
#include "Game/RenderSystem.h"
#include "Network/NetworkServer.h"

#include <optional>
#include <unordered_set>

int main()
{
    // Forces Core.cpp.obj to be pulled from libgte_core.a - proves
    // Core::BuildFrame()'s own internal call (formerly the free function
    // gte::AddFrameDebuggerReplayPasses(), gte_editor-only) has no
    // remaining undefined reference. Never called.
    void (gte::Core::*forceLinkCore)() = &gte::Core::BuildFrame;
    (void)forceLinkCore;

    // Forces RenderSystem.cpp.obj to be pulled from libgte_core.a - proves
    // RenderSystem::Draw()'s own internal call (formerly the free function
    // gte::RecordFrameDebuggerDraws(), gte_editor-only) has no remaining
    // undefined reference. The explicit member-function-pointer TYPE below
    // disambiguates which of RenderSystem::Draw()'s two overloads this
    // refers to (the float-aspect overload) - re-confirmed against the real,
    // current RenderSystem.h (lines 208-210) before finalizing this probe;
    // update it here if that header's signature has since changed. Never
    // called.
    using DrawAspectOverload = void (gte::RenderSystem::*)(gte::Registry&, gte::Renderer&, float,
        gte::IFrameDebuggerCaptureRecorder*, std::optional<std::size_t>, const std::unordered_set<gte::Entity>&);
    DrawAspectOverload forceLinkRenderSystem = &gte::RenderSystem::Draw;
    (void)forceLinkRenderSystem;

    // Forces Network/NetworkServer.cpp.obj to be pulled from
    // libgte_core.a - proves NetworkServer's own GET /get_logs/
    // POST /clear_logs route registration (formerly direct calls to
    // gte::Logger::Query()/Clear()/etc., gte_editor-only) has no remaining
    // undefined reference. Safe to actually CONSTRUCT (unlike Core) -
    // NetworkServer's constructor only registers httplib route handlers, it
    // never calls Start() (no socket is ever opened) and needs no exotic
    // interface fixture.
    gte::Network::NetworkServer networkServer;
    (void)networkServer;

    return 0;
}
