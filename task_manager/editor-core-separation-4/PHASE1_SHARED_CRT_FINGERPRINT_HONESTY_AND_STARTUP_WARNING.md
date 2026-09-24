# PHASE1 — Shared-CRT Fingerprint Honesty + Startup Warning

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first.

**Severity:** CRITICAL (Issue #1 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "1.
CRITICAL — the documented 'shared-CRT refusal' safety gate does not exist in
code".

---

## Step 1: The Goal

Make the code and its own documentation agree with each other, honestly. Today,
`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h` (lines 51-59) claims, in a
doc comment, that "the host REFUSES to load ANY plugin — and, more
fundamentally, refuses to even attempt plugin loading at all — unless its OWN
fingerprint has `sharedRuntimeLinkage` set to 1." **This is not true of the real
code.** `sharedRuntimeLinkage` is only ever used inside `PluginHost.cpp`'s
`operator==` field-by-field comparison (host vs. plugin) — there is no
standalone "refuse everything if the host's own value is 0" check anywhere.

Design decision already made (confirmed via `ask_questions` during this
campaign's planning — do not re-litigate this, implement it exactly as
decided): **do NOT implement the literal hard refusal.** This development
machine's only usable MinGW toolchain cannot produce shared-CRT binaries at
all (documented already in `cmake/MingwRuntime.cmake`'s own top-of-file
comment and `docs/conventions/plugin-architecture.md`) — a hard refusal would
mean the host's own fingerprint always has `sharedRuntimeLinkage == 0` on this
machine, so a literal "refuse unless 1" gate would **permanently disable all
plugin loading** here, including the three existing demo plugins and every
probe. That is a worse outcome than the current honest gap.

Instead, this phase does two things:

1. **Fix the doc comment** in `GtePluginAbiFingerprint.h` (and the matching
   claim in `docs/conventions/plugin-architecture.md`) to say what the code
   ACTUALLY does today, plainly, with no invented guarantee.
2. **Add a real, loud, one-time-per-process startup `GTE_LOG_WARNING`**,
   emitted the first time `PluginHost::LoadPlugins()` runs, whenever the
   HOST's own fingerprint has `sharedRuntimeLinkage == 0` — so the real,
   ongoing risk ("a statically-linked host and statically-linked plugins do
   not share one process-wide heap; the moment a future plugin transfers heap
   ownership across the boundary, this is silent memory corruption") is
   surfaced loudly at runtime (visible via `GET /get_logs`), instead of
   silently, permanently true and invisible.

## Step 2: The Situation (exact current code)

`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`, lines 51-59 (the
`sharedRuntimeLinkage` field's own doc comment):

```cpp
    // 1 if this binary was built with shared (DLL) libgcc/libstdc++
    // linkage, 0 if statically linked - see PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #4 and this phase's own Step 3.4 below. The
    // host REFUSES to load ANY plugin - and, more fundamentally, refuses to
    // even attempt plugin loading at all - unless its OWN fingerprint has
    // this field set to 1 (see PluginHost's own doc comment, PHASE2), since
    // a statically-linked host/plugin pair does not share one process-wide
    // heap even if every other field matches exactly.
    std::uint32_t sharedRuntimeLinkage;
```

`src/Core/Plugins/PluginHost.cpp`'s `LoadPlugins()`/`TryLoadOnePlugin()`
(shown in full below) never reads `MakeThisBuildsFingerprint().sharedRuntimeLinkage`
in isolation anywhere — only inside `operator==`'s field-by-field compare
(`GtePluginAbiFingerprint.h` lines 65-75), which only ever compares plugin vs.
host for EQUALITY, never checks the host's own value alone.

`docs/conventions/plugin-architecture.md`'s "The shared/DLL CRT requirement..."
section does not repeat the false "REFUSES to load" claim verbatim, but its
neighboring prose in the header is the one place that needs correcting.

## Step 3: The Plan (exact changes)

### 3.1 Fix `GtePluginAbiFingerprint.h`'s doc comment

File: `plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`.

Replace the `sharedRuntimeLinkage` field's doc comment (the block shown in
Step 2 above) with wording that states the TRUE, current behavior — no
invented refusal, but pointing at the real, new mitigation this phase adds:

```cpp
    // 1 if this binary was built with shared (DLL) libgcc/libstdc++
    // linkage, 0 if statically linked - see the source design doc's Section
    // 8.1 ("Iron Rule") and PHASE0_MASTER_STRATEGY.md's Locked Design
    // Decision #4. Compared for EQUALITY (like every other field here) by
    // operator== below - a MISMATCH between host and plugin is refused like
    // any other fingerprint mismatch. Honest correction (editor-core-
    // separation-4 campaign, PHASE1): earlier documentation for this field
    // claimed the host additionally refuses to load ANY plugin outright
    // whenever its OWN fingerprint has this field read as 0 - THAT
    // STANDALONE REFUSAL DOES NOT EXIST IN THIS CODE, and is not implemented
    // by this phase either (this development machine's only usable
    // toolchain cannot produce a shared-CRT binary at all - see
    // cmake/MingwRuntime.cmake's own top-of-file comment - so a literal
    // refusal would disable plugin loading entirely here). What DOES exist,
    // as of this phase: PluginHost::LoadPlugins() logs a loud, one-time
    // GTE_LOG_WARNING whenever the HOST's own sharedRuntimeLinkage reads 0,
    // naming the real, ongoing risk explicitly (statically-linked host and
    // plugins do not share one process-wide heap - any future capability
    // that transfers heap ownership across the ABI boundary is undefined
    // behavior under this condition) - see PluginHost.cpp's own
    // LogSharedCrtRiskWarningOnce() for the exact wording. Treat this field
    // as "checked for equality, and its host-side value is loudly warned
    // about when 0" - not "a hard gate," until a dedicated future decision
    // implements a real hard gate (e.g. once this repo's active toolchain is
    // switched to a shared-CRT-capable one and this becomes enforceable
    // without disabling plugin loading altogether).
    std::uint32_t sharedRuntimeLinkage;
```

Keep `operator==`'s existing behavior (comparing this field for equality)
completely unchanged — that part of the code was already correct; only the
comment's extra, false claim above it needs correcting.

### 3.2 Add the one-time startup warning to `PluginHost`

File: `src/Core/Plugins/PluginHost.h`.

Add one new private method declaration to the `PluginHost` class:

```cpp
private:
    // ... existing TryLoadOnePlugin(...) declaration stays ...

    // PHASE1 (editor-core-separation-4 campaign) - logs ONE loud
    // GTE_LOG_WARNING, at most once per PluginHost instance, the first time
    // LoadPlugins() runs, if-and-only-if this build's own fingerprint has
    // sharedRuntimeLinkage == 0 - see GtePluginAbiFingerprint.h's own
    // corrected doc comment on that field for the full reasoning. A no-op
    // (correctly silent) on a build that genuinely achieved shared CRT
    // linkage.
    void LogSharedCrtRiskWarningOnce();

    bool m_sharedCrtRiskWarningLogged = false;
```

File: `src/Core/Plugins/PluginHost.cpp`.

Add the new method's definition (near `TryLoadOnePlugin`, inside the
anonymous namespace's neighboring real methods, i.e. as a normal member
function definition):

```cpp
void PluginHost::LogSharedCrtRiskWarningOnce()
{
    if (m_sharedCrtRiskWarningLogged) {
        return;
    }
    m_sharedCrtRiskWarningLogged = true;

    const GtePluginAbiFingerprint hostFingerprint = MakeThisBuildsFingerprint();
    if (hostFingerprint.sharedRuntimeLinkage != 0) {
        return; // Genuinely shared-CRT-linked - nothing to warn about.
    }

    GTE_LOG_WARNING("PluginHost",
        "This build was NOT linked with shared/DLL CRT (sharedRuntimeLinkage=0). "
        "A statically-linked host and statically-linked plugin .dll(s) do NOT "
        "share one process-wide heap - allocating on one side of the plugin ABI "
        "boundary and freeing on the other (even indirectly) is undefined "
        "behavior. This is currently a real, unenforced risk on this build - "
        "see plugins/gte_plugin_abi/GtePluginAbiFingerprint.h's "
        "sharedRuntimeLinkage field and docs/conventions/plugin-architecture.md "
        "for the full explanation.");
}
```

Call it as the FIRST line inside `PluginHost::LoadPlugins()` (before the
`std::filesystem::exists()` check), so the warning fires even on a machine
whose `plugins/` folder doesn't exist yet (the risk is about THIS build's own
linkage, not about whether any plugin actually loaded):

```cpp
void PluginHost::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    LogSharedCrtRiskWarningOnce();

    if (!std::filesystem::exists(pluginsDirectory)) {
        ...
```

This keeps the "at most once per `PluginHost` instance" guarantee even if
`LoadPlugins()` is somehow called more than once (matches `PluginHost.h`'s own
existing doc comment about `LoadPlugins()` being safe, if unusual, to call more
than once).

### 3.3 Fix `docs/conventions/plugin-architecture.md`

File: `docs/conventions/plugin-architecture.md`, section "The shared/DLL CRT
requirement, and this repository's own real, discovered limitation". Add one
short paragraph (do not delete any existing honest content there — it is
accurate) right after the existing "A second, shared-runtime-CAPABLE toolchain
was installed..." paragraph:

```markdown
**Honest correction (`editor-core-separation-4` campaign, PHASE1)**: an
earlier version of `GtePluginAbiFingerprint.h`'s own doc comment claimed the
host additionally refuses to load ANY plugin outright whenever its own
fingerprint has `sharedRuntimeLinkage` read as `0` — that standalone hard
refusal never existed in the real code, and is still not implemented (doing so
today would disable plugin loading entirely on this development machine, since
its only usable toolchain cannot produce a shared-CRT binary at all). What
exists instead, as of this phase: `PluginHost::LoadPlugins()` logs one loud,
one-time `GTE_LOG_WARNING` naming this exact risk whenever the host's own
`sharedRuntimeLinkage` reads `0`, so it is visible (via `GET /get_logs`) rather
than silently, permanently true.
```

### 3.4 Update `plugins/gte_plugin_abi/PublicSurface.md` if needed

No type list changes here — `sharedRuntimeLinkage` was already documented as
part of `GtePluginAbiFingerprint`. Skip this file unless you find it also
repeats the false claim verbatim (it currently does not, per this phase's own
research — double-check before editing, do not add an unnecessary edit).

## Verification (fast, incremental — no full build)

1. `cmake --build build --target gte_core` (incremental — only `gte_core.a`
   and its direct dependents need rebuilding; this touches
   `src/Core/Plugins/PluginHost.h/.cpp` and a header-only ABI file).
2. Confirm zero compile errors/warnings introduced.
3. Live check: `run_app_background` the already-built
   `build\GreatTamanaEditor.exe` (rebuild it first if the incremental target
   above didn't already relink it — `cmake --build build --target
   GreatTamanaEditor`), then `gte_send_request` `GET /get_logs?limit=50` and
   confirm a `"PluginHost"` category warning line containing
   `"sharedRuntimeLinkage=0"` (or your exact wording) is present — this
   PROVES the warning fires for real on this exact machine's current
   configuration (which has `sharedRuntimeLinkage == 0` today). Then
   `stop_app_background` it.
4. If a genuine ambiguity comes up (e.g. exact wording review, where exactly
   to place the call), use `ask_questions` rather than guessing silently.

## Completion

Write `PHASE1_COMPLETION_REPORT.md` in this same folder: what changed, the
exact log line observed via `GET /get_logs`, any deviation from this plan.
Then `git_add` + `git_commit` (message referencing PHASE1).
