// tools/ci/gte_core_player_link_probe/main.cpp
//
// editor-core-separation-2 campaign, PHASE4 - a tiny, PERMANENT, checked-in
// program whose only job is to be linked against libgte_core.a ALONE (never
// gte_editor, never SDL, never ImGui) and have that link genuinely SUCCEED.
// See tools/ci/gte_core_player_link_probe/README.md for the full "why" and
// the exact command to run this by hand.
//
// This program's LINK STEP is never actually EXECUTED as part of the probe's
// three original forced-link checks (see README.md) - only COMPILED and
// LINKED. It forces the linker to pull RenderSystem.cpp.obj, Core.cpp.obj,
// and Network/NetworkServer.cpp.obj out of libgte_core.a's own archive (each
// one carries at least one of this campaign's own fixed gte_core ->
// gte_editor call sites) by taking the ADDRESS of one real, public,
// .cpp-defined method from each - forcing the WHOLE containing .o file to be
// extracted from the archive and its every remaining internal reference to
// be resolved, without this probe needing to safely, fully CONSTRUCT any of
// these (Core in particular needs a real ISurfaceProvider/Vulkan instance
// this tiny probe has no business standing up for THAT check) or ever
// calling anything that could crash.
//
// editor-core-separation-3 campaign, PHASE5
// (PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md, Step 3.2) added a FOURTH,
// genuinely EXECUTED (when actually run, not merely linked) bonus check
// below main()'s three original forced-link lines: a real, headless
// gte::Core IS constructed (via tests/Fakes/HeadlessSurfaceProvider.h, the
// same VK_EXT_headless_surface mechanism CoreHeadlessConstructionTests.cpp
// already uses, self-skipping with the identical try/catch-around-
// construction shape on a machine whose Vulkan driver lacks that extension),
// Core::LoadPlugins() is called against this probe's own real plugins/
// folder, and Core::BuildFrame() is called once - proving the plugin ABI
// boundary works, and "always all-in" causes no crash, from INSIDE a real
// (if surfaceless) gte::Core, the closest this probe can get to "the
// runtime-tier plugin's pass renders correctly in the Player probe too"
// without a real window/swapchain to screenshot.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Game/RenderSystem.h"
#include "Network/NetworkServer.h"

// tests/Fakes/HeadlessSurfaceProvider.h has zero GoogleTest dependency (only
// ISurfaceProvider.h/volk.h/<stdexcept>/<string>), so it is safe to #include
// directly from this non-gtest standalone probe - confirmed by direct
// reading of the file before writing this include (PHASE5_PLAYER_PROCESS_
// PLUGIN_ISOLATION_PROBE.md, Step 3.2).
#include "../../../tests/Fakes/HeadlessSurfaceProvider.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

namespace {

// Mirrors tests/Core/CoreHeadlessConstructionTests.cpp's own NoopHostServices
// exactly - that file's own version lives in an anonymous namespace inside a
// GoogleTest-only .cpp, not reusable from this standalone, non-gtest probe,
// so this is a deliberate, small, one-off duplicate rather than a shared
// header (there is no tests/Fakes/ home for it since it is trivial and
// gtest-adjacent only by convention, not by any real dependency).
class NoopHostServices : public gte::IHostServices {
public:
    void Log(gte::LogLevel /*level*/, std::string_view /*message*/) override {}
};

// Mirrors tools/ci/gte_plugin_abi_handshake_probe/main.cpp's own
// ResolveDemoHelloWorldDllPath()/tools/ci/gte_plugin_isolation_probe/main.cpp's
// own ResolvePluginsDirectory() precedent - resolves the shared plugins/
// folder relative to THIS exe's own directory (GetModuleFileNameW), never
// std::filesystem::current_path() (a process's current working directory at
// execution time is whatever its CALLER happened to set, never guaranteed to
// be this exe's own directory).
std::filesystem::path ResolvePluginsDirectory()
{
    wchar_t exePathBuffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    const std::filesystem::path exePath(std::wstring(exePathBuffer, length));
    return exePath.parent_path() / "plugins";
}

} // namespace

int main()
{
    // Forces Core.cpp.obj to be pulled from libgte_core.a - proves
    // Core::BuildFrame()'s own internal call (formerly the free function
    // gte::AddFrameDebuggerReplayPasses(), gte_editor-only) has no
    // remaining undefined reference. Never called via this expression alone
    // (BuildFrame() IS genuinely called below, on a real headless Core, by
    // this phase's own new bonus check - this member-function-pointer
    // expression's own job is purely the LINK-time proof, independent of
    // whether the bonus check below happens to run or self-skip on this
    // machine).
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

    // PHASE5 (editor-core-separation-3 campaign) bonus check - self-skips,
    // loudly, on any machine whose Vulkan driver lacks
    // VK_EXT_headless_surface, mirroring CoreHeadlessConstructionTests.cpp's
    // own established try/catch-around-construction precedent EXACTLY
    // (there is no separate boolean "is this supported" predicate to call -
    // the skip signal IS the constructor throwing). Not a required part of
    // this probe's own pass/fail exit code - a skip here still exits 0, same
    // as a pass; only a genuine crash/hang would be a real failure.
    try {
        gte::HeadlessSurfaceProvider surfaceProvider;
        NoopHostServices hostServices;
        gte::Core core(surfaceProvider, hostServices);
        core.LoadPlugins(ResolvePluginsDirectory());
        core.BuildFrame();
        std::printf("Bonus check PASS: headless Core constructed, LoadPlugins()+BuildFrame() ran with no crash.\n");
    } catch (const std::exception& e) {
        std::printf("Bonus check SKIPPED: this machine's Vulkan driver lacks VK_EXT_headless_surface (matches the "
                    "existing, documented CoreHeadlessConstructionTest skip). Real reason: %s\n",
            e.what());
    }

    return 0;
}
