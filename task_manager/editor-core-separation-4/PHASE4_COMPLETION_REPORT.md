# PHASE4 — Plugin Runtime DLL Landmine — Defense in Depth — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `gte_core` + full `build` rebuild,
a `build/plugins/` listing, a live `GreatTamanaEditor.exe` run via
`GET /get_logs`, and the OPTIONAL stronger manual-copy proof (both run
successfully).

## What changed

### (a) `cmake/MingwRuntime.cmake`

- The EXISTING `gte_apply_plugin_shared_crt_linkage(target_name)` function was
  left **completely unchanged** — still the function called by the host
  executable (`GreatTamanaEditor`) and the 3 standalone probe `.exe`s
  (`gte_plugin_abi_handshake_probe`, `gte_plugin_isolation_probe`,
  `gte_core_player_link_probe`), confirmed via a fresh `search_in_dir` for
  `gte_apply_plugin_shared_crt_linkage` across the repo before and after this
  phase's edits — the only 4 remaining call sites of the ORIGINAL function are
  exactly those 4 host/probe targets in root `CMakeLists.txt`.
- Added a NEW function, `gte_apply_plugin_dll_shared_crt_linkage(target_name)`,
  right after the original in the same file, with the exact doc comment from
  the phase file's Step 3.1. It applies the same `-shared-libgcc` link option
  (and the same `GTE_ENABLE_PLUGINS` / `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
  guard/no-op-with-warning shape as the original) but **deliberately does NOT
  call `mingw_copy_runtime_dll()`** — so a plugin `.dll` target using this new
  function never gets the 3 runtime DLLs staged into the shared `plugins/`
  scan folder `PluginHost::LoadPlugins()` scans at startup.

### (b) Every demo plugin's `CMakeLists.txt`

Updated all 3: `plugins/demo_hello_world/CMakeLists.txt`,
`plugins/demo_render_feature/CMakeLists.txt`,
`plugins/demo_editor_panel/CMakeLists.txt`. Each file's last line changed
from `gte_apply_plugin_shared_crt_linkage(demo_xxx)` to
`gte_apply_plugin_dll_shared_crt_linkage(demo_xxx)`, with a short pointer
comment (not the full reasoning, per the phase file's own instruction) added
above each call referencing this phase's file. `demo_render_feature_second`
(PHASE5's future plugin) does not exist yet — confirmed via `browse_dir` on
`plugins/` before starting — so there was nothing else to update.

### (c) `src/Core/Plugins/PluginHost.cpp`

- Added `#include <cstring>` (explicit, not relying on any implicit
  transitive include from `<windows.h>`), right after the existing
  `#include <windows.h>`.
- Added, inside the existing anonymous namespace (right after
  `DescribeFingerprintMismatch()`, before the closing `} // namespace`):
  - `constexpr const char* kKnownNonPluginFilenames[]` — the 3 runtime DLL
    names (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`).
  - `bool IsKnownNonPluginFilename(const std::filesystem::path& fileName)` —
    a case-insensitive (`_stricmp`) match against that list.
- In `LoadPlugins()`'s per-file loop, right after the existing
  `entry.path().extension() != ".dll"` check, added:
  ```cpp
  if (IsKnownNonPluginFilename(entry.path().filename())) {
      GTE_LOG_INFO("PluginHost", "Skipping known non-plugin runtime file: " + entry.path().string());
      continue;
  }
  ```
  before the pre-existing `TryLoadOnePlugin(entry.path());` call. PHASE7
  (case-insensitive `.dll` extension matching) has not landed yet, so no
  merge conflict/adaptation was needed — the extension check above this new
  block is still the original case-sensitive form.

No other files were touched. `PluginHost.h`, `GreatTamanaEditor`'s own
CMake call site, and every probe's own CMake call site are all unchanged, as
required. The plugin architecture's shape (the 10 Locked Design Decisions
from `editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`) is unaffected —
this phase only changes WHERE runtime DLLs get copied (CMake-level) and adds a
defensive skip inside an existing scan loop (code-level); no interface, no ABI
contract, and no call site outside these two mechanisms changed.

## Verification evidence

1. `cmake --build build --target gte_core` — succeeded (auto-reconfigured
   first, since `CMakeLists.txt`/`.cmake` files changed; zero errors), 2/3
   steps did real work (`PluginHost.cpp.obj` rebuilt, `libgte_core.a`
   relinked). Configure output confirmed the expected honest no-op warnings
   for the 3 demo plugins now calling the NEW function by name:
   ```
   CMake Warning at cmake/MingwRuntime.cmake:181 (message):
     gte_apply_plugin_dll_shared_crt_linkage(demo_hello_world): active
     toolchain has no shared libstdc++ variant - honest no-op, ...
   ```
   (and the same for `demo_render_feature`/`demo_editor_panel`), plus the
   pre-existing, unrelated warning for `GreatTamanaEditor` still calling the
   ORIGINAL function name — confirming the split landed correctly.
2. `cmake --build build` — succeeded, 8/8 steps, zero errors (rebuilt the 3
   demo plugin `.dll`s, relinked `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`).
3. `browse_dir` on `build/plugins/` — contains only `demo_editor_panel.dll`,
   `demo_hello_world.dll`, `demo_render_feature.dll` (plus each plugin's own
   CMake object-file subfolder). **Honest note, exactly as the phase file
   requires:** none of the 3 runtime DLLs were present there BEFORE this
   phase either, since this machine's currently-ACTIVE `CMAKE_CXX_COMPILER`
   (`scoop`'s `gcc` package) has no shared `libstdc++-6.dll` at all
   (`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED=FALSE`), so mitigation (a)'s
   real effect (never staging those DLLs into `plugins/` in the first place)
   is **not empirically observable end-to-end on this machine's current
   toolchain** — the fix is real and structural (verified by reading the
   generated build rule: `mingw_copy_runtime_dll()` is simply never invoked
   for the 3 demo plugin targets anymore, confirmed by the absence of any
   "Staging ... next to demo_xxx" `POST_BUILD` custom-command line in this
   build's own output, whereas `GreatTamanaEditor.exe`'s own link step still
   shows no such staging either for the same toolchain-support reason — the
   difference is code-level, not currently observable as a DLL appearing vs.
   not appearing on disk).
4. Live check: `run_app_background` on the rebuilt `GreatTamanaEditor.exe`
   (PID 17716), `GET /get_logs?limit=50` → `count:6`, all 3 demo plugins
   (`DemoEditorPanelPlugin`, `HelloWorldPlugin`, `DemoRenderFeaturePlugin`)
   loaded successfully, the PHASE1 shared-CRT startup warning still fires
   once as expected, and no new/unexpected warning appeared. `stop_app_background`
   (PID 17716) — stopped cleanly.
5. **OPTIONAL stronger proof — RUN, and it worked:** checked first whether the
   active toolchain's own `bin/` has a real `libstdc++-6.dll` — it does not
   (per step 3's note), but a SECOND, already-installed, shared-runtime-capable
   MinGW toolchain exists on this machine at
   `C:\Users\F5954\scoop\apps\mingw\current\bin\libstdc++-6.dll` (the one
   `MingwRuntime.cmake`'s own top-of-file comment references as
   `scoop install mingw`, GCC 16.2.0, deliberately not yet switched to as
   `CMAKE_CXX_COMPILER`). Copied that one real DLL by hand into
   `build/plugins/libstdc++-6.dll`, restarted the Editor (PID 8556), and
   `GET /get_logs?limit=50` showed `count:7` with a NEW entry:
   ```
   {"category":"PluginHost","level":"Info","message":"Skipping known
   non-plugin runtime file: C:\\Users\\F5954\\Documents\\TAMANA\\
   GreatTamanaEngine\\build\\plugins\\libstdc++-6.dll", ...}
   ```
   appearing immediately after the 3 "Loaded plugin ..." lines and before the
   `Network`/`EditorHost` lines — exactly the expected `IsKnownNonPluginFilename()`
   ignore-list behavior, and the 3 real demo plugins still loaded normally
   alongside it. `stop_app_background` (PID 8556) — stopped cleanly. Deleted
   the manually-copied `build/plugins/libstdc++-6.dll` afterward — confirmed
   via a fresh `browse_dir` that `build/plugins/` is back to exactly the 3
   demo plugin `.dll`s, nothing left behind.

## Deviations from the plan

None. Both mitigations were implemented exactly as the phase file specifies,
word-for-word for the CMake doc comment and the `PluginHost.cpp` ignore-list
block. `demo_render_feature_second` (PHASE5's future plugin) did not exist yet,
so there was nothing extra to update there. PHASE7's case-insensitive
extension-matching change has not landed yet either, so no merge/adaptation
of the two independent checks was needed — they will simply coexist cleanly
whenever PHASE7 lands, per the phase file's own note. No design ambiguity was
encountered that required `ask_questions`.

## Locked Design Decisions check

No architectural shape changed: no plugin `.dll`'s link dependencies changed
(only where a build-time DLL copy lands), `PluginHost`'s public interface
(`LoadPlugins()`, `AllLoadedModules()`, `LoadedModuleCount()`) is unchanged,
and the fingerprint ABI contract is untouched. This phase only adds (a) a
second CMake helper function nobody outside plugin `.dll` targets calls, and
(b) a defensive filename skip inside an existing scan loop — neither changes
any of the 10 Locked Design Decisions from
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`.
