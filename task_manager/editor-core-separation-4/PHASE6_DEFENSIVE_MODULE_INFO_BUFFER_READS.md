# PHASE6 — Defensive `GtePluginModuleInfo` Buffer Reads

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE5`'s own `COMPLETION_REPORT.md` files if they exist.

**Severity:** MEDIUM (Issue #6 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "6.
MEDIUM — `PluginHost` trusts a third-party plugin's `GtePluginModuleInfo`
buffers to be null-terminated, with no defensive bound".

---

## Step 1: The Goal

Stop trusting a plugin's own `GetModuleInfo()` implementation to correctly
null-terminate its fixed-size `char[]` buffers. `GtePluginModuleInfo`
(`plugins/gte_plugin_abi/GtePluginModuleInfo.h`) is:

```cpp
struct GtePluginModuleInfo {
    char name[64] = {};
    char version[16] = {};
    char description[128] = {};
};
```

Nothing in the ABI contract MECHANICALLY forces null-termination — it is
convention only (today's 3-4 demo plugins happen to follow it correctly via
`strncpy(outInfo.name, "...", sizeof(outInfo.name) - 1)`, which always leaves
at least the final byte `\0` since the buffer itself is zero-initialized by
the struct's own default member initializers `= {}` AND `strncpy`'s own
`size - 1` bound leaves room). But `PluginHost.cpp`'s own log line:

```cpp
GTE_LOG_INFO("PluginHost", std::string("Loaded plugin '") + info.name + "' v" + info.version + " from " + pathStr);
```

uses `std::string::operator+(const char*)`, which calls `strlen()`
internally. If ANY plugin's `GetModuleInfo()` (including a genuinely
third-party one this engine's own authors never wrote, or even a first-party
one with a future copy-paste bug) writes exactly 64 (or 16/128) non-null
bytes with no terminator, this is a real out-of-bounds read the moment the
host tries to log it — the entire point of this system is to load `.dll`s the
engine's own authors did NOT write; trusting caller-supplied fixed buffers to
self-terminate, with zero defensive bound on the HOST side, is a real
robustness gap.

## Step 2: The Situation

Only ONE real, production consumption site exists in `src/` today (confirmed
via `search_in_dir` for `info.name` across `src/`):
`src/Core/Plugins/PluginHost.cpp`, line 164 (shown above). This is the one
and only place to fix for this phase — `tools/ci/gte_plugin_abi_handshake_probe/
main.cpp` and `tools/ci/gte_plugin_isolation_probe/main.cpp` also call
`GetModuleInfo()` and print `info.name`/`info.description` via `printf("%s",
...)`, but they are throwaway CI probes, not the production host boundary
this issue is about — leave them untouched, do not expand this phase's scope
beyond the one real file the re-analysis document names.

## Step 3: The Plan (exact changes)

### 3.1 Add a small, reusable, bounded-read helper

File: `src/Core/Plugins/PluginHost.cpp`, inside the existing anonymous
namespace (the same one that already has `DescribeFingerprintMismatch()`):

```cpp
// editor-core-separation-4 campaign, PHASE6
// (PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md) - GtePluginModuleInfo's
// fixed-size char[] buffers are NOT mechanically guaranteed to be
// null-terminated by the ABI contract itself (only by convention - see that
// struct's own doc comment) - a plugin the engine's own authors did not
// write could write a non-terminated buffer, and this engine has no way to
// verify that in advance. strnlen(buf, sizeof(buf)) is the one correct,
// always-safe way to read it: it never reads past the buffer's own known,
// fixed size, regardless of whether a null terminator is actually present
// inside it.
std::string ReadFixedBuffer(const char* buffer, std::size_t bufferSize)
{
    return std::string(buffer, strnlen(buffer, bufferSize));
}
```

`strnlen` needs `<cstring>` — add `#include <cstring>` to this file's include
list if not already present (check first; `<windows.h>` may already
transitively provide it on this toolchain, but never rely on an implicit
transitive include — add it explicitly regardless).

### 3.2 Use it at the one real call site

Replace:
```cpp
GTE_LOG_INFO("PluginHost", std::string("Loaded plugin '") + info.name + "' v" + info.version + " from " + pathStr);
```
with:
```cpp
GTE_LOG_INFO("PluginHost", "Loaded plugin '" + ReadFixedBuffer(info.name, sizeof(info.name)) + "' v"
    + ReadFixedBuffer(info.version, sizeof(info.version)) + "' from " + pathStr);
```

(Adjust quoting/parenthesization exactly as needed so this actually compiles
as one `std::string` concatenation — the sketch above is illustrative; verify
the real, compiling form yourself rather than pasting this verbatim without
checking.)

### 3.3 Double-check `DescribeFingerprintMismatch()` for the same class of bug

That function (same file) already reads `pluginFp.compilerId`/
`pluginFp.buildConfig` via `std::string(pluginFp.compilerId)` — but
`GtePluginAbiFingerprint.h`'s own generation code
(`GtePluginAbiFingerprintGenerated.h.in`) explicitly bounds its copy loop to
`i < 15` into a 16-byte buffer, guaranteeing byte 15 is always `\0` — this is
DIFFERENT from `GtePluginModuleInfo`, which has no such compile-time-generated
guarantee (a plugin author hand-writes `GetModuleInfo()` themselves, there is
no generated code enforcing anything). Confirm this distinction yourself by
re-reading both structs' own real, current field-population code before
deciding whether `DescribeFingerprintMismatch()` needs the same
`ReadFixedBuffer()` treatment — the re-analysis document's own Issue #6 only
calls out `GtePluginModuleInfo`, not `GtePluginAbiFingerprint`, because the
LATTER's fixed-width fields are always compiler/CMake-generated, never
plugin-author-hand-written. If your own re-reading finds the fingerprint path
is genuinely already safe for the reason above, leave it unchanged and note
this explicitly, with your own reasoning, in this phase's completion report
— do not blindly apply the same fix everywhere without first confirming which
places actually need it.

### 3.4 Add a Tier-1 test proving the bounded read is genuinely safe

`ReadFixedBuffer()` itself is a private, anonymous-namespace function inside a
`.cpp` file — not directly unit-testable from `tests/` as written. Two
options, pick whichever fits this codebase's own existing precedent better
after checking `tests/CMakeLists.txt`'s own taxonomy comment for how similar
small, file-local pure helpers elsewhere are (or are not) tested directly:

- **(a)** Move `ReadFixedBuffer()` into a small header
  (`src/Core/Plugins/FixedBufferReader.h`, header-only, `inline` function) so
  it becomes directly includable and testable from `tests/Core/
  FixedBufferReaderTests.cpp`, OR
- **(b)** Leave it as a private helper in `PluginHost.cpp` and instead prove
  the end-to-end safety property via a real `GtePluginModuleInfo` fixture in
  a NEW or EXTENDED test that constructs a deliberately non-terminated buffer
  by hand (e.g. `GtePluginModuleInfo info; std::memset(info.name, 'A',
  sizeof(info.name));` — every byte 'A', zero terminator) and confirms
  reading it via the exact same logic used in production does not read out
  of bounds and produces the expected, fully-populated 64-character string
  (address-sanitizer/valgrind are not available on this toolchain per this
  repo's own established constraints, so "does not crash and produces the
  exact expected truncated value" is the practical, available proof here).

Prefer **(a)** — it is more directly testable and mirrors this codebase's own
"extract pure logic into its own small header, test it directly" convention
(AGENTS.md, "Testability & Regression Safety") more closely than (b). If you
choose (a), remember to also update `PluginHost.cpp` to `#include` the new
header and call the free function instead of a private anonymous-namespace
one, and add the new header to `gte_core`'s unconditional source list in root
`CMakeLists.txt`. Add the new test file
(`tests/Core/Plugins/FixedBufferReaderTests.cpp`, or `tests/Core/
FixedBufferReaderTests.cpp` if this repo's own `tests/` tree does not mirror
a `Plugins/` subfolder yet — check `browse_dir` on `tests/Core/` first to
match whatever convention already exists there) to `tests/CMakeLists.txt`.

```cpp
TEST(FixedBufferReaderTest, ReadFixedBuffer_NormalNullTerminatedStringReadsCorrectly)
{
    char buffer[16] = {};
    std::strncpy(buffer, "hello", sizeof(buffer) - 1);
    EXPECT_EQ(ReadFixedBuffer(buffer, sizeof(buffer)), "hello");
}

TEST(FixedBufferReaderTest, ReadFixedBuffer_NonTerminatedFullBufferReadsExactlyItsFixedSizeNeverMore)
{
    char buffer[16];
    std::memset(buffer, 'A', sizeof(buffer)); // deliberately NO null terminator anywhere
    const std::string result = ReadFixedBuffer(buffer, sizeof(buffer));
    EXPECT_EQ(result.size(), sizeof(buffer));
    EXPECT_EQ(result, std::string(sizeof(buffer), 'A'));
}

TEST(FixedBufferReaderTest, ReadFixedBuffer_EmptyBufferReadsEmptyString)
{
    char buffer[16] = {};
    EXPECT_EQ(ReadFixedBuffer(buffer, sizeof(buffer)), "");
}
```

## Verification (fast, incremental — no full build)

1. `cmake --build build --target gte_core` (rebuilds `PluginHost.cpp`) then
   `cmake --build build --target GreatTamanaEngineTests`.
2. Run `build\GreatTamanaEngineTests.exe --gtest_filter=FixedBufferReaderTest.*`
   (or whatever exact test-suite name you chose) — confirm all pass,
   including the deliberately non-terminated buffer case.
3. Live check: `run_app_background` the rebuilt `GreatTamanaEditor.exe`,
   `gte_send_request` `GET /get_logs?limit=50`, confirm the existing 4
   `"Loaded plugin '...'"` lines (from Phase 5's now-4 demo plugins) still
   read correctly, unchanged in visible content. `stop_app_background` when
   done.

## Completion

Write `PHASE6_COMPLETION_REPORT.md`: which option (a/b) you chose and why,
the exact new test file added, the test run output, the live log check
result. Then `git_add` + `git_commit` (message referencing PHASE6).
