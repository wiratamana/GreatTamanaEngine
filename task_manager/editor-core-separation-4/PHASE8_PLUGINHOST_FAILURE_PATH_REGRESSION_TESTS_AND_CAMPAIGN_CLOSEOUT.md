# PHASE8 — `PluginHost` Failure-Path Regression Tests + Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE7`'s own `COMPLETION_REPORT.md` files — this is the LAST phase
of this campaign, and it needs the full, accurate picture of everything the
previous 7 phases actually did (including any real deviations they recorded)
to write an honest closeout. If any of those files do not exist yet at the
point you start this phase, that means those phases have not actually been
implemented yet either — stop and confirm with `ask_questions` rather than
guessing at what they did; this file's own Step 4 (campaign closeout) is not
meaningful until every earlier phase has really landed.

**Severity:** LOW (Issue #8 of the re-analysis document) **plus this
campaign's own mandatory final full regression verification and closeout.**

**Source of truth for Issue #8:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "8.
LOW — no automated regression test exists for `PluginHost`'s own failure
paths".

---

## Step 1: The Goal

Two things, in this order:

1. **Fix Issue #8**: add real, automated GoogleTest coverage for all 4 of
   `PluginHost`'s own documented failure/skip paths — today, every existing
   probe/test only ever exercises the HAPPY path (well-formed demo plugins
   loading successfully). The 4 unprotected paths are: (a) a `.dll` missing
   one of the 3 required exports, (b) a `.dll` whose fingerprint mismatches
   the host's, (c) a `.dll` whose `GTE_CreatePluginModule()` returns
   `nullptr` (a legal "I decline to load" signal), and (d) the
   reverse-load-order destroy sequence on `PluginHost` teardown.
2. **Close out this whole campaign**: this is the ONE phase in this campaign
   allowed to run a full clean build and a full `ctest` regression pass (see
   `PHASE0_MASTER_STRATEGY.md`'s own Workflow Rule 1) — do this AFTER Issue #8
   is implemented and its own narrow compile check passes, then write
   `CAMPAIGN_COMPLETION_REPORT.md`, mirroring
   `task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`'s own
   shape/tone exactly (that file is a good model to re-read before writing
   this campaign's own version).

## Step 2: The Situation

`PluginHost::TryLoadOnePlugin()`'s 4 real, distinct failure/decline paths (all
in `src/Core/Plugins/PluginHost.cpp`, already read in full during earlier
phases of this campaign — re-read it fresh now, it may have changed across
Phases 1/4/6/7):

1. Missing export (any of the 3: `GTE_GetPluginAbiFingerprint`,
   `GTE_CreatePluginModule`, `GTE_DestroyPluginModule`) → `GTE_LOG_WARNING` +
   `FreeLibrary()` + return, ZERO modules loaded from that file. The logged
   message is `<full .dll path> + " is missing export " + <export name> +
   ", not a valid plugin, skipping"`, category `"PluginHost"`.
2. Fingerprint mismatch → `GTE_LOG_WARNING` (with per-field diff) +
   `FreeLibrary()` + return. The logged message is `<full .dll path> + ":
   ABI fingerprint mismatch - " + <field diff>`, category `"PluginHost"`.
3. `GTE_CreatePluginModule()` returns `nullptr` → `GTE_LOG_INFO` (legal
   decline, not a warning) + `FreeLibrary()` + return. The logged message is
   `<full .dll path> + "'s GTE_CreatePluginModule() returned nullptr, plugin
   declined to load"`, category `"PluginHost"`.
4. `~PluginHost()` destroys every successfully-loaded module in REVERSE load
   order (last-loaded, first-destroyed), calling each one's own `destroyFn`
   BEFORE `FreeLibrary()`.

`search_in_dir` across `tests/` for `PluginHost` (already confirmed during
this campaign's own planning) finds **zero** matches — this class has never
been directly unit-tested, only exercised transitively through real demo
plugins in probes/the live Editor. `tests/Core/` has no `Plugins/`
sub-folder yet either (confirmed via `browse_dir` during this campaign's own
planning) — this phase is the one that creates it.

## Step 3: The Plan

### 3.1 New fixture-plugin fixtures — 4 deliberately-broken/instrumented tiny
`.dll` targets, isolated from the real `plugins/` scan folder

Create a new folder, `tests/Fixtures/FakePlugins/`, with its OWN
`CMakeLists.txt` (added to `tests/CMakeLists.txt` via `add_subdirectory(
Fixtures/FakePlugins)`, guarded by the SAME `if(GTE_BUILD_TESTS)` scope
`tests/CMakeLists.txt` itself is already only reached from). Each fixture is
its own tiny `add_library(... SHARED ...)` target, `PREFIX ""`, with its own
dedicated `RUNTIME_OUTPUT_DIRECTORY` — **NOT** `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`
(the real, shared `plugins/` folder the real `PluginHost`/Editor scans) —
instead a brand-new, test-only directory, e.g.
`"${CMAKE_BINARY_DIR}/test_fixtures/fake_plugins"`, so these deliberately
broken `.dll`s can NEVER be accidentally picked up by the real Editor's own
plugin scan.

Bake that absolute directory path into a small generated header, mirroring
`GtePluginAbiFingerprintGenerated.h.in`'s own `configure_file()` precedent
exactly:

```cmake
# tests/Fixtures/FakePlugins/CMakeLists.txt
set(GTE_FAKE_PLUGIN_FIXTURES_DIR "${CMAKE_BINARY_DIR}/test_fixtures/fake_plugins")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/FakePluginFixturesDirGenerated.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/FakePluginFixturesDirGenerated.h"
    @ONLY
)

# CRITICAL WIRING STEP - easy to miss, spelled out explicitly here: the
# configure_file() call above writes into THIS subdirectory's OWN
# CMAKE_CURRENT_BINARY_DIR (tests/Fixtures/FakePlugins's binary dir), which
# is a COMPLETELY DIFFERENT directory than gte_plugin_abi's own generated/
# folder (plugins/gte_plugin_abi's binary dir) - linking a fixture target
# against gte_plugin_abi does NOT expose THIS folder's own generated header
# to anything. Without this small INTERFACE library,
# tests/Core/Plugins/PluginHostFailurePathTests.cpp's own
# `#include "generated/FakePluginFixturesDirGenerated.h"` (Section 3.3 below)
# fails to compile ("No such file or directory") the moment
# GreatTamanaEngineTests is built, since nothing ever added this directory to
# that target's own include path. Mirrors gte_plugin_abi/CMakeLists.txt's own
# `target_include_directories(gte_plugin_abi INTERFACE
# "${CMAKE_CURRENT_BINARY_DIR}/generated")` precedent exactly, just as its own
# tiny, dedicated INTERFACE library instead of piggy-backing on an existing
# one (this fixture's own generated header has nothing to do with the plugin
# ABI itself, so it earns its own small target rather than overloading
# gte_plugin_abi's).
add_library(gte_fake_plugin_fixtures_dirs INTERFACE)
target_include_directories(gte_fake_plugin_fixtures_dirs INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

function(add_fake_plugin_fixture target_name)
    add_library(${target_name} SHARED ${ARGN})
    target_link_libraries(${target_name} PRIVATE gte_plugin_abi)
    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
    )
    # Folded in HERE (not left as a separate per-target reminder below) so
    # none of the 5 calls to this function below can accidentally forget it -
    # every fixture .dll is a plugin-ABI-boundary .dll exactly like a real
    # demo plugin, so it must call the SAME CRT-linkage helper every demo
    # plugin CMakeLists.txt calls (Phase 4's gte_apply_plugin_dll_shared_crt_linkage()
    # - by the time this phase runs, Phase 4 has already landed per this
    # campaign's own sequential phase order, so there is no "if it landed"
    # ambiguity left to hedge about here).
    gte_apply_plugin_dll_shared_crt_linkage(${target_name})
endfunction()

add_fake_plugin_fixture(fake_plugin_missing_export FakePluginMissingExport.cpp)
set_target_properties(fake_plugin_missing_export PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/unhappy_path")
add_fake_plugin_fixture(fake_plugin_bad_fingerprint FakePluginBadFingerprint.cpp)
set_target_properties(fake_plugin_bad_fingerprint PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/unhappy_path")
add_fake_plugin_fixture(fake_plugin_declines_to_load FakePluginDeclinesToLoad.cpp)
set_target_properties(fake_plugin_declines_to_load PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/unhappy_path")
add_fake_plugin_fixture(fake_plugin_destroy_order_a FakePluginDestroyOrderA.cpp)
set_target_properties(fake_plugin_destroy_order_a PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/destroy_order")
add_fake_plugin_fixture(fake_plugin_destroy_order_b FakePluginDestroyOrderB.cpp)
set_target_properties(fake_plugin_destroy_order_b PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/destroy_order")
```

(The exact split between `add_fake_plugin_fixture()`'s shared body and each
target's own `RUNTIME_OUTPUT_DIRECTORY` override above is one reasonable
shape — feel free to fold the per-target `RUNTIME_OUTPUT_DIRECTORY` into the
function itself via a second parameter if that reads cleaner once you're
actually editing the file; the two-sub-folder split itself, Section 3.2
below, is the load-bearing part, not this exact function signature.)

`FakePluginFixturesDirGenerated.h.in`:
```cpp
#pragma once
namespace gte::test {
inline constexpr const char* kFakePluginUnhappyPathFixturesDir = "@GTE_FAKE_PLUGIN_FIXTURES_DIR@/unhappy_path";
inline constexpr const char* kFakePluginDestroyOrderFixturesDir = "@GTE_FAKE_PLUGIN_FIXTURES_DIR@/destroy_order";
} // namespace gte::test
```

**On the backslash-escaping concern for the substituted path**: this is
**already safe, with no extra defensive code needed** — `CMAKE_BINARY_DIR`
(and every other built-in CMake path variable: `CMAKE_SOURCE_DIR`,
`CMAKE_CURRENT_SOURCE_DIR`, `CMAKE_CURRENT_BINARY_DIR`, ...) is documented by
CMake itself to always use forward slashes (`/`) internally, on every
platform including Windows — CMake only ever converts to native
(backslash) separators when a script explicitly asks it to, e.g. via
`file(TO_NATIVE_PATH ...)`, which nothing here calls.
`GTE_FAKE_PLUGIN_FIXTURES_DIR` above is built purely by string-concatenating
`CMAKE_BINARY_DIR` with a literal, hardcoded, forward-slash-separated suffix
(`"/test_fixtures/fake_plugins"`) — there is no step anywhere in this chain
that could introduce a backslash. The substituted value is therefore
guaranteed to be a plain forward-slash path (e.g.
`C:/Users/F5954/.../GreatTamanaEngine/build/test_fixtures/fake_plugins`),
which is valid, unescaped C++ string-literal content as-is (forward slashes
need no escaping) AND a perfectly valid path for `std::filesystem::path`/the
real Win32 `LoadLibraryW()`/`std::filesystem::directory_iterator` call sites
that will read it (Windows APIs universally accept `/` as a path separator
too). No `string(REPLACE "\\" "/" ...)` guard is needed — do not add one, it
would be defending against a failure mode that cannot occur here.

**`FakePluginMissingExport.cpp`** — exports ONLY
`GTE_GetPluginAbiFingerprint` and `GTE_DestroyPluginModule`, deliberately
OMITTING `GTE_CreatePluginModule` (proves the "missing export" skip path for
ANY of the 3 — omitting exactly one is sufficient and simpler to reason about
than omitting all three). **Include the generated fingerprint header via its
`gte_plugin_abi/` INTERFACE include path, exactly like every real demo
plugin already does — NOT via a literal `../../../plugins/gte_plugin_abi/`
relative path**: `GtePluginAbiFingerprintGenerated.h` is a
`configure_file()` OUTPUT living under
`<build-dir>/generated/gte_plugin_abi/`, never a checked-in file under
`plugins/gte_plugin_abi/` in the source tree at all — a literal relative
path to it there will fail to compile (`No such file or directory`). This is
the exact same mistake `PHASE1_COMPLETION_REPORT.md`/`PHASE2_COMPLETION_REPORT.md`
of the prior campaign already discovered and fixed once (see
`plugins/demo_hello_world/HelloWorldPlugin.cpp`'s own header comment on this
exact point) — do not reintroduce it here. Since
`add_fake_plugin_fixture()` above already links every fixture target
`PRIVATE gte_plugin_abi`, that target's own INTERFACE include directories
(both the plain checked-in `plugins/gte_plugin_abi/` headers AND
`gte_plugin_abi`'s OWN `${CMAKE_CURRENT_BINARY_DIR}/generated` — i.e.
`plugins/gte_plugin_abi/`'s build-tree location, holding
`GtePluginAbiFingerprintGenerated.h` — a COMPLETELY SEPARATE folder from the
`gte_fake_plugin_fixtures_dirs` INTERFACE library's own generated dir a few
lines above, which instead holds `FakePluginFixturesDirGenerated.h` — do not
confuse the two, each fixes access to its own distinct generated header)
are already available — the correct
include for the fingerprint header is simply
`#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`:
```cpp
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}
// GTE_CreatePluginModule deliberately NOT exported.
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { }
}
```
(`../../../plugins/gte_plugin_abi/IPluginModule.h` — 3 levels up from
`tests/Fixtures/FakePlugins/` reaches the repo root, then down into
`plugins/gte_plugin_abi/` — this one IS a real, checked-in header at that
literal path, unlike the generated fingerprint header above; only the
GENERATED header needs the INTERFACE-include-path spelling.)

**`FakePluginBadFingerprint.cpp`** — exports all 3, but
`GTE_GetPluginAbiFingerprint()` returns a deliberately corrupted fingerprint
(a real one with one field intentionally wrong, so the mismatch is
unambiguous and realistic):
```cpp
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

namespace {
class FakeModule final : public gte::IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(gte::GtePluginModuleInfo&) const override { }
};
} // namespace

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    gte::GtePluginAbiFingerprint fp = gte::MakeThisBuildsFingerprint();
    fp.abiContractGeneration = 999999; // deliberately, unambiguously wrong
    return fp;
}
__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return new FakeModule(); }
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module) { delete module; }
}
```
(`FakeModule`/`GTE_CreatePluginModule()` here are never actually reached at
runtime — `PluginHost::TryLoadOnePlugin()`'s fingerprint check runs BEFORE
`GTE_CreatePluginModule()` is ever called, so this fixture's own
create/destroy pair only needs to compile correctly, matching the real ABI
contract's required export signatures; it is never expected to execute.)

**`FakePluginDeclinesToLoad.cpp`** — exports all 3, correct fingerprint, but
`GTE_CreatePluginModule()` always returns `nullptr`:
```cpp
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}
__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return nullptr; }
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { }
}
```

**`FakePluginDestroyOrderA.cpp`** / **`FakePluginDestroyOrderB.cpp`** — both
correct, both load successfully, and BOTH append a plain-text line to a
well-known temp marker file on EVERY `GTE_CreatePluginModule()` and
`GTE_DestroyPluginModule()` call — this is how the test proves REVERSE load
order without depending on `std::filesystem::directory_iterator`'s own
(unspecified-by-the-standard) enumeration order:

```cpp
// FakePluginDestroyOrderA.cpp (FakePluginDestroyOrderB.cpp is IDENTICAL
// except every "A" below becomes "B" - keep them as two separate, real
// files, not a templated/shared one, mirroring this codebase's own existing
// demo-plugin-per-folder convention).
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <cstdio>
#include <cstdlib>

namespace {

void AppendMarker(const char* line)
{
    // GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE is set by the TEST process
    // (_putenv() - see the test file below for exactly why _putenv() and
    // not _putenv_s()) BEFORE it constructs the PluginHost that loads this
    // fixture, and BEFORE that call reaches LoadLibraryW() on either
    // fixture .dll - a plain OS environment variable is the simplest way to
    // hand this fixture a file path without growing the real gte_plugin_abi
    // ABI surface just for a test fixture's own internal bookkeeping (this
    // file is NEVER loaded by the real, production PluginHost/Editor - it
    // lives only under tests/Fixtures/). This works correctly across the
    // .dll boundary specifically BECAUSE of that ordering: _putenv() writes
    // through to the one real, process-wide Win32 environment block (not a
    // private per-module cache), and each fixture .dll's own statically-
    // linked CRT populates its own getenv()-backing environ array by
    // reading that SAME process-wide block at its own LoadLibraryW() /
    // DllMain(DLL_PROCESS_ATTACH) time - which only happens once
    // PluginHost::LoadPlugins() actually loads this .dll, strictly AFTER
    // the test process's own _putenv() call already ran. Never reorder the
    // test's own _putenv()-then-LoadPlugins() sequence - reversing it would
    // silently break this technique.
    const char* path = std::getenv("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE");
    if (path == nullptr) {
        return;
    }
    if (FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s\n", line);
        std::fclose(f);
    }
}

class FakeModuleA final : public gte::IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(gte::GtePluginModuleInfo&) const override { }
};

} // namespace

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}
__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule()
{
    AppendMarker("CREATE:A");
    return new FakeModuleA();
}
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    AppendMarker("DESTROY:A");
    delete module;
}
}
```

Each of these 5 fixture targets already gets `gte_apply_plugin_dll_shared_crt_linkage()`
called on it for free via `add_fake_plugin_fixture()` itself (Section 3.1
above folds this call into the shared function body, precisely so none of
the 5 individual `add_fake_plugin_fixture(...)` call sites below it need to
remember to add it themselves) — no further action needed here.

### 3.2 `PluginHost` needs the 3 "bad" fixtures in ONE folder, the 2 "destroy
order" fixtures in a SEPARATE folder

Two distinct scenarios, two distinct dedicated sub-folders under
`${GTE_FAKE_PLUGIN_FIXTURES_DIR}` (`unhappy_path` for the first 3,
`destroy_order` for the last 2, matched exactly by the two
`RUNTIME_OUTPUT_DIRECTORY` overrides in Section 3.1's own CMake sketch above)
— this keeps the "unhappy path" test's `LoadedModuleCount() == 0` assertion
honest (it must not also see the 2 genuinely-successful destroy-order
fixtures sitting in the same folder), and keeps the "destroy order" test's
own folder free of any fixture that fails to load (which would make
`LoadedModuleCount()` not equal exactly 2 there). This is exactly why the
generated header above exposes two distinct constants,
`kFakePluginUnhappyPathFixturesDir` and `kFakePluginDestroyOrderFixturesDir`,
rather than one shared directory constant.

### 3.3 The 4 new GoogleTest cases

Create `tests/Core/Plugins/PluginHostFailurePathTests.cpp` (`browse_dir` on
`tests/Core/` during this campaign's own planning already confirmed there is
no existing `Plugins/` sub-folder there yet — this phase is the one that
creates it, mirroring `tests/Core/EditorPanelRegistryTests.cpp`'s own
existing flat-file-under-`Core/`-plus-one-nested-folder-when-warranted
convention).

```cpp
#include "Core/Plugins/PluginHost.h"
#include "Core/Logging.h"
#include "Editor/Logger.h" // Logger::Query()/LatestEntryId() - see this file's own header comment for why direct log inspection is used, not just an aggregate count.
#include "generated/FakePluginFixturesDirGenerated.h" // exact generated include path - verify once you see the real CMake output layout

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gte {
namespace {

// Returns true if ANY log entry recorded since `sinceId`, category
// "PluginHost", contains `keyword` (case-sensitive substring match) in its
// message. Used below to prove each of the 3 "unhappy path" fixtures failed
// via ITS OWN intended, distinct mechanism - not merely that the aggregate
// LoadedModuleCount() ended up zero, which would equally pass even if, say,
// FakePluginBadFingerprint.dll accidentally tripped the "missing export"
// path instead of the "fingerprint mismatch" path it's meant to prove.
bool AnyLogMessageContains(std::uint64_t sinceId, const std::string& keyword)
{
    LogQueryFilter filter;
    filter.sinceId = sinceId;
    filter.category = "PluginHost";
    for (const LogEntry& entry : Logger::Query(filter)) {
        if (entry.message.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST(PluginHostFailurePathTest, LoadPlugins_UnhappyPathFolder_LoadsExactlyZeroModulesAndEachFixtureFailsForItsOwnDistinctReason)
{
    // FakePluginMissingExport.dll / FakePluginBadFingerprint.dll /
    // FakePluginDeclinesToLoad.dll each fail a DIFFERENT one of PluginHost's
    // own 3 load-time checks (missing export / fingerprint mismatch /
    // decline-to-load) - none of the three may ever produce a loaded
    // module, and none of them may ever crash the host.
    const std::uint64_t baselineId = Logger::LatestEntryId();

    PluginHost host;
    host.LoadPlugins(test::kFakePluginUnhappyPathFixturesDir);
    EXPECT_EQ(host.LoadedModuleCount(), 0u);

    // Each fixture's own real, current .dll filename (fake_plugin_missing_export.dll,
    // etc. - PREFIX "" + the CMake target name, see Section 3.1's own
    // add_fake_plugin_fixture() calls) is part of PluginHost's own logged
    // path prefix on every one of its 3 checked messages, so matching on it
    // ties each log line back to the specific fixture that produced it.
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_missing_export"))
        << "expected a log line naming fake_plugin_missing_export.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "is missing export"))
        << "expected the missing-export skip message";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_bad_fingerprint"))
        << "expected a log line naming fake_plugin_bad_fingerprint.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "ABI fingerprint mismatch"))
        << "expected the fingerprint-mismatch skip message";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_declines_to_load"))
        << "expected a log line naming fake_plugin_declines_to_load.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "declined to load"))
        << "expected the decline-to-load message";
}

TEST(PluginHostFailurePathTest, LoadPlugins_DestroyOrderFolder_LoadsExactlyTwoModules)
{
    PluginHost host;
    host.LoadPlugins(test::kFakePluginDestroyOrderFixturesDir);
    EXPECT_EQ(host.LoadedModuleCount(), 2u);
}

TEST(PluginHostFailurePathTest, Destructor_DestroysLoadedModulesInExactReverseOfTheirCreateOrder)
{
    const std::filesystem::path markerPath =
        std::filesystem::temp_directory_path() / "gte_plugin_destroy_order_test_marker.txt";
    std::filesystem::remove(markerPath); // start from a clean slate, in case a prior run left it behind

    // _putenv() (single "NAME=value" string form), NOT _putenv_s() - this
    // exact toolchain already has a real, mechanically-confirmed precedent
    // for this exact call on GTEST_OS_WINDOWS:
    // third_party/googletest/googletest/test/gtest_unittest.cc's own
    // internal SetEnv() helper (used by GoogleTest's own flag-saver tests,
    // which this exact fetched copy of GoogleTest already builds and runs
    // successfully as part of this project) uses
    // `_putenv((Message() << name << "=" << value).GetString().c_str())`
    // on GTEST_OS_WINDOWS specifically - there is no need to guess between
    // _putenv/_putenv_s/setenv here, this is the already-proven-working
    // choice on this exact repository's own toolchain.
    const std::string setMarkerEnv = std::string("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE=") + markerPath.string();
    _putenv(setMarkerEnv.c_str());

    {
        PluginHost host; // scoped - its destructor runs at the closing brace below
        host.LoadPlugins(test::kFakePluginDestroyOrderFixturesDir);
        ASSERT_EQ(host.LoadedModuleCount(), 2u);
    } // ~PluginHost() runs here - both fixtures' GTE_DestroyPluginModule() fire, in reverse load order

    _putenv("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE="); // unset (empty value) for any later test in this same process

    std::ifstream markerFile(markerPath);
    ASSERT_TRUE(markerFile.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(markerFile, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    markerFile.close();
    std::filesystem::remove(markerPath); // clean up after ourselves

    // Extract CREATE order and DESTROY order independently from the marker
    // file, rather than assuming std::filesystem::directory_iterator's own
    // (unspecified by the standard) enumeration order - this test only
    // asserts the real, load-bearing invariant PluginHost promises: destroy
    // order is the EXACT REVERSE of create order, whatever that real create
    // order turned out to be.
    std::vector<std::string> createOrder;
    std::vector<std::string> destroyOrder;
    for (const std::string& entry : lines) {
        if (entry.rfind("CREATE:", 0) == 0) {
            createOrder.push_back(entry.substr(7));
        } else if (entry.rfind("DESTROY:", 0) == 0) {
            destroyOrder.push_back(entry.substr(8));
        }
    }

    ASSERT_EQ(createOrder.size(), 2u);
    ASSERT_EQ(destroyOrder.size(), 2u);
    std::vector<std::string> expectedDestroyOrder(createOrder.rbegin(), createOrder.rend());
    EXPECT_EQ(destroyOrder, expectedDestroyOrder);
}

} // namespace
} // namespace gte
```

`_putenv()` is declared in `<cstdlib>` on this toolchain (MinGW) - no extra
include needed beyond what's already listed above. If, when you actually
compile this, `_putenv` genuinely isn't available under that exact spelling
on the real installed toolchain (unexpected given the `third_party/googletest`
precedent above, but this repo's own rule is "real source may have drifted,
always re-check, never assume") try `_putenv_s(name, value)` next (also
`<cstdlib>`, two-argument form, no manual `"NAME=value"` concatenation
needed) — and only reach for `ask_questions` if NEITHER compiles, which
would itself be a surprising enough finding to flag before guessing further.

Add `tests/Core/Plugins/PluginHostFailurePathTests.cpp` to `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list. Wiring order in `tests/CMakeLists.txt` matters here
and must mirror the root `CMakeLists.txt`'s own already-working
`gte_plugin_isolation_probe`/`gte_core_player_link_probe` precedent exactly
(`add_subdirectory()` that DEFINES the dependency targets always comes
BEFORE the `add_executable()`/`add_dependencies()` pair that references
them, never after):

1. Add `add_subdirectory(Fixtures/FakePlugins)` near the TOP of
   `tests/CMakeLists.txt`, before the `GTE_TEST_SOURCES` list / before
   `add_executable(GreatTamanaEngineTests ...)` — this defines all 5 fixture
   targets (`fake_plugin_missing_export`, `fake_plugin_bad_fingerprint`,
   `fake_plugin_declines_to_load`, `fake_plugin_destroy_order_a`,
   `fake_plugin_destroy_order_b`) before anything downstream needs them.
2. Immediately AFTER the existing `target_link_libraries(GreatTamanaEngineTests
   PRIVATE imgui)` call (i.e. after every existing `target_link_libraries()`
   call for this target, right before `sdl3_copy_runtime_dll(GreatTamanaEngineTests)`),
   add BOTH of the following (the first is the actual fix for the
   `#include "generated/FakePluginFixturesDirGenerated.h"` compile error
   Section 3.1 above's own `gte_fake_plugin_fixtures_dirs` INTERFACE library
   exists to prevent — do not skip it, `PluginHostFailurePathTests.cpp` will
   not compile without it):
   ```cmake
   # Section 3.1's own gte_fake_plugin_fixtures_dirs INTERFACE library is what
   # actually lets PluginHostFailurePathTests.cpp's
   # #include "generated/FakePluginFixturesDirGenerated.h" resolve - without
   # this line, that #include fails to compile with "No such file or
   # directory" the moment this target is built.
   target_link_libraries(GreatTamanaEngineTests PRIVATE gte_fake_plugin_fixtures_dirs)

   # PHASE8_PLUGINHOST_FAILURE_PATH_REGRESSION_TESTS_AND_CAMPAIGN_CLOSEOUT.md -
   # PluginHostFailurePathTests.cpp LoadLibraryW()s these 5 fixture .dlls at
   # runtime rather than linking them - CMake has no way to know that
   # dependency on its own (same reasoning as every demo-plugin
   # add_dependencies() call already in root CMakeLists.txt).
   add_dependencies(GreatTamanaEngineTests
       fake_plugin_missing_export
       fake_plugin_bad_fingerprint
       fake_plugin_declines_to_load
       fake_plugin_destroy_order_a
       fake_plugin_destroy_order_b
   )
   ```

### 3.4 Update `docs/conventions/plugin-architecture.md`

Add one short paragraph: "`PluginHost`'s 4 documented failure/skip paths
(missing export, fingerprint mismatch, decline-to-load, reverse-order
destroy) are covered by `tests/Core/Plugins/PluginHostFailurePathTests.cpp`,
using deliberately-broken fixture `.dll`s under `tests/Fixtures/FakePlugins/`
— never the real, production demo plugins."

## Step 4: Campaign closeout (do this AFTER 3.1-3.4's own narrow compile
check passes)

This is the ONE phase allowed to do the following (per `PHASE0_MASTER_STRATEGY.md`'s
Workflow Rule 1):

1. **Full clean rebuild**: delete `build/` (`rd /s /q build` via `run_shell` —
   if it fails once, retry once before treating it as a real problem, mirroring
   the prior campaign's own documented, harmless one-off retry precedent),
   then `cmake -S . -B build -G Ninja` + `cmake --build build`. Confirm zero
   errors. Confirm (via `browse_dir` on `build/plugins/`) all 4 demo plugin
   `.dll`s are present (`demo_hello_world.dll`, `demo_render_feature.dll`,
   `demo_render_feature_second.dll` from Phase 5, `demo_editor_panel.dll`) —
   the 5 new fixture `.dll`s from this phase must NOT appear there (confirm
   via `browse_dir` on `build/test_fixtures/fake_plugins/unhappy_path` and
   `.../destroy_order` instead, proving the isolation from the real scan
   folder actually holds in the real, built output, not just on paper).
2. **Full `ctest -C Debug --output-on-failure`** (working directory
   `build/`). Compare the total/passed/skipped count against
   `editor-core-separation-3`'s own final baseline (1776 total, 1774 passed, 2
   legitimate environment-gated skips, zero failures — see that campaign's
   own `CAMPAIGN_COMPLETION_REPORT.md`). Explain, BY NAME, every count delta —
   this campaign added: 2 tests from Phase 3
   (`RegisterPluginPanel_RefusesACollisionWith...` x2), 3 tests from Phase 5
   (`PluginRenderFeatureDiagnosticsTest.*`), Phase 6's new
   `FixedBufferReaderTest.*` (however many you added), and Phase 8's own 3
   new `PluginHostFailurePathTest.*` — add these up and confirm the real
   observed delta matches exactly; if it does not, that is a REAL regression
   to investigate, not something to wave away.
3. **Every existing probe, re-run fresh**: `tools/ci/gte_core_standalone_probe`,
   `tools/ci/gte_core_player_link_probe`, `tools/ci/gte_plugin_abi_handshake_probe`,
   `tools/ci/gte_plugin_isolation_probe` (this last one per its own updated,
   Phase-5-driven expectations — 4 plugins loaded, 2 implementing
   `IRenderFeatureModule_v1`).
4. **Live, HTTP-driven end-to-end smoke test**: `run_app_background`
   `build\GreatTamanaEditor.exe`, then, via `gte_send_request`:
   - `GET /get_swapchain` — confirm every pre-existing panel is present, the
     Game/Scene View is still solid magenta (now potentially produced by
     EITHER of the two render-feature plugins — visually identical either
     way, per Phase 5's own deliberate design), and "Demo Plugin Panel" is
     still docked.
   - `GET /list_tabs` — confirm every pre-existing name, unchanged.
   - `GET /get_logs?limit=100` — confirm: Phase 1's shared-CRT risk warning,
     Phase 5's "2 loaded plugins implement IRenderFeatureModule_v1" warning,
     4 `"Loaded plugin '...'"` lines (one per real demo plugin), and (if you
     ran Phase 7's optional manual `.DLL` proof again here) zero unexpected
     new warnings/errors beyond what this campaign itself deliberately added
     — the 5 fixture `.dll`s from this phase must NEVER appear anywhere in
     this log (they live in a test-only folder the real Editor never scans;
     seeing them here would itself indicate a real isolation-folder bug).
   - `stop_app_background` when done.
5. **Verify `-DGTE_ENABLE_PLUGINS=OFF` still builds** (Phase 2's own fix) —
   re-run Phase 2's own throwaway-side-build-directory check, fresh, as this
   campaign's own final confirmation that fix is real and still holds after
   every later phase's own changes. **Use a DIFFERENT command than Phase 2's
   own literal one** — Phase 2's own verification command included
   `-DGTE_BUILD_TESTS=OFF` (deliberately, since at that point in the campaign
   there was nothing test-related to prove); THIS phase's own re-run must
   instead leave `GTE_BUILD_TESTS` at its default (`ON`), since this phase's
   own new fixture targets live under `tests/` and the whole point of this
   re-run is confirming they ALSO still build correctly under
   `GTE_ENABLE_PLUGINS=OFF`:
   `cmake -S . -B build-plugins-off -G Ninja -DGTE_ENABLE_PLUGINS=OFF`
   (no `-DGTE_BUILD_TESTS=OFF`), then `cmake --build build-plugins-off
   --target GreatTamanaEngineTests`. This build must successfully produce
   `GreatTamanaEngineTests` with all 5 new fixture targets built — Phase 2's
   fix (an unconditional `add_subdirectory(plugins/gte_plugin_abi)`) is
   exactly what keeps `gte_plugin_abi` (and therefore this phase's own
   fixtures, which link it) buildable even with `GTE_ENABLE_PLUGINS=OFF`; if
   this side build fails ONLY on the new fixtures, that is a real,
   newly-introduced regression this phase itself caused, not a pre-existing
   one to wave away. Delete `build-plugins-off` again once verified, same as
   Phase 2's own cleanup step.
6. Write `CAMPAIGN_COMPLETION_REPORT.md` in this same folder — mirror
   `task_manager/editor-core-separation-3/CAMPAIGN_COMPLETION_REPORT.md`'s own
   structure closely: what the campaign set out to do, what shipped
   phase-by-phase (one paragraph per phase, summarizing each
   `PHASEn_COMPLETION_REPORT.md`), the full clean build + `ctest` evidence,
   every probe's fresh output, the live smoke test table, a restated
   before/after status of all 8 original re-analysis issues (explicitly
   confirm each of the 8 is now fixed, with a one-line pointer to which phase
   fixed it), and an honest "what remains genuinely open" section (e.g.
   Issue #1's warning-not-hard-refusal is a deliberate, permanent choice on
   this machine until a toolchain switch happens — restate that honestly,
   don't imply it's "fully fixed" in some stronger sense than it actually is).
7. If, and only if, this full regression pass surfaces a REAL, newly-broken
   test (not an already-known, already-explained count delta), use
   `delegate_task` to fix specifically that regression before finishing this
   phase and writing the closeout report — do not write a "ready to merge"
   closeout report while a real, unexplained failure is still present.
   (Per `PHASE0_MASTER_STRATEGY.md`'s own Workflow Rule 7, THIS specific
   escape hatch — fixing a regression discovered by Phase 8's own mandatory
   full regression run — is the one exception where an implementation phase
   in this campaign is allowed to invoke `delegate_task`; every other phase
   must not.)

## Completion

Write `PHASE8_COMPLETION_REPORT.md` (the per-phase report, same as every
other phase) AND `CAMPAIGN_COMPLETION_REPORT.md` (the campaign-wide summary,
per Step 4.6 above) in this same folder. Then `git_add` + `git_commit`
(message referencing PHASE8 and the campaign closeout).
