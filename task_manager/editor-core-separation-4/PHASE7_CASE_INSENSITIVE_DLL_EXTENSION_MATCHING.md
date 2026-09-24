# PHASE7 — Case-Insensitive `.dll` Extension Matching

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE6`'s own `COMPLETION_REPORT.md` files if they exist (Phase 4 in
particular already touches this exact loop in `PluginHost::LoadPlugins()` —
merge cleanly with its ignore-list check, do not clobber it).

**Severity:** LOW (Issue #7 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "7.
LOW — silent, undiagnosed skip for any `.DLL` (capitalized extension)".

---

## Step 1: The Goal

`PluginHost::LoadPlugins()`'s file-extension check is case-sensitive:

```cpp
if (entry.path().extension() != ".dll") {
    continue;
}
```

`std::filesystem::path::extension()` returns whatever case the real filename
on disk has (NTFS preserves case even though lookups are case-insensitive). A
file named `MyPlugin.DLL` (uppercase — not unusual; some build tools/zip
tools/CI artifacts preserve or force uppercase extensions) is silently
skipped, with **no log line at all**, not even a debug one — the loop just
`continue`s. Given this whole feature's own pitch is "drop a `.dll` into
`plugins/`, zero config," a case mismatch producing total, silent invisibility
is a bad diagnostic experience for exactly the audience (third-party plugin
authors) this feature exists for.

## Step 2: The Situation

Exact current code, `src/Core/Plugins/PluginHost.cpp`, `LoadPlugins()`:

```cpp
for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory)) {
    if (!entry.is_regular_file()) {
        continue;
    }
    if (entry.path().extension() != ".dll") {
        continue;
    }
    TryLoadOnePlugin(entry.path());
}
```

If Phase 4 (`PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md`) already
landed before this phase, this loop will also already contain an
`IsKnownNonPluginFilename()` check — keep that fully intact, this phase only
changes the extension comparison itself, nothing else in the loop.

## Step 3: The Plan (exact changes)

### 3.1 Case-insensitive extension check, plus a discoverable skip log

File: `src/Core/Plugins/PluginHost.cpp`.

Add a small helper (anonymous namespace, alongside `DescribeFingerprintMismatch()`
and (if Phase 4 landed) `IsKnownNonPluginFilename()`):

```cpp
// editor-core-separation-4 campaign, PHASE7
// (PHASE7_CASE_INSENSITIVE_DLL_EXTENSION_MATCHING.md) -
// std::filesystem::path::extension() is a case-SENSITIVE string return on
// Windows (NTFS preserves case even though lookups are case-insensitive) -
// a file literally named "MyPlugin.DLL" must still be recognized as a
// plugin candidate.
bool HasDllExtensionCaseInsensitive(const std::filesystem::path& filePath)
{
    std::string ext = filePath.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".dll";
}
```

`std::tolower`/`<cctype>` — add `#include <cctype>` if not already present.

Replace the loop body's extension check with:

```cpp
for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory)) {
    if (!entry.is_regular_file()) {
        continue;
    }
    if (!HasDllExtensionCaseInsensitive(entry.path())) {
        GTE_LOG_DEBUG("PluginHost", "Skipping non-.dll file while scanning plugins directory: " + entry.path().string());
        continue;
    }
    // (Phase 4's IsKnownNonPluginFilename() check, if present, stays here, unchanged, right after this.)
    TryLoadOnePlugin(entry.path());
}
```

Use `GTE_LOG_DEBUG` (not `INFO`/`WARNING`) for the "skipping non-.dll file"
line specifically — this fires for every ordinary non-plugin file that might
legitimately sit in a `plugins/` folder (a `README.md`, a `.pdb` debug
symbols file, etc.), so it must not be noisy at the default log level; it
only needs to be DISCOVERABLE when someone is actually debugging why their
`.DLL` (or `.txt`, or anything else) isn't loading, by explicitly querying
debug-level logs.

### 3.2 Update `docs/conventions/plugin-architecture.md` if it describes the
extension-matching rule

Search for any prose there describing the folder scan as `.dll`-only; if
found, add one clause noting the match is now case-insensitive. If it doesn't
go into this level of detail today, skip this edit.

## Verification (fast, incremental — no full build)

1. `cmake --build build --target gte_core` (rebuilds `PluginHost.cpp`).
2. Manual proof (cheap, precise, no test-framework ceremony needed for a LOW
   item like this, though feel free to also add a Tier-1 test for
   `HasDllExtensionCaseInsensitive()` directly if you moved it to a header —
   it is a pure, trivially testable function, so prefer adding
   `tests/Core/Plugins/HasDllExtensionCaseInsensitiveTests.cpp` — a couple of
   `TEST()` cases covering `.dll`/`.DLL`/`.Dll`/`.txt`/no-extension — if time
   allows; this is optional for a LOW-severity item, required if you already
   extracted the function into a directly-includable header for some other
   reason):
   - Manually copy one existing built demo plugin `.dll` (e.g.
     `build/plugins/demo_hello_world.dll`) to a new file
     `build/plugins/CapitalCase.DLL` (same bytes, different name/extension
     case) via `run_shell` (`copy` command).
   - `run_app_background` the rebuilt `GreatTamanaEditor.exe`.
   - `gte_send_request` `GET /get_logs?limit=100` — confirm a `"Loaded
     plugin '...'"` line now appears for the `CapitalCase.DLL` copy too (it
     will report the SAME module name/version as `demo_hello_world`, since
     it's a byte-identical copy — that's fine and expected, this is purely
     testing the extension-matching path, not plugin identity).
   - `stop_app_background`, then delete the manually-copied `CapitalCase.DLL`
     test file so it doesn't linger in the build output.
3. If a genuinely ambiguous edge case comes up (e.g. a file with literally no
   extension, or `.dll.bak`), use `ask_questions` rather than guessing beyond
   what this phase's own scope requires.

## Completion

Write `PHASE7_COMPLETION_REPORT.md`: the exact manual `CapitalCase.DLL` test
performed and its real log output confirming it loaded, whether you added the
optional Tier-1 test. Then `git_add` + `git_commit` (message referencing
PHASE7).
