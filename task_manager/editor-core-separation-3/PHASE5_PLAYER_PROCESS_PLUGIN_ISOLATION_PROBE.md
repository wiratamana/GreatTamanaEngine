# PHASE5 — Proving "Always All-In" Stays Editor-Clean in a Player-Shaped Process

**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**:
`PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md` — both demo plugins (render
feature + editor panel) must already exist and be proven working inside
`GreatTamanaEditor.exe` before starting this phase.

Maps to the source design doc's Milestone 3. **Read
`PHASE0_MASTER_STRATEGY.md`'s Step 2.3, point 2, again before starting** —
the sibling document Milestone 3's own text references does not exist in
this repository; this phase deliberately builds on real, already-existing
infrastructure instead (`tools/ci/gte_core_player_link_probe`,
`tests/Fakes/HeadlessSurfaceProvider.h`).

---

## Step 1: The Goal (Where are we going, this phase specifically?)

Prove, mechanically, not just by code-reading, that "always all-in" (source
design doc, Section 0, decision #3) still keeps a Player-shaped process
editor-clean (Section 7): a process that never links `gte_editor` at all,
pointed at the EXACT SAME `plugins/` folder used by PHASE2/3/4's three demo
`.dll`s, still gets the runtime-tier plugin's behavior, while the
editor-tier plugin sits there, loaded, fully inert — never crashing, never
producing any visible effect, simply never asked for anything.

This phase has TWO deliverables, deliberately split by their real GPU
dependency, so the primary claim is provable on ANY machine, while the
richer claim is a documented, honestly-labeled, environment-gated bonus,
exactly mirroring this repository's own two existing, accepted
environment-gated test skips (`PmxLoaderRealModelSmokeTest`,
`CoreHeadlessConstructionTest`):

1. **Primary (no GPU needed, always runnable)**: a new
   `tools/ci/gte_plugin_isolation_probe/`, extending PHASE2's own
   handshake-probe shape — a tiny, standalone, non-engine `main.cpp` that
   loads EVERY `.dll` in `<build-dir>/plugins/` (all three demo plugins,
   real fingerprint-checked, real `IPluginModule*` registry, exactly
   mirroring `PluginHost::LoadPlugins()`'s own real procedure — reusing
   `PluginHost` itself directly, since it is a plain `gte_core`-owned class
   with zero GPU/window dependency of its own), queries
   `IRenderFeatureModule_v1` on every module (confirms it resolves on
   `DemoRenderFeaturePlugin`, `nullptr` on the other two), and **explicitly,
   provably never calls `QueryCapability("IEditorPanelModule_v1")` anywhere
   in this probe's own source at all** — the isolation mechanism IS this
   absence of a call, made visible/checkable by a human reader of this
   probe's own small `main.cpp`, not a runtime assertion (there is nothing
   to assert at runtime about a call that never happens — the proof is
   structural: this file never mentions `IEditorPanelModule_v1` anywhere).
2. **Bonus (GPU/headless-surface-gated, honestly may self-skip)**: extend
   `tools/ci/gte_core_player_link_probe`'s own real `main.cpp` to actually
   construct a real `gte::Core` (via `tests/Fakes/HeadlessSurfaceProvider.h`,
   guarded by the exact same `VK_EXT_headless_surface` support check
   `CoreHeadlessConstructionTest` already uses), call
   `Core::LoadPlugins(...)`, call `Core::BuildFrame()` once, and confirm no
   crash — this is the closest this probe can get to "the runtime-tier pass
   renders correctly in the Player probe too" without a real window/
   swapchain (this probe links `gte_core.a` alone, with no presentable
   surface at all — there is no swapchain to screenshot here, unlike
   `GreatTamanaEditor.exe`). Self-skips, loudly, with a clear message, on
   any machine whose Vulkan driver lacks the extension — exactly like
   `CoreHeadlessConstructionTest` already does today.

---

## Step 2: The Situation

- `tools/ci/gte_core_player_link_probe/` (read its own `CMakeLists.txt` and
  `main.cpp` in full — `editor-core-separation-2` campaign, PHASE4) is
  ALREADY a real, minimal, "Player-shaped" host: it configures the real
  repository root with `GTE_CORE_STANDALONE_PROBE_ONLY=ON`, links a real
  executable against `gte_core.a` **alone** (confirmed zero `gte_editor`/
  SDL/ImGui in that build tree), and forces `RenderSystem.cpp.obj`/
  `Core.cpp.obj`/`Network/NetworkServer.cpp.obj` into the link via
  member-function-pointer address-taking plus a real `NetworkServer`
  construction. This phase's own bonus deliverable EXTENDS this exact
  probe's existing `main.cpp`, in place — it does not create a rival
  probe.
- `tests/Fakes/HeadlessSurfaceProvider.h` (read in full —
  `editor-core-separation-1` campaign, PHASE18) is a real
  `ISurfaceProvider` implementation backed by a real
  `VK_EXT_headless_surface`-created `VkSurfaceKHR` — NOT a fake/stub
  pointer. `CoreHeadlessConstructionTest`
  (`tests/Core/CoreConstructionTests.cpp` or wherever it actually lives —
  confirm via `search_in_dir`) already shows the exact, real, working
  pattern for "construct a real `Core` headlessly, self-skip if
  `VK_EXT_headless_surface` isn't supported" — this phase's own bonus
  deliverable copies that exact self-skip check, not a new one.
- **`GTE_CORE_STANDALONE_PROBE_ONLY=ON` must NOT skip `gte_editor`,
  `demo_editor_panel`, or the `plugins/` `add_subdirectory()` block silently
  — the THREE demo plugin `.dll`s must still build in that configuration**,
  since this phase's own probes need them to exist on disk to load.
  PHASE2's own Step 3.5 places its `add_subdirectory(plugins/...)` block
  (and the `GTE_PLUGIN_RUNTIME_OUTPUT_DIR` variable it depends on)
  IMMEDIATELY after `gte_core`'s own `add_library()` block, deliberately
  OUTSIDE/BEFORE the `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard that
  wraps `gte_editor` itself — re-confirm this placement is genuinely still
  true in the real, current root `CMakeLists.txt` before relying on it here
  (real source may have drifted since PHASE2 ran, or PHASE2's own
  implementation may have deviated from its strategy doc for a discovered
  reason); if it is NOT true for any reason, fix it forward here (this is
  exactly the kind of cross-phase consistency issue the PHASE6 final
  regression pass, and this campaign's own double-check pass, exist to
  catch) and note the fix in this phase's own completion report.

---

## Step 3: The Plan

### 3.1 — Primary deliverable: `tools/ci/gte_plugin_isolation_probe/`

New folder, mirroring `tools/ci/gte_plugin_abi_handshake_probe/`'s (PHASE2)
own nested-cmake-invocation `CMakeLists.txt` shape exactly (configure the
real repo root with `GTE_CORE_STANDALONE_PROBE_ONLY=ON` `GTE_ENABLE_PLUGINS=ON`
`GTE_BUILD_TESTS=OFF`, then build this probe's own target).

`main.cpp` — deliberately small enough that its own absence of an
`IEditorPanelModule_v1` reference is trivially, visibly true to any reader:

```cpp
// tools/ci/gte_plugin_isolation_probe/main.cpp
//
// Milestone 3 (editor-core-separation-3 campaign, PHASE5) - proves "always
// all-in" (source design doc, Section 0/7) from a genuinely Player-shaped
// process: this file links gte_core.a ALONE (no gte_editor, confirmed via
// this probe's own CMakeLists.txt using GTE_CORE_STANDALONE_PROBE_ONLY=ON,
// mirroring tools/ci/gte_core_player_link_probe's own precedent), uses
// gte::PluginHost directly (a plain gte_core class, zero GPU/window
// dependency), and queries ONLY IRenderFeatureModule_v1 - this file
// intentionally, permanently NEVER mentions IEditorPanelModule_v1 anywhere
// in its own source, which IS the isolation proof itself: an editor-tier
// plugin sitting in plugins/ is loaded (its fingerprint/export handshake
// still succeeds - PluginHost does not distinguish "runtime" from "editor"
// plugins at load time, only at QUERY time), but nothing in this whole
// file ever asks it for anything.
#include "../../../src/Core/Plugins/PluginHost.h"
#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"

#include <cstdio>
#include <filesystem>

int main()
{
    gte::PluginHost host;
    // Resolved relative to THIS probe's own known build output location -
    // the same <build-dir>/plugins/ folder GreatTamanaEditor.exe's own
    // PluginHost already scans (confirm the real, current relative path
    // between this probe's own binary output folder and that shared
    // plugins/ folder at implementation time - it depends on the nested
    // build directory CMake produces, mirroring how
    // gte_core_player_link_probe's own main.cpp already resolves its own
    // real, current paths).
    host.LoadPlugins(std::filesystem::current_path() / "plugins");

    if (host.LoadedModuleCount() == 0) {
        std::fprintf(stderr, "FAIL: expected at least the 3 demo plugins to load, loaded 0.\n");
        return 1;
    }

    int renderFeatureCount = 0;
    for (gte::IPluginModule* module : host.AllLoadedModules()) {
        gte::GtePluginModuleInfo info;
        module->GetModuleInfo(info);
        const bool hasRenderFeature = module->QueryCapability(gte::kIRenderFeatureModule_v1_Name) != nullptr;
        std::printf("Loaded '%s' - IRenderFeatureModule_v1: %s\n", info.name, hasRenderFeature ? "yes" : "no");
        if (hasRenderFeature) {
            ++renderFeatureCount;
        }
    }

    if (renderFeatureCount != 1) {
        std::fprintf(stderr, "FAIL: expected exactly 1 plugin implementing IRenderFeatureModule_v1, found %d.\n", renderFeatureCount);
        return 1;
    }

    std::printf("PASS: %zu plugin(s) loaded, exactly 1 implements IRenderFeatureModule_v1, "
                "and this file never once asked any plugin for IEditorPanelModule_v1.\n",
        host.LoadedModuleCount());
    return 0;
}
```

This probe's own executable target must be added directly to the REAL root
`CMakeLists.txt`, inside the same `if(GTE_CORE_STANDALONE_PROBE_ONLY)` block
PHASE2's Step 3.6 already added `gte_plugin_abi_handshake_probe` to (confirm
its real, current location via `search_in_dir` first), mirroring that exact
shape — link against `gte_core` directly (the INNER, nested build produced
by `GTE_CORE_STANDALONE_PROBE_ONLY=ON` already has a real `gte_core` target
available) plus `gte_plugin_abi`, apply
`gte_apply_plugin_shared_crt_linkage()` to itself, and depend on ALL THREE
demo plugin targets (not just one — this probe's own `main.cpp` requires
`LoadedModuleCount() == 3`) so a single `--target` build step produces every
`.dll` it needs:
```cmake
add_executable(gte_plugin_isolation_probe tools/ci/gte_plugin_isolation_probe/main.cpp)
target_link_libraries(gte_plugin_isolation_probe PRIVATE gte_core gte_plugin_abi)
gte_apply_plugin_shared_crt_linkage(gte_plugin_isolation_probe)
add_dependencies(gte_plugin_isolation_probe demo_hello_world demo_render_feature demo_editor_panel)
```
(placed inside the same `if(GTE_CORE_STANDALONE_PROBE_ONLY)` block as
`gte_plugin_abi_handshake_probe`). Its own outer nested-cmake-invocation
wrapper (`tools/ci/gte_plugin_isolation_probe/CMakeLists.txt`) mirrors
`tools/ci/gte_plugin_abi_handshake_probe/`'s (PHASE2) own shape exactly —
configure the real repo root with `GTE_CORE_STANDALONE_PROBE_ONLY=ON`
`GTE_ENABLE_PLUGINS=ON` `GTE_BUILD_TESTS=OFF`, then build
`--target gte_plugin_isolation_probe` (confirm the transitive
`add_dependencies()` above genuinely builds all three plugin `.dll`s during
this phase's own verification; add explicit extra `--target` build steps
instead if it does not, for any real, discovered reason).

### 3.2 — Bonus deliverable: extend `tools/ci/gte_core_player_link_probe/main.cpp`

Read the real, current `main.cpp` in full first, and read
`tests/Core/CoreHeadlessConstructionTests.cpp` in full too (confirm its
exact real file name/location via `search_in_dir` first). **Important,
confirmed correction to how this self-skip actually works**: there is no
standalone, reusable `SupportsHeadlessSurface()` boolean-returning function
anywhere in this codebase to call — `CoreHeadlessConstructionTests.cpp`'s
own real mechanism is a `try`/`catch` wrapped directly around the actual
`Core` construction call, catching `std::exception` and treating that as
the "this machine's Vulkan driver lacks `VK_EXT_headless_surface`" signal
(`Renderer`'s constructor is what actually throws, deep inside
`VulkanDevice::PickPhysicalDevice()`/`FindQueueFamilies()`, when handed a
literally-null/invalid surface). Mirror THAT exact shape — a construction
attempt wrapped in `try`/`catch`, not a pre-check predicate — do not invent
or reference a `SupportsHeadlessSurface()` function that does not exist.
`tests/Fakes/HeadlessSurfaceProvider.h` has zero GoogleTest dependency
(only `ISurfaceProvider.h`/`volk.h`/`<stdexcept>`/`<string>`), so it is
safe to `#include` directly from this non-gtest standalone probe:

```cpp
// PHASE5 (editor-core-separation-3 campaign) bonus check - self-skips,
// loudly, on any machine whose Vulkan driver lacks
// VK_EXT_headless_surface, mirroring CoreHeadlessConstructionTests.cpp's
// own established try/catch-around-construction precedent EXACTLY (there
// is no separate boolean "is this supported" predicate to call - the
// skip signal IS the constructor throwing) - this is NOT a required part
// of this probe's own pass/fail exit code; a skip here still exits 0.
try {
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices; // mirror CoreHeadlessConstructionTests.cpp's own minimal IHostServices fake - confirm its real, current name/shape via read_file, do not invent a new one if a suitable fake already exists under tests/Fakes/
    gte::Core core(surfaceProvider, hostServices);
    core.LoadPlugins(std::filesystem::current_path() / "plugins");
    core.BuildFrame();
    std::printf("Bonus check PASS: headless Core constructed, LoadPlugins()+BuildFrame() ran with no crash.\n");
} catch (const std::exception& e) {
    std::printf("Bonus check SKIPPED: this machine's Vulkan driver lacks VK_EXT_headless_surface (matches the "
                "existing, documented CoreHeadlessConstructionTest skip). Real reason: %s\n", e.what());
}
```

Confirm `HeadlessSurfaceProvider`'s own real constructor signature and
whatever minimal `IHostServices` implementation already exists for test use
(`tests/Fakes/`) before writing this literally — reuse an existing fake if
one is already suitable, do not invent a duplicate. Once this probe
genuinely constructs a real `Core` and calls `LoadPlugins()` against a real
`plugins/` folder, it participates in the plugin ABI boundary for real, not
just as an inert link target — its own `gte_core_player_link_probe`
executable target (root `CMakeLists.txt`, inside the same
`if(GTE_CORE_STANDALONE_PROBE_ONLY)` block PHASE2's Step 3.6 already
touches) must therefore also get
`gte_apply_plugin_shared_crt_linkage(gte_core_player_link_probe)` applied,
mirroring PHASE2's own handshake probe — add this call if PHASE2 did not
already add it there for a different reason.

---

## Step 4: Verification (this phase's own mandatory checkpoint)

1. Build `tools/ci/gte_plugin_isolation_probe` fresh (nested-cmake
   pattern, its own inner build directory). Confirm it prints `PASS` and
   exits `0`.
2. Rebuild `tools/ci/gte_core_player_link_probe` fresh. Confirm it still
   passes its OWN pre-existing checks (the `RenderSystem::Draw()`/
   `NetworkServer` construction proof from `editor-core-separation-2`) AND
   this phase's own new bonus check runs (or cleanly self-skips, with the
   documented message) — either outcome is an acceptable PASS for this
   phase's own purposes; only a genuine crash/hang is a failure.
3. Re-confirm, one more time, via `browse_dir` on both probes' own inner
   build output folders, that `demo_hello_world.dll`/
   `demo_render_feature.dll`/`demo_editor_panel.dll` genuinely exist in each
   probe's own `plugins/` folder before it runs — a probe reporting success
   because it found ZERO plugins (a folder-path mistake) would be a
   silent, false-positive pass; explicitly rule this out by confirming
   `LoadedModuleCount() == 3` exactly (not merely `> 0`) in this phase's own
   completion report.

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly.

## This phase's own specific ambiguity to flag via `ask_questions` if hit

- If `GTE_CORE_STANDALONE_PROBE_ONLY=ON` turns out to already exclude the
  `plugins/` `add_subdirectory()` block for a reason deeper than a simple
  guard-placement mistake (e.g. a real, structural reason `gte_plugin_abi`
  itself cannot configure in that mode), stop and ask before restructuring
  PHASE1/PHASE2's own CMake decisions retroactively.

## Non-Goals for this phase specifically

- No real Player Build Pipeline/`<ProjectName>.exe` generation — unchanged,
  permanently out of scope (mirrors `editor-core-separation-1`/`-2`'s own
  identical, repeated Non-Goal).
- No true multi-process IPC/sandboxing.
