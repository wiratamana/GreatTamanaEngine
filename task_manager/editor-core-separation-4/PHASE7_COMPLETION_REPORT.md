# PHASE7 — Case-Insensitive `.dll` Extension Matching — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `gte_core` build, a rebuilt
`GreatTamanaEditor.exe`, and a live manual `CapitalCase.DLL` test via
`GET /get_logs`.

## What changed

**`src/Core/Plugins/PluginHost.cpp`** (the only file touched):

1. Added `#include <cctype>` (right after the pre-existing `#include
   <cstring>`), needed for `std::tolower`.
2. Added a new helper in the existing anonymous namespace, placed right
   after PHASE4's `IsKnownNonPluginFilename()` (and after
   `DescribeFingerprintMismatch()`, unchanged, above it):
   ```cpp
   bool HasDllExtensionCaseInsensitive(const std::filesystem::path& filePath)
   {
       std::string ext = filePath.extension().string();
       for (char& c : ext) {
           c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
       }
       return ext == ".dll";
   }
   ```
   Doc comment matches the phase file's Step 3.1 text exactly, word-for-word.
3. In `LoadPlugins()`'s per-file loop, replaced the case-sensitive
   `entry.path().extension() != ".dll"` check with:
   ```cpp
   if (!HasDllExtensionCaseInsensitive(entry.path())) {
       GTE_LOG_DEBUG("PluginHost", "Skipping non-.dll file while scanning plugins directory: " + entry.path().string());
       continue;
   }
   ```
   using `GTE_LOG_DEBUG` (not `INFO`/`WARNING`), exactly as the phase file
   requires — quiet by default, discoverable only when debug-level logs are
   queried.
4. PHASE4's `IsKnownNonPluginFilename()` check was left **fully intact**,
   unchanged, immediately after the new extension check — confirmed by
   reading the loop body both before and after this edit; the two checks now
   coexist cleanly exactly as PHASE4's own completion report predicted they
   would.

No other file in `src/` was touched. `PluginHost.h`'s public interface is
unchanged.

### `docs/conventions/plugin-architecture.md`

Checked first via `search_in_dir` for `"extension"` (zero matches) and
`"scan"` (one match, a "Where things live, physically" bullet only saying
`PluginHost` "actually scans" the `<build-dir>/plugins/` folder at startup —
no mention of the match rule's case-sensitivity at all). The document does
not describe the folder scan as strictly `.dll`-only case-sensitive (or at
all, at this level of detail) today, so per this phase's own Step 3.2/Step 2
instruction ("skip this edit" when not found), **no edit was made.**

### Optional Tier-1 test

**Not added.** The phase file marks this optional for a LOW-severity item,
and required only if `HasDllExtensionCaseInsensitive()` were moved into a
directly-includable header for some other reason. It was implemented exactly
as the phase file's own Step 3.1 code specifies — staying a private,
anonymous-namespace, file-local helper inside `PluginHost.cpp` (mirroring
`DescribeFingerprintMismatch()`'s and `IsKnownNonPluginFilename()`'s own
existing placement, not extracted into a header like PHASE5's
`PluginRenderFeatureDiagnostics.h` or PHASE6's `FixedBufferReader.h`), so
no new directly-includable header was created and the optional test was
skipped. The manual `CapitalCase.DLL` proof below already gives concrete,
real, end-to-end evidence for a LOW-severity diagnostic-only change.

## Verification evidence

1. `cmake --build build --target gte_core` — succeeded, 2/2 steps, zero
   errors:
   ```
   [1/2] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginHost.cpp.obj
   [2/2] Linking CXX static library libgte_core.a
   ```
2. `cmake --build build --target GreatTamanaEditor` — succeeded (relink step
   only).
3. `browse_dir` on `build/plugins/` before the manual test: confirmed exactly
   the 4 existing demo plugin `.dll`s (`demo_editor_panel.dll`,
   `demo_hello_world.dll`, `demo_render_feature.dll`,
   `demo_render_feature_second.dll`), nothing else.
4. Manual proof: `copy /Y build\plugins\demo_hello_world.dll
   build\plugins\CapitalCase.DLL` (byte-identical copy, deliberately
   different filename AND uppercase extension). `run_app_background` on the
   rebuilt `GreatTamanaEditor.exe` (PID 17844). `GET /get_logs?limit=100` →
   `200`, `count:9`. Real log output, confirming the fix works end-to-end:
   ```
   {"category":"PluginHost","frame":0,"id":2,"level":"Info",
    "message":"Loaded plugin 'HelloWorldPlugin' v1.0.0 from
    C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\build\\plugins\\CapitalCase.DLL",
    "timestamp_seconds":0.004518}
   ```
   As expected/predicted by the phase file: this reports the SAME module
   name/version (`HelloWorldPlugin` v1.0.0) as the real `demo_hello_world.dll`
   copy (`id:4`, loaded a moment later), since `CapitalCase.DLL` is a
   byte-identical copy — this test is purely about the extension-matching
   path, not plugin identity, exactly as the phase file states. All 4 real
   demo plugins plus the `CapitalCase.DLL` copy loaded successfully (5
   "Loaded plugin" lines total, `id:2`-`id:6`), the PHASE1 shared-CRT startup
   warning (`id:1`) and the PHASE5 multi-render-feature warning (`id:7`) both
   still fired exactly as in every prior phase's baseline — no regression.
5. `stop_app_background(pid: 17844)` — stopped cleanly.
6. `del build\plugins\CapitalCase.DLL` — deleted the manually-copied test
   file. Confirmed via a fresh `browse_dir` on `build/plugins/` immediately
   after: back to exactly the same 4 demo plugin `.dll`s as step 3, nothing
   left behind.

No `.dll.bak`/no-extension edge case was ever actually encountered during
this manual test (only the one deliberately-crafted `CapitalCase.DLL` file
was created), so no genuine ambiguity requiring `ask_questions` came up
during implementation or verification.

## Deviations from the plan

None. The helper's signature, placement, and doc comment; the loop's
replacement check; the `GTE_LOG_DEBUG` level choice; and the manual
`CapitalCase.DLL` test procedure all match the phase file's own Step 3
text and Verification section exactly. `docs/conventions/plugin-architecture.md`
was correctly left untouched after a fresh `search_in_dir` check found it
doesn't describe the scan's case-sensitivity at all today. The optional
Tier-1 test was correctly skipped since the function was not moved into a
directly-includable header. No design ambiguity was encountered that
required `ask_questions`.

## Locked Design Decisions check

No architectural shape changed: `PluginHost`'s public interface
(`LoadPlugins()`, `AllLoadedModules()`, `LoadedModuleCount()`) is unchanged,
no ABI contract changed, and PHASE4's `IsKnownNonPluginFilename()` ignore-list
mechanism is fully intact and unchanged. This phase only widens which
filenames are considered plugin candidates (case-insensitive `.dll` matching
instead of case-sensitive) and adds one quiet debug-level diagnostic log line
— it does not change any of the 10 Locked Design Decisions from
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`.
