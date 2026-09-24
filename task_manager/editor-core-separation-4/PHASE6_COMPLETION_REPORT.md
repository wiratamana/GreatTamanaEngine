# PHASE6 — Defensive `GtePluginModuleInfo` Buffer Reads — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `gte_core` + `GreatTamanaEngineTests`
build, a filtered `FixedBufferReaderTest.*` run, and a live `GreatTamanaEditor.exe`
run via `GET /get_logs`.

## Option chosen: (a) — header-only `FixedBufferReader.h`

Chosen exactly per the phase file's own Step 3.4 preference. Reason it fits
this codebase well (re-confirmed, not just assumed): `tests/CMakeLists.txt`'s
own taxonomy — checked via `browse_dir` on `tests/Core/` first — already shows
the PHASE5 precedent of exactly this shape: a small, pure, file-local helper
(`CountModulesImplementingRenderFeature()`) was extracted into its own
header+`.cpp` pair (`src/Core/Plugins/PluginRenderFeatureDiagnostics.h/.cpp`)
specifically so it could be Tier-1-tested directly from
`tests/Core/PluginRenderFeatureDiagnosticsTests.cpp`. `ReadFixedBuffer()` is
even simpler (one line, no `.cpp` needed at all — genuinely `inline`,
header-only), so option (a) was a strictly easier version of an already-used,
already-successful pattern in this exact repo. No concrete reason was found
to prefer option (b) instead, so `ask_questions` was not needed here.

## What changed

1. **`src/Core/Plugins/FixedBufferReader.h`** (new) — header-only, `inline`,
   `namespace gte`, `std::string ReadFixedBuffer(const char* buffer,
   std::size_t bufferSize)`, implemented exactly as the phase file's Step 3.1
   specifies: `return std::string(buffer, strnlen(buffer, bufferSize));`.
   Includes `<cstddef>`, `<cstring>`, `<string>` explicitly (no reliance on
   any transitive include).

2. **Root `CMakeLists.txt`** — added `src/Core/Plugins/FixedBufferReader.h` to
   `gte_core`'s unconditional source list, right after
   `src/Core/Plugins/PluginHost.cpp`, with an explanatory comment block
   matching this repo's own established per-entry comment convention.

3. **`src/Core/Plugins/PluginHost.cpp`** —
   - Added `#include "FixedBufferReader.h"` right after the existing
     `#include "../../../plugins/gte_plugin_abi/PluginExports.h"` line.
     `#include <cstring>` was already present in this file (added by an
     earlier phase for `_stricmp`), so no duplicate include was needed for
     `strnlen` itself — it now comes in cleanly via `FixedBufferReader.h`'s
     own explicit `<cstring>` include, and the file's pre-existing
     `<cstring>` include stays for `_stricmp`'s own use in
     `IsKnownNonPluginFilename()`.
   - The one real production call site (`TryLoadOnePlugin()`'s "5. Success."
     step) now reads:
     ```cpp
     GTE_LOG_INFO("PluginHost", "Loaded plugin '" + ReadFixedBuffer(info.name, sizeof(info.name)) + "' v"
         + ReadFixedBuffer(info.version, sizeof(info.version)) + " from " + pathStr);
     ```
     Note: this phase's own sketch (Step 3.2) had an extra stray `'` before
     `" from "` that would have changed the visible log format
     (`...v1.0.0' from ...` instead of the original `...v1.0.0 from ...`) —
     caught this during implementation (the phase file itself warned "verify
     the real, compiling form yourself rather than pasting this verbatim"),
     and wrote the corrected form above so the STEP 3 live-check requirement
     ("confirm the existing 4 lines... read correctly, unchanged in visible
     content") is genuinely satisfied byte-for-byte, not just "close enough."
   - No private anonymous-namespace `ReadFixedBuffer()` was ever added to this
     file — the free function lives ONLY in the new header, called directly
     as `gte::ReadFixedBuffer` (unqualified, since `PluginHost.cpp` is itself
     inside `namespace gte`).

4. **`DescribeFingerprintMismatch()`** (same file) — **left unchanged**, on
   purpose. Re-read `plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h.in`'s
   own generation code directly (not just trusted the phase file's own
   summary) to confirm this myself:
   ```cpp
   GtePluginAbiFingerprint fp{};   // value-initializes the WHOLE struct first (every byte 0)
   ...
   for (int i = 0; kCompilerId[i] != '\0' && i < 15; ++i) { fp.compilerId[i] = kCompilerId[i]; }
   for (int i = 0; kBuildConfig[i] != '\0' && i < 15; ++i) { fp.buildConfig[i] = kBuildConfig[i]; }
   ```
   `compilerId`/`buildConfig` are both `char[16]`
   (`GtePluginAbiFingerprint.h`). `fp{}` zero-initializes all 16 bytes of each
   FIRST, then the copy loop is hard-bounded to `i < 15` — so byte index 15
   is mechanically GUARANTEED to remain `'\0'` no matter what
   `CMAKE_CXX_COMPILER_ID`/`CMAKE_BUILD_TYPE` actually resolve to at configure
   time (even a 20+ character compiler ID string can only ever fill indices
   0-14). This is COMPILER/CMake-generated code, never hand-written by a
   plugin author — the entire bug class this phase fixes (a plugin author
   forgetting/failing to null-terminate) cannot occur here. This is a real,
   structural difference from `GtePluginModuleInfo` (hand-written by
   whoever authors a given plugin's `GetModuleInfo()`, with no generated
   code enforcing anything). Confirmed: `DescribeFingerprintMismatch()` is
   already safe and needs no change — left exactly as-is.

5. **`tests/Core/FixedBufferReaderTests.cpp`** (new) — the exact 3 `TEST()`
   cases from the phase file's Step 3.4, verbatim (adjusted only to call
   `gte::ReadFixedBuffer` and `#include "Core/Plugins/FixedBufferReader.h"`,
   matching this repo's own established test-file include-path convention —
   confirmed by reading `tests/Core/PluginRenderFeatureDiagnosticsTests.cpp`'s
   own first line first):
   - `ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly`
   - `ReadFixedBuffer_NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore`
   - `ReadFixedBuffer_EmptyBufferReadsEmptyString`

   Placed in `tests/Core/FixedBufferReaderTests.cpp` (flat, NOT
   `tests/Core/Plugins/`) — confirmed via `browse_dir` on `tests/Core/`
   FIRST that no `Plugins/` subfolder convention exists there yet (it
   contains `CoreHeadlessConstructionTests.cpp`, `EditorPanelRegistryTests.cpp`,
   `LogSinkTests.cpp`, `PluginRenderFeatureDiagnosticsTests.cpp`,
   `TimeTests.cpp` — all flat, including PHASE5's own plugin-adjacent test
   file), so this phase matches that existing flat convention rather than
   introducing a new subfolder nobody else uses yet.

6. **`tests/CMakeLists.txt`** — added `Core/FixedBufferReaderTests.cpp` to
   `GTE_TEST_SOURCES`, right after `Core/PluginRenderFeatureDiagnosticsTests.cpp`,
   with a comment block matching this file's own established per-entry
   convention.

No other files were touched. `PluginHost.h`'s public interface,
`GtePluginModuleInfo`, `GtePluginAbiFingerprint`, and every other call site
in `src/` are unchanged. `tools/ci/gte_plugin_abi_handshake_probe/main.cpp`
and `tools/ci/gte_plugin_isolation_probe/main.cpp` were confirmed (via a
fresh read, not just trusted the phase file's own claim) to still be the
only other `info.name`/`info.description` consumers, and were deliberately
left untouched, exactly as the phase file's Step 2 instructs.

## Verification evidence

1. `cmake --build build --target gte_core` — succeeded, 2/3 steps did real
   work (`PluginHost.cpp.obj` rebuilt to pick up the new include + call site,
   `libgte_core.a` relinked):
   ```
   [1/3] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginHost.cpp.obj
   [2/3] Linking CXX static library libgte_core.a
   ```
   (CMake auto-reconfigured first since `CMakeLists.txt` changed — only the
   expected, pre-existing, unrelated `MingwRuntime.cmake`/KTX `git describe`
   warnings appeared, zero new warnings/errors.)

2. `cmake --build build --target GreatTamanaEngineTests` — succeeded, 2/2
   steps, zero errors:
   ```
   [1/2] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Core/FixedBufferReaderTests.cpp.obj
   [2/2] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
   ```

3. `build\tests\GreatTamanaEngineTests.exe --gtest_filter=FixedBufferReaderTest.*`
   — **3 tests from 1 test suite, 3 PASSED, 0 failed**, including the
   deliberately non-terminated full-buffer case:
   ```
   [ RUN      ] FixedBufferReaderTest.ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly
   [       OK ] FixedBufferReaderTest.ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly (0 ms)
   [ RUN      ] FixedBufferReaderTest.ReadFixedBuffer_NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore
   [       OK ] FixedBufferReaderTest.ReadFixedBuffer_NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore (0 ms)
   [ RUN      ] FixedBufferReaderTest.ReadFixedBuffer_EmptyBufferReadsEmptyString
   [       OK ] FixedBufferReaderTest.ReadFixedBuffer_EmptyBufferReadsEmptyString (0 ms)
   [==========] 3 tests from 1 test suite ran. (1 ms total)
   [  PASSED  ] 3 tests.
   ```

4. `cmake --build build --target GreatTamanaEditor` — succeeded (1/1 link
   step, relinked with the updated `gte_core`).

5. Live check: `run_app_background` on the rebuilt `GreatTamanaEditor.exe`
   (PID 18564), `GET /get_logs?limit=50` → `200`, `count:8`. All 4
   "Loaded plugin '...'" lines are present and **byte-for-byte unchanged in
   visible content** from PHASE5's own documented baseline:
   ```
   "Loaded plugin 'DemoEditorPanelPlugin' v1.0.0 from ...\\build\\plugins\\demo_editor_panel.dll"
   "Loaded plugin 'HelloWorldPlugin' v1.0.0 from ...\\build\\plugins\\demo_hello_world.dll"
   "Loaded plugin 'DemoRenderFeaturePlugin' v1.0.0 from ...\\build\\plugins\\demo_render_feature.dll"
   "Loaded plugin 'DemoRenderFeaturePluginSecond' v1.0.0 from ...\\build\\plugins\\demo_render_feature_second.dll"
   ```
   The PHASE1 shared-CRT startup warning and the PHASE5 multi-render-feature
   warning both still fire exactly as before (`id:1` and `id:6`
   respectively). `stop_app_background(pid: 18564)` — stopped cleanly.

## Deviations from the plan

One real deviation, caught and corrected during implementation (not a design
change, a bug in the phase file's own illustrative sketch): Step 3.2's sketch
code had a stray extra `'` character right before `" from "` that would have
silently changed the log line's visible format. Fixed to the form shown above,
which reproduces the ORIGINAL (pre-PHASE6) log line's exact visible text —
confirmed via the live `/get_logs` check in evidence item 5. No other
deviation. `DescribeFingerprintMismatch()` was re-confirmed safe by directly
reading `GtePluginAbiFingerprintGenerated.h.in`'s own generation code (not
just trusting the phase file's summary) and left unchanged, exactly as Step
3.3 anticipates as the likely outcome. No design ambiguity was encountered
that required `ask_questions` — the phase file's own Step 3.4 preference for
option (a) was followed with a concrete, re-confirmed reason (the PHASE5
precedent), so no alternative needed asking about.

## Locked Design Decisions check

No architectural shape changed: `PluginHost`'s public interface
(`LoadPlugins()`, `AllLoadedModules()`, `LoadedModuleCount()`) is unchanged,
`GtePluginModuleInfo`/`GtePluginAbiFingerprint` themselves are byte-for-byte
unchanged (still plain, fixed-size POD structs — Locked Design Decision #3
and #5), and no plugin `.dll`'s ABI contract changed. This phase adds one
new header-only, pure helper function and uses it at the one existing log
call site — it does not change any of the 10 Locked Design Decisions from
`task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`.
