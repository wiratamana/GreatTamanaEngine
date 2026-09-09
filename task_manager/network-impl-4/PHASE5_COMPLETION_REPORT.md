# PHASE5 — Completion Report (`network-impl-4`)

Implements `PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md` in full
(the second-iteration-audited revision) — `GET /get_texture` and
`GET /list_textures` are now fully wired end to end, built on top of the
Phase 1-4 registry/readback/bridge machinery. This is a real, buildable code
change, not a plan/description.

## 1. Which `frames_since_update` approach was actually implemented

The Phase 5 document being executed here was **already revised** (its own
"second-iteration audit note") to definitively close the "Option 1 vs
Option 2" ambiguity the earlier revision had left open. The concrete design
it commits to — and the one implemented here, verbatim — is:

**A third, small, plain-data struct, `gte::PublishedTextureListEntry`**,
added to `src/Application/FrameCaptureBridge.h` (the `gte` namespace, NOT
`gte::Network`). Its fields are all **already-resolved plain scalars**
(`name`/`regime`/`format` as `std::string`, `width`/`height` as
`std::uint32_t`, `hasDepth` as `bool`, `framesSinceUpdate` as
`std::uint64_t`) — it never references `rg::DebugTextureSnapshot` or
`rg::ExecuteTimingMode` in any way, keeping `FrameCaptureBridge.h`
Vulkan/Renderer/RenderGraph-free exactly as its own header comment promises.

The full data flow, matching the document's three-file split exactly:

1. **`Application::Run()`** (the one place that legitimately knows about
   both `RenderGraph` and `FrameCaptureBridge`) calls
   `m_renderGraph.ListDebugTextures()` +
   `m_renderGraph.CurrentDebugTextureFrameCounter()` once per frame, resolves
   each `rg::DebugTextureSnapshot` into a `PublishedTextureListEntry` via two
   new anonymous-namespace helpers (`ToDebugTextureRegimeString()`,
   `DebugTextureColorFormatName()`), computes
   `framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter`
   for every known texture at once, and calls
   `m_captureBridge.PublishTextureList(std::move(published))`.
2. **`FrameCaptureBridge`** stores the published vector behind its own
   dedicated `m_textureListMutex` (deliberately no condition variable —
   nothing ever blocks on this), and hands back a cheap copy via
   `GetPublishedTextureList()`.
3. **`NetworkServer.cpp`** (the one place that legitimately knows about both
   `FrameCaptureBridge` and `NetworkRoutes.h`) is the ONLY place that copies
   a `PublishedTextureListEntry` into a fresh
   `gte::Network::TextureListEntryView` (a separate, `Network`-namespace,
   `NetworkRoutes.h`-owned struct with the exact same field shape), one
   field at a time, right before calling `BuildListTexturesResponseJson()`.

This keeps both of the engine's existing "a struct must never cross this
exact layer boundary" rules intact simultaneously — neither
`FrameCaptureBridge.h` nor `NetworkRoutes.h` ever depends on the other
layer's type — with the trivial 1:1 copy living at the one file that already
depends on both.

## 2. Files edited

- `src/Network/NetworkRoutes.h`:
  - Added `#include <vector>` (required for
    `BuildListTexturesResponseJson()`'s `const
    std::vector<TextureListEntryView>&` parameter — confirmed neither
    `<cstdint>` nor `<string>` brings it in transitively).
  - Added `ParsedGetTextureQuery` + `ParseGetTextureQuery()`.
  - Added `BuildTextureCaptureJsonBody()`.
  - Added `TextureListEntryView` + `BuildListTexturesResponseJson()`.
- `src/Network/NetworkRoutes.cpp`:
  - Implemented `ParseGetTextureQuery()`, `BuildTextureCaptureJsonBody()`,
    `BuildListTexturesResponseJson()` — all via `nlohmann::json` (already
    `#include <nlohmann/json.hpp>`'d in this file, exact spelling confirmed
    live before editing), never hand-formatted string concatenation.
- `src/Application/FrameCaptureBridge.h`:
  - Added `PublishedTextureListEntry` (right after `CapturedPngImage`, per
    the plan).
  - Added `PublishTextureList()`/`GetPublishedTextureList()` public methods
    (right after `FailPendingRequest()`).
  - Added `m_textureListMutex`/`m_publishedTextureList` private members.
  - No new `#include` needed — `<string>`/`<vector>`/`<mutex>`/`<cstdint>`
    were all already present (confirmed live before editing).
- `src/Application/FrameCaptureBridge.cpp`:
  - Implemented `PublishTextureList()`/`GetPublishedTextureList()`.
- `src/Application/Application.cpp`:
  - Added `ToDebugTextureRegimeString()`/`DebugTextureColorFormatName()`
    anonymous-namespace helpers, right alongside the existing
    `IsBgraFormat()`.
  - Added the new `/list_textures` publish block to `Application::Run()`,
    placed directly after Phase 4's own named-texture capture block (both
    unconditional, once per `Run()` iteration, neither depends on the
    other's result), still before
    `Profiling::FrameProfiler::Instance().SetMemorySnapshot(...)` — exactly
    the placement the plan specifies. No new `#include` was needed —
    `RenderGraph/RenderGraphDebugTextureRegistry.h` was already added by
    Phase 4's own block, and `<cstdio>` (for `std::snprintf`) was already
    present.
- `src/Network/NetworkServer.cpp`:
  - Added `RegisterGetTextureRoute()` (a new, parallel route-registration
    function, deliberately NOT shoehorned into the existing
    `RegisterCaptureRoute()` helper — the plan's own Step 2 reasoning: too
    many extra params/branches to be worth sharing).
  - Added `RegisterListTexturesRoute()` — needs no `RenderGraph`/`rg::`
    header at all, confirmed: it only ever touches plain scalars and its own
    `TextureListEntryView`.
  - Wired both into `RegisterRoutes()`, directly alongside the two existing
    `RegisterCaptureRoute(...)` calls, using the same `captureBridge`
    parameter already in scope.
- `tests/Network/NetworkRoutesTests.cpp`:
  - Table-driven `ParseGetTextureQuery()` coverage (missing/empty
    `texture_name`, `channel` absent/`"color"`/`"depth"`, and a
    `TEST_P`-based invalid-channel suite covering `"Depth"`/`"COLOR"`/
    `"bogus"` — regression-proving the exact-lowercase-only matching rule).
  - `BuildTextureCaptureJsonBody()`: builds, re-parses via
    `nlohmann::json::parse()`, asserts individual field values.
  - `BuildListTexturesResponseJson()`: an empty-list literal-string case
    (`{"textures":[]}`, the one safe exception per the plan's own Step 3.9),
    a 2-entry case asserting every field by key, and a name-with-a-quote
    case proving `nlohmann::json`'s own escaping (not hand-formatting) is
    what protects this endpoint.
- `tests/Application/FrameCaptureBridgeTests.cpp`:
  - A case confirming `GetPublishedTextureList()` returns empty before any
    `PublishTextureList()` call.
  - A cross-thread publish/read-back round-trip case asserting every field
    of two entries.
  - A case confirming publishing an EMPTY list after a non-empty one
    correctly clears it (`PublishTextureList()`'s own "overwrites wholesale,
    never merges" contract).

## 3. Deviations from the plan

**None found.** Every "confirmed live" claim in the phase document's own
Step 2 (exact signatures of `RegisterCaptureRoute()`/`RegisterRoutes()`,
`NetworkRoutes.cpp`'s existing `#include <nlohmann/json.hpp>` spelling,
`NetworkRoutes.h`'s missing `<vector>` include,
`ResolveCaptureResponseFormat()`/`BuildCaptureJsonBody()`'s signatures, the
`FrameCaptureBridge.h`/`.cpp` shape left by Phase 4) held up exactly as
documented once re-checked against the live source tree before editing. The
Step 3.1-3.7 code blocks were transcribed essentially verbatim (the same
"pseudocode was already logically correct" outcome Phase 4's own completion
report reached for its own plan).

One genuinely NEW piece of work this phase's own plan explicitly assigned
here (rather than deferring to Phase 6): **Step 3.9's Tier-1 test coverage**
for both new `NetworkRoutes.h` builders/parser and
`FrameCaptureBridge`'s `PublishTextureList()`/`GetPublishedTextureList()` —
all written and passing (see Section 5). This matches the parent task's own
explicit instruction to write the Tier-1 tests this phase specifies, rather
than deferring them to Phase 6 as the ORIGINAL (pre-instruction) plan
implied for some of Phase 4's own work.

## 4. Compile check

Ran `cmake --build build --target gte_core` (incremental) first:

```
[1/5] Building CXX object CMakeFiles/gte_core.dir/src/Application/FrameCaptureBridge.cpp.obj
[2/5] Building CXX object CMakeFiles/gte_core.dir/src/Network/NetworkRoutes.cpp.obj
[3/5] Building CXX object CMakeFiles/gte_core.dir/src/Application/Application.cpp.obj
[4/5] Building CXX object CMakeFiles/gte_core.dir/src/Network/NetworkServer.cpp.obj
[5/5] Linking CXX static library libgte_core.a
```

Then `cmake --build build --target GreatTamanaEngineTests` (incremental,
after adding the new test code):

```
[1/4] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Application/FrameCaptureBridgeTests.cpp.obj
[2/4] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Network/NetworkRoutesTests.cpp.obj
[3/4] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Network/CaptureEndpointsEndToEndTests.cpp.obj
[4/4] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

Then a full `cmake --build build` (the default `GreatTamanaEngine.exe`
target) to confirm the main engine executable — which transitively includes
every file this phase touched — still builds cleanly end to end:

```
[1/2] Building CXX object CMakeFiles/GreatTamanaEngine.dir/src/main.cpp.obj
[2/2] Linking CXX executable GreatTamanaEngine.exe; Staging ... .spv next to GreatTamanaEngine; Copying SDL3.dll next to GreatTamanaEngine
```

**Clean build, zero errors/warnings, all three targets.**

## 5. Test run (fast, targeted — not the full `ctest` suite, per this task's rules)

Ran every newly-added test plus the pre-existing capture/JSON-builder tests
that share the same files, to confirm no regression:

```
tests\GreatTamanaEngineTests.exe --gtest_filter=FrameCaptureBridgeTest.*:ParseGetTextureQueryTests.*:ParseGetTextureQueryInvalidChannelTest.*:BuildTextureCaptureJsonBodyTests.*:BuildListTexturesResponseJsonTests.*
```
**19/19 pass** (10 `FrameCaptureBridgeTest` — 7 pre-existing + 3 new — plus
9 new `ParseGetTextureQueryTests`/`BuildTextureCaptureJsonBodyTests`/
`BuildListTexturesResponseJsonTests` cases; the invalid-channel `TEST_P`
suite is separately registered under
`NetworkRoutesTests/ParseGetTextureQueryInvalidChannelTest` — confirmed via
a second run with `--gtest_filter=*InvalidChannel*`, **3/3 pass**).

```
tests\GreatTamanaEngineTests.exe --gtest_filter=NetworkRoutesTests.*:BuildResponseJsonTests.*:ParseInstantiatePrimitiveRequestTests.*:ParseDeleteEntityRequestTests.*
```
**34/34 pass** — every pre-existing `NetworkRoutes.h`-family test (Phase 3 of
`network-impl-2`, Phase 1/5 of `network-impl-3`) still passes unchanged
after this phase's own additions to the same files.

Per this task's own instructions, the full `ctest` regression suite was NOT
run (reserved for Phase 6).

## 6. What this phase deliberately does NOT do (per the plan)

- Does not modify `RegisterCaptureRoute()`/`BuildCaptureJsonBody()`/
  `ResolveCaptureResponseFormat()` — reused, untouched.
- Does not change `/get_swapchain`/`/get_game_view`'s existing response
  `Content-Type` conventions for their own failure statuses.
- Does not add pagination/filtering to `/list_textures`.
- Does not add a `POST` variant of either endpoint.
- Does not report a texture's DEPTH format anywhere in `/list_textures`
  (only its COLOR format, plus `has_depth`).
- `Application.cpp`'s own new publish block and `NetworkServer.cpp`'s own
  two new route lambdas stay Tier 2 (need a live `Renderer`/`RenderGraph`/
  `httplib::Server`) — end-to-end manual verification against a running
  `GreatTamanaEngine.exe` is Phase 6's job, per the plan's own Step 3.9.

## Summary

- Branch: `feature/network-impl` (unchanged, as required).
- Edited files: `src/Network/NetworkRoutes.h`, `src/Network/NetworkRoutes.cpp`,
  `src/Application/FrameCaptureBridge.h`, `src/Application/FrameCaptureBridge.cpp`,
  `src/Application/Application.cpp`, `src/Network/NetworkServer.cpp`,
  `tests/Network/NetworkRoutesTests.cpp`,
  `tests/Application/FrameCaptureBridgeTests.cpp`.
- No new files created this phase (this document itself excepted).
- `frames_since_update` plumbing for `/list_textures` implemented exactly as
  the current (already-audited) Phase 5 document commits to: the
  three-struct split (`rg::DebugTextureSnapshot` → `PublishedTextureListEntry`
  → `TextureListEntryView`), resolved in `Application::Run()`, copied 1:1 in
  `NetworkServer.cpp`.
- Compile check: clean, zero errors/warnings (`gte_core`,
  `GreatTamanaEngineTests`, and the main `GreatTamanaEngine.exe` target).
- New tests: 22 new Tier-1 test cases added (13 in `NetworkRoutesTests.cpp`,
  9 in `FrameCaptureBridgeTests.cpp` — including the 3-case `TEST_P` suite),
  all passing; 34 pre-existing related tests re-run and still passing (no
  regression).
- No deviation from the phase document's own plan was found.
- Ready for Phase 6 (`PHASE6_TESTS_DOCS_AND_REGRESSION_SAFETY`).
