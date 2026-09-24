# PHASE4 — Migrate `demo_hello_world` onto `ZeroCapabilityPluginModule` (full symmetry)

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first, including "Decisions
made without `ask_questions`" #1 and #3 (both apply directly to this phase).
Also read `PHASE1`/`PHASE2`/`PHASE3`'s own `PHASEn_COMPLETION_REPORT.md` files
before starting.

**Use `ask_questions` if anything below is ambiguous or looks wrong once you have
the real files open — do not silently guess.**

---

## Step 1: The Goal

Rewrite `plugins/demo_hello_world/HelloWorldPlugin.cpp` to use Phase 1's
`ZeroCapabilityPluginModule` + `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`,
completing this campaign's own goal of migrating ALL FOUR existing demo plugins
onto the new authoring sugar (not just the two the source proposal document's
own before/after examples happened to show). Same byte-identical-behavior
requirement as every prior phase: `GtePluginModuleInfo` fields must read
identically, and `QueryCapability()` must still return `nullptr` unconditionally
for every capability name (this plugin implements zero capabilities — that is
its whole point, proving the handshake works even for a plugin with nothing to
offer).

## Step 2: The Situation

Current `plugins/demo_hello_world/HelloWorldPlugin.cpp` (57 lines) defines
`HelloWorldPluginModule : public IPluginModule` directly (no separate capability
class exists at all — `QueryCapability(const char*)` is already a one-line
`return nullptr;`), with `GetModuleInfo()` filling `"HelloWorldPlugin"` /
`"1.0.0"` /
`"Milestone 0 handshake proof - implements zero capabilities."` via 3
`std::strncpy` calls, and the verbatim `extern "C"` export block using the
HEAP-ALLOCATED flavor (`new gte::HelloWorldPluginModule()` /
`delete module`) — this is the ONE demo plugin among the four that does NOT
already use the static-instance pattern Phases 2-3 migrated their targets onto.

This plugin is the one `tools/ci/gte_plugin_abi_handshake_probe/main.cpp` loads
directly via raw `LoadLibraryW`/`GetProcAddress` (bypassing `PluginHost`
entirely) — that probe's own `main.cpp` prints
`"OK: loaded plugin '%s' v%s - %s\n"` using `info.name`/`info.version`/
`info.description` read straight off the returned `IPluginModule*` — this exact
printed line must not change.

## Step 3: The Plan

### 3.1 — Rewrite `plugins/demo_hello_world/HelloWorldPlugin.cpp`

Replace the ENTIRE file content with:

```cpp
// plugins/demo_hello_world/HelloWorldPlugin.cpp
//
// PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.5 - Milestone
// 0's own handshake proof: implements zero capabilities, nothing else. This
// plugin is deliberately tiny/throwaway-quality - it exists to prove the ABI
// boundary works, not to demonstrate a real feature.
//
// editor-core-separation-5 campaign, PHASE4
// (PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md) - migrated onto
// ZeroCapabilityPluginModule + GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE (see
// PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md's own "What about
// demo_hello_world (zero capabilities)?" section), completing this campaign's
// goal of moving all four demo plugins onto the same authoring pattern. This
// ALSO switches this ONE plugin from the heap-allocated
// (new/delete-per-load) flavor to the static-instance flavor every other demo
// plugin already used before this campaign - see
// PHASE0_MASTER_STRATEGY.md's "Decisions made without ask_questions" #3 for
// why: this plugin never had any real per-instance construction logic to
// begin with, so nothing is lost, and it makes all four demo plugins follow
// one single, consistent pattern. Every string literal below (module
// name/version/description) is byte-for-byte identical to this file's
// pre-migration content.

#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

ZeroCapabilityPluginModule g_module(
    MakeModuleInfo("HelloWorldPlugin", "1.0.0", "Milestone 0 handshake proof - implements zero capabilities."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

Note this file no longer needs `#include "../gte_plugin_abi/IPluginModule.h"`,
`#include "../gte_plugin_abi/GtePluginModuleInfo.h"`,
`#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`, or
`#include <cstring>` directly — `SingleCapabilityPluginModule.h` and
`PluginExportsMacro.h` already transitively pull in everything needed
(`IPluginModule.h`, `GtePluginModuleInfo.h`, `GtePluginAbiFingerprint.h`, the
generated fingerprint header, and `<cstring>`). Confirm this compiles cleanly
in the Verification step below rather than assuming it — if it does not, add
back only whichever single include is genuinely missing, do not add all four
back defensively.

### 3.2 — Do not touch `plugins/demo_hello_world/CMakeLists.txt`

Confirm it still only needs its existing source list, `gte_plugin_abi` link,
`RUNTIME_OUTPUT_DIRECTORY`/`PREFIX ""`, and whichever
`gte_apply_plugin_*_shared_crt_linkage(...)` call it already has — no change.

Run `git_status` before Step 3.1 (`PHASE0_MASTER_STRATEGY.md` Workflow Rule
10) — confirm the branch still reads `feature/editor-core-separation` and the
tree is clean or contains only Phase 1/2/3's already-committed diff.

## Verification (both standalone probes)

1. **Handshake probe** (this is the one that directly exercises this exact
   plugin): `cmake --build build-plugin-abi-handshake-probe\
   gte_plugin_abi_inner_build`, then run
   `build-plugin-abi-handshake-probe\gte_plugin_abi_inner_build\
   gte_plugin_abi_handshake_probe.exe` directly. Confirm exit code `0` and
   stdout reads exactly
   `OK: loaded plugin 'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof - implements zero capabilities.`
   — byte-identical to this probe's pre-migration output (this is the single
   most important check this phase has: it proves the static-instance flavor's
   `GTE_CreatePluginModule()` returning `&g_module` behaves identically to the
   old flavor's `new HelloWorldPluginModule()` from this probe's own external,
   black-box point of view).
2. **Isolation probe**: `cmake --build build-plugin-isolation-probe\
   gte_plugin_isolation_inner_build`, then run
   `gte_plugin_isolation_probe.exe`. Confirm `LoadedModuleCount() == 4` still
   holds (this plugin must still load — a static-instance module returned from
   a DLL-scope global object is exactly as valid an `IPluginModule*` as a
   heap-allocated one) and `renderFeatureCount == 2` still holds (unaffected —
   this plugin never implemented `IRenderFeatureModule_v1`), and confirm the
   `Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no` line still prints.
3. Also do a targeted incremental rebuild of the main `build/` tree
   (`cmake --build build --target demo_hello_world`) so `build/plugins/
   demo_hello_world.dll` is current for Phase 5's own final full regression.

## Completion

Write `PHASE4_COMPLETION_REPORT.md`: the rewritten file's exact final content,
both probes' exact stdout, explicit confirmation the handshake probe's printed
line is byte-identical to the pre-migration baseline, and the line-count
reduction achieved (57→~12, roughly — the largest relative reduction of any of
the four demo plugins, since this one had no capability class to keep at all).
Also note explicitly: **all four demo plugins now follow the exact same
authoring pattern** (this campaign's own Step 1 goal, now fully achieved). Run
`git_status` (Workflow Rule 10) and confirm the diff about to be staged is
EXACTLY the one rewritten `.cpp` file (plus the `PHASE4_COMPLETION_REPORT.md`
you are about to add) — nothing else. Then `git_add` + `git_commit` (message
referencing PHASE4).
