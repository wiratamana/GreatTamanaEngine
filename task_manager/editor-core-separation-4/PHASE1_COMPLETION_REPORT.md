# PHASE1 — Shared-CRT Fingerprint Honesty + Startup Warning — COMPLETION REPORT

**Status:** DONE. Verified with an incremental `gte_core` build and a live
`GreatTamanaEditor.exe` run via `GET /get_logs`.

## What changed

1. **`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`** — replaced the
   `sharedRuntimeLinkage` field's doc comment (previously claiming the host
   "REFUSES to load ANY plugin ... unless its OWN fingerprint has this field
   set to 1", which is false — no such standalone check exists anywhere in
   the code). The new comment states the true behavior: the field is only
   ever compared for EQUALITY by `operator==` (unchanged), plus an "Honest
   correction" note pointing at the real mitigation added in this phase
   (`PluginHost::LogSharedCrtRiskWarningOnce()`). Wording matches the phase
   file's own Step 3.1 text exactly (word-for-word, per the plan).

2. **`src/Core/Plugins/PluginHost.h`** — added a new private method
   declaration `void LogSharedCrtRiskWarningOnce();` and a new private
   member `bool m_sharedCrtRiskWarningLogged = false;`.

3. **`src/Core/Plugins/PluginHost.cpp`** —
   - Added the `LogSharedCrtRiskWarningOnce()` definition: guards on
     `m_sharedCrtRiskWarningLogged` (set true immediately, so it is "at most
     once per instance" even under re-entrancy/exceptions), reads
     `MakeThisBuildsFingerprint()`, returns silently if
     `sharedRuntimeLinkage != 0`, otherwise emits one
     `GTE_LOG_WARNING("PluginHost", ...)` naming the real risk (statically-
     linked host/plugin do not share one process-wide heap; any future
     cross-boundary heap-ownership transfer is undefined behavior).
   - Called `LogSharedCrtRiskWarningOnce();` as the very first line inside
     `PluginHost::LoadPlugins()`, before the `std::filesystem::exists()`
     check, exactly as the phase file specifies (fires even if
     `plugins/` doesn't exist yet, since the risk is about this build's own
     linkage, not about whether any plugin loaded).

4. **`docs/conventions/plugin-architecture.md`** — added the exact "Honest
   correction (`editor-core-separation-4` campaign, PHASE1)" paragraph from
   the phase file's Step 3.3, placed right after the existing "A second,
   shared-runtime-CAPABLE toolchain was installed..." paragraph, before the
   "## Where things live, physically" section. No existing content removed.

5. **`plugins/gte_plugin_abi/PublicSurface.md`** — NOT edited. Re-checked
   first via `search_in_dir` for "REFUSES" across
   `plugins/gte_plugin_abi/` — only `GtePluginAbiFingerprint.h` itself
   contained the false claim; `PublicSurface.md` does not repeat it
   verbatim, matching the phase file's own prediction. Skipped per Step
   3.4's own instruction to avoid an unnecessary edit.

No other files were touched. The plugin architecture's shape (the 10 Locked
Design Decisions from `editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`)
is unchanged — this phase only corrects documentation and adds a diagnostic
log line.

## Verification evidence

1. `cmake --build build --target gte_core` — succeeded, 3/3 steps, zero
   errors/warnings:
   ```
   [1/3] Building CXX object CMakeFiles/gte_core.dir/src/Core/Plugins/PluginHost.cpp.obj
   [2/3] Building CXX object CMakeFiles/gte_core.dir/src/Core/Core.cpp.obj
   [3/3] Linking CXX static library libgte_core.a
   ```
2. `cmake --build build --target GreatTamanaEditor` — succeeded, relinked
   `GreatTamanaEditor.exe` with the updated `gte_core`.
3. Launched `build\GreatTamanaEditor.exe` via `run_app_background` (PID
   13084), then `GET /get_logs?limit=50`. The very first log entry
   (`id:1`, `frame:0`, `timestamp_seconds:0.0`) is:

   ```
   category=PluginHost, level=Warning:
   "This build was NOT linked with shared/DLL CRT (sharedRuntimeLinkage=0).
   A statically-linked host and statically-linked plugin .dll(s) do NOT
   share one process-wide heap - allocating on one side of the plugin ABI
   boundary and freeing on the other (even indirectly) is undefined
   behavior. This is currently a real, unenforced risk on this build -
   see plugins/gte_plugin_abi/GtePluginAbiFingerprint.h's
   sharedRuntimeLinkage field and docs/conventions/plugin-architecture.md
   for the full explanation."
   ```

   This confirms the warning fires exactly once, as the very first log
   entry (before the 3 "Loaded plugin ..." info lines that follow it),
   proving `LogSharedCrtRiskWarningOnce()` runs before any plugin scanning,
   and that this machine's current configuration genuinely has
   `sharedRuntimeLinkage == 0` (as expected — this dev machine's only active
   toolchain cannot produce a shared-CRT binary, per
   `cmake/MingwRuntime.cmake` and `docs/conventions/plugin-architecture.md`).
   All 3 demo plugins still loaded successfully afterward (`DemoEditorPanelPlugin`,
   `HelloWorldPlugin`, `DemoRenderFeaturePlugin`), confirming this new warning
   does not interfere with normal plugin loading.
4. `stop_app_background(pid: 13084)` — stopped cleanly.

## Deviations from the plan

None. The doc-comment wording, the method signature/placement, the call
site, and the `docs/conventions/plugin-architecture.md` paragraph all match
the phase file's Step 3.1/3.2/3.3 text exactly. `PublicSurface.md` was
correctly left untouched per Step 3.4's own guidance, confirmed via a fresh
`search_in_dir` check rather than assumed. No ambiguity was encountered that
required `ask_questions`.
