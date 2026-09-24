# PHASE1 — Plugin ABI Authoring Sugar — Foundation (two new header-only files)

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first, including its "Decisions
made without `ask_questions`" section (decisions #1 and #2 apply directly to this
phase). Also read the source proposal document in full:
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md`.

**Use `ask_questions` if anything below is ambiguous or looks wrong once you have
the real files open — do not silently guess.**

---

## Step 0 (before Step 1): confirm the starting state (`PHASE0_MASTER_STRATEGY.md` Workflow Rule 10)

Run `git_status` FIRST, before creating either new file — confirm the branch
still reads `feature/editor-core-separation` and the working tree is clean
(nothing already staged/modified that this phase didn't cause). Re-run it again
right before this phase's own final commit (see "Completion" at the end of this
file).

## Step 1: The Goal

Add exactly two new header-only files to `plugins/gte_plugin_abi/`:
`PluginExportsMacro.h` and `SingleCapabilityPluginModule.h`. Zero existing file in
`plugins/gte_plugin_abi/` is modified except `PublicSurface.md` (documentation
only — the explicit, reviewed list of what crosses the plugin ABI boundary).
**Zero existing demo plugin `.cpp` is touched in this phase** — that migration is
Phases 2-4. This phase is pure, inert addition: nothing anywhere in the codebase
includes either new header yet when this phase is done, so there is zero risk of
regressing any of the four demo plugins' current behavior.

## Step 2: The Situation

`plugins/gte_plugin_abi/` currently has exactly these files (confirmed via
`browse_dir`): `CMakeLists.txt`, `GtePluginAbiFingerprint.h`,
`GtePluginAbiFingerprintGenerated.h.in`, `GtePluginModuleInfo.h`,
`IEditorPanelModule.h`, `IPluginModule.h`, `IPluginPanelDrawContext.h`,
`IPluginRenderPassBuilder.h`, `IRenderFeatureModule.h`, `PluginExports.h`,
`PublicSurface.md`. `CMakeLists.txt` defines `gte_plugin_abi` as a plain
`INTERFACE` library whose only two include directories are
`${CMAKE_CURRENT_SOURCE_DIR}` (this folder itself) and
`${CMAKE_CURRENT_BINARY_DIR}/generated` (where `GtePluginAbiFingerprintGenerated.h`
lands, under a `gte_plugin_abi/` subfolder, via `configure_file()`) — **this means
adding a new header FILE to this folder requires zero `CMakeLists.txt` change at
all** (an `INTERFACE` library has no explicit source list to update; any `.h`
physically present under an already-included directory is automatically visible
to every consumer that already links `gte_plugin_abi`).

`IPluginModule.h` forward-declares `struct GtePluginModuleInfo;` only (no full
definition) — `SingleCapabilityPluginModule.h` needs the FULL definition (it reads
`sizeof(info.name)` etc.), so it must `#include "GtePluginModuleInfo.h"` directly,
never rely on `IPluginModule.h`'s forward declaration alone.

## Step 3: The Plan

### 3.1 — New file: `plugins/gte_plugin_abi/PluginExportsMacro.h`

Create this file with EXACTLY this content (this is the proposal document's own
Section "1. `PluginExportsMacro.h`", copied verbatim — no changes needed here, the
proposal's own code for this specific file is already correct):

```cpp
#pragma once

#include "GtePluginAbiFingerprint.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "IPluginModule.h"

// editor-core-separation-5 campaign, PHASE1
// (PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md) - hides the extern "C" {
// ... } export block every plugin .dll must write out by hand today (see
// PublicSurface.md's own "IPluginModule and the three fixed exports" section
// for exactly why extern "C" itself can never be removed - GetProcAddress()
// resolves these 3 names by exact, literal, unmangled string match, so
// PluginHost::TryLoadOnePlugin() and every existing plugin .dll are
// completely unaffected by this file - it produces the IDENTICAL exported
// symbols, just without the plugin author re-typing the same 8 lines every
// time).
//
// Two flavors, matching the two ways a plugin's IPluginModule can be produced:
//
// GTE_DEFINE_PLUGIN_EXPORTS(ModuleClass) - the module is heap-allocated fresh
//     each time GTE_CreatePluginModule() is called (`new ModuleClass()`), and
//     genuinely destroyed via `delete` when GTE_DestroyPluginModule() is
//     called. Use this when the module needs real per-instance construction
//     logic.
//
// GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(instanceExpr) - the module is a
//     single, already-existing object (typically a file-scope
//     `namespace { MyModule g_module; }`) - GTE_CreatePluginModule() just
//     returns its address, GTE_DestroyPluginModule() is an intentional no-op
//     (the object's own destructor runs naturally at DLL unload/process
//     exit). PREFERRED for the common case: it means this plugin never once
//     calls `new`/`delete` for its own module object at all, which is one
//     less thing to reason about under the plugin ABI's own shared-heap
//     caveat (nothing to free across the boundary because nothing was ever
//     allocated across it) - see docs/conventions/plugin-architecture.md's
//     "The shared/DLL CRT requirement" section for the full caveat this
//     sidesteps.
#define GTE_DEFINE_PLUGIN_EXPORTS(ModuleClass) \
    extern "C" { \
    __declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint() { return gte::MakeThisBuildsFingerprint(); } \
    __declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return new ModuleClass(); } \
    __declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module) { delete module; } \
    }

#define GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(instanceExpr) \
    extern "C" { \
    __declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint() { return gte::MakeThisBuildsFingerprint(); } \
    __declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return &(instanceExpr); } \
    __declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { /* static-instance flavor - nothing to free */ } \
    }
```

**Real correction versus the proposal document's own literal text (see
`PHASE0_MASTER_STRATEGY.md` Step 2 / "Decisions made without `ask_questions`" #2):**
the `#include` line for the generated fingerprint header above reads
`"gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"` (WITH the `gte_plugin_abi/`
prefix) — the proposal document's own sketch wrote a bare
`"GtePluginAbiFingerprintGenerated.h"`, which would fail to compile (that header
is a CMake `configure_file()` output living under
`<build-dir>/plugins/gte_plugin_abi/generated/gte_plugin_abi/
GtePluginAbiFingerprintGenerated.h`, resolved only through `gte_plugin_abi`'s own
`INTERFACE` include directory rooted at `.../generated`, not at
`.../generated/gte_plugin_abi`). Copy the exact spelling every existing plugin
`.cpp` already uses (e.g. `plugins/demo_hello_world/HelloWorldPlugin.cpp` line
19) — `search_in_dir` on `GtePluginAbiFingerprintGenerated.h` across `plugins/` to
see every existing correct call site before writing this file, to be certain.

### 3.2 — New file: `plugins/gte_plugin_abi/SingleCapabilityPluginModule.h`

Create this file with the proposal document's own Section "2.
`SingleCapabilityPluginModule.h`" content (`MakeModuleInfo()` +
`SingleCapabilityPluginModule<CapabilityInterface>`, copied verbatim — this part
of the proposal's own code is already correct and compiles as written), **PLUS**
one additional class this campaign's own Step 1 goal requires
(`ZeroCapabilityPluginModule`, decision #1 in `PHASE0_MASTER_STRATEGY.md`).

Full exact content:

```cpp
#pragma once

#include "GtePluginModuleInfo.h"
#include "IPluginModule.h"

#include <cstring>

namespace gte {

// Bounded, ALWAYS-null-terminated copy into a fixed GtePluginModuleInfo -
// removes the "std::strncpy(..., sizeof(field) - 1)" idiom every plugin
// author had to get right by hand (a real, live robustness gap closed at the
// SOURCE instead of relying on every future plugin author's own discipline -
// see editor-core-separation-4 campaign's PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md
// for the matching HOST-side defensive read, this is the PLUGIN-side
// defensive write half of the same concern).
inline GtePluginModuleInfo MakeModuleInfo(const char* name, const char* version, const char* description) noexcept
{
    GtePluginModuleInfo info{};
    auto copyBounded = [](char* dest, std::size_t destSize, const char* src) {
        std::size_t i = 0;
        for (; src[i] != '\0' && i + 1 < destSize; ++i) { dest[i] = src[i]; }
        dest[i] = '\0';
    };
    copyBounded(info.name, sizeof(info.name), name);
    copyBounded(info.version, sizeof(info.version), version);
    copyBounded(info.description, sizeof(info.description), description);
    return info;
}

// The reusable IPluginModule wrapper for the common "this plugin implements
// EXACTLY ONE capability" case (three of this repo's own four demo plugins,
// after this campaign - demo_render_feature, demo_render_feature_second,
// demo_editor_panel). A plugin that implements 2+ capabilities still
// hand-writes its own IPluginModule, exactly like today - this template does
// not replace that path, only removes the boilerplate for the simpler, more
// common one.
template <typename CapabilityInterface>
class SingleCapabilityPluginModule final : public IPluginModule {
public:
    SingleCapabilityPluginModule(CapabilityInterface& capability, const char* capabilityNameAndVersion, GtePluginModuleInfo info) noexcept
        : m_capability(capability)
        , m_capabilityNameAndVersion(capabilityNameAndVersion)
        , m_info(info)
    {
    }

    void* QueryCapability(const char* nameAndVersion) override
    {
        return (std::strcmp(nameAndVersion, m_capabilityNameAndVersion) == 0)
            ? static_cast<void*>(&m_capability)
            : nullptr;
    }

    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override { outInfo = m_info; }

private:
    CapabilityInterface& m_capability;
    const char* m_capabilityNameAndVersion;
    GtePluginModuleInfo m_info;
};

// editor-core-separation-5 campaign, PHASE1 - the zero-capability mirror of
// SingleCapabilityPluginModule<T> above, for a plugin that implements NO
// capability interface at all (demo_hello_world's own exact shape, migrated
// in PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md). Flagged by
// the source proposal document itself ("What about demo_hello_world (zero
// capabilities)?") as a natural follow-up for full consistency across every
// demo plugin - QueryCapability() unconditionally returns nullptr, never
// guesses, never partially implements a capability it wasn't given.
class ZeroCapabilityPluginModule final : public IPluginModule {
public:
    explicit ZeroCapabilityPluginModule(GtePluginModuleInfo info) noexcept : m_info(info) { }

    void* QueryCapability(const char*) override { return nullptr; }

    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override { outInfo = m_info; }

private:
    GtePluginModuleInfo m_info;
};

} // namespace gte
```

Neither new file references `std::string`/`std::vector`/anything outside
`gte_plugin_abi` — both stay fully compliant with `PublicSurface.md`'s own
boundary rules (Locked Design Decision #3). Both are pure, optional, additive
sugar around the existing, unchanged `IPluginModule` contract.

### 3.3 — Update `plugins/gte_plugin_abi/PublicSurface.md`

Add a new subsection right after the existing "Added by later phases" list
(after the PHASE4 `IEditorPanelModule_v1` line), e.g.:

```markdown
- editor-core-separation-5 campaign, PHASE1: `PluginExportsMacro.h`
  (`GTE_DEFINE_PLUGIN_EXPORTS`/`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`) and
  `SingleCapabilityPluginModule.h` (`MakeModuleInfo()`,
  `SingleCapabilityPluginModule<T>`, `ZeroCapabilityPluginModule`) - optional,
  additive authoring sugar around the existing IPluginModule/extern "C" contract
  above; zero ABI change, never required, never used by PluginHost itself
  (PluginHost only ever calls the three fixed extern "C" exports and
  IPluginModule's own two virtual methods, regardless of which flavor a given
  plugin .dll used to produce them).
```

Keep this addition short — do not restate the whole rationale here, this
document is a boundary-type LIST, the reasoning lives in this phase file and the
source proposal document.

### 3.4 — Do NOT touch anything else

Do not modify `GtePluginAbiFingerprint.h`, `GtePluginAbiFingerprintGenerated.h.in`,
`GtePluginModuleInfo.h`, `IPluginModule.h`, `IEditorPanelModule.h`,
`IPluginPanelDrawContext.h`, `IPluginRenderPassBuilder.h`, `IRenderFeatureModule.h`,
`PluginExports.h`, `CMakeLists.txt`, or any file under `plugins/demo_*/` or `src/`
in this phase.

## Verification (fast, no full build, no plugin `.dll` rebuild needed)

Since nothing yet `#include`s either new header, verification is a standalone
compile check proving (a) both files compile cleanly on their own, (b) neither
pulls in anything outside `gte_plugin_abi` (confirming `PublicSurface.md`'s own
"zero real `gte_core`/`gte_editor` header, ever" claim stays true), mirroring the
exact spirit of `PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md`'s own original compile
check from `editor-core-separation-3`:

1. Write a small, throwaway, NOT-committed `.cpp` **OUTSIDE this repository
   entirely** — e.g.
   `C:\Users\F5954\AppData\Local\Temp\gte_editor_core_separation_5_phase1_compile_check.cpp`
   (any OS temp-folder path is fine; the only hard requirement is that it sits
   outside `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\` so it can never
   show up in `git_status`/`git_add .` even if the final delete step is somehow
   skipped) — containing:
   ```cpp
   #include "IRenderFeatureModule.h"
   #include "PluginExportsMacro.h"
   #include "SingleCapabilityPluginModule.h"

   namespace {
   class TestFeature final : public gte::IRenderFeatureModule_v1 {
   public:
       void AddRenderGraphPasses(gte::IPluginRenderPassBuilder&) override { }
   };
   TestFeature g_feature;
   gte::SingleCapabilityPluginModule<gte::IRenderFeatureModule_v1> g_module(
       g_feature, gte::kIRenderFeatureModule_v1_Name,
       gte::MakeModuleInfo("Test", "1.0.0", "compile check only"));
   gte::ZeroCapabilityPluginModule g_zero(gte::MakeModuleInfo("Zero", "1.0.0", "compile check only"));
   }

   GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(g_module)
   ```
   (Note: `IRenderFeatureModule.h` is included only to get a real, existing
   capability interface to instantiate the template against — this is exactly
   what Phase 2 will do for real; this compile check just proves the mechanism
   before touching a real demo plugin.)
2. Compile it with the `gcc` tool (`use_gpp: true`), with the include path
   limited to EXACTLY `plugins/gte_plugin_abi` plus the already-generated
   `build/plugins/gte_plugin_abi/generated` folder (this folder already exists
   from the last real configure/build — confirm via `browse_dir` first; if
   missing, run `cmake --build build --target gte_plugin_abi` once to produce
   it, this is a no-op/near-instant since it is an `INTERFACE` library target).
   **Compile-only (`-c`), with `-Wall -Wextra` so "zero warnings" in step 3
   below is an actually-meaningful check, not a vacuous one** — pass this as
   ONE single-line `arguments` string to the `gcc` tool (shown here on
   multiple lines only for readability):
   ```
   -std=c++20 -Wall -Wextra -c
   -I "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\plugins\gte_plugin_abi"
   -I "C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build\plugins\gte_plugin_abi\generated"
   "<scratch_file>.cpp" -o "<scratch_file>.o"
   ```
   (Do NOT add `-shared`/`-o NUL` — this is a plain compile-only step, `-c`
   already means "stop after compiling, do not link", and a genuine shared-lib
   link is neither needed nor meaningful here; a plain object-file compile is
   enough to prove both headers parse and the template instantiates.)
3. Confirm zero errors, zero warnings referencing anything outside
   `gte_plugin_abi` (e.g. no accidental transitive include of a real
   `gte_core`/`gte_editor` header, no accidental `std::string` usage sneaking
   in).
4. Delete the scratch `.cpp`/`.o` before finishing (never commit it — it was
   only a throwaway compile probe, not a permanent test fixture; a REAL,
   permanent proof of these two headers in use is Phases 2-4's own migrated
   demo plugins).

## Completion

Write `PHASE1_COMPLETION_REPORT.md`: confirm both new files' exact final content,
the `PublicSurface.md` diff, the scratch compile-check command run and its exact
output, and an explicit note that no demo plugin `.cpp` was touched this phase
(that is Phases 2-4). Run `git_status` (`PHASE0_MASTER_STRATEGY.md` Workflow
Rule 10) and confirm the diff about to be staged is EXACTLY the two new headers
plus `PublicSurface.md` — nothing else, no leftover scratch file. Then `git_add`
+ `git_commit` (message referencing PHASE1).
