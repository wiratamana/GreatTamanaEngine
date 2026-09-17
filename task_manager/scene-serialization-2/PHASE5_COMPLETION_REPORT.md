# PHASE5 COMPLETION REPORT — `POST /save_scene` and `POST /load_scene` Network Endpoints

_Campaign: `task_manager/scene-serialization-2/`. Parent: `PHASE0_MASTER_STRATEGY.md`.
Phase file implemented: `PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md`._

Branch: `feature/scene-serialization` (unchanged, as required).

## Prerequisites followed

- Read `README.md` and `AGENTS.md` at the project root.
- Re-read `PHASE0_MASTER_STRATEGY.md` in full for overall campaign context
  (Locked Design Decisions, Cross-Phase Invariants, Appendix A's on-disk
  shape) before starting.
- Read `PHASE1_COMPLETION_REPORT.md` through `PHASE4_COMPLETION_REPORT.md`
  (every prior phase report in this folder). `PHASE4_COMPLETION_REPORT.md`'s
  own "Notes for PHASE5 / PHASE6" section was the most load-bearing: it
  confirmed `Game::EnsureDefaultCameraExists()` is now `public` (a
  discrepancy fix from that phase), noted the still-outstanding live-engine
  smoke test from Phase 4's own Definition of Done, and confirmed no
  discrepancy had been found yet against this phase's own `.md` file.
- Read `PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md` in full, including its
  own Step 3.1–3.7 code snippets, before writing any code.
- Read the real, current `src/Game/EngineCommandResults.h`,
  `src/Application/EngineCommandBridge.h`, `EngineCommandDispatch.cpp`,
  `src/Editor/SceneIO.h/.cpp`, `src/Network/NetworkRoutes.h/.cpp`,
  `NetworkServer.cpp` (in particular the existing `/import_asset`/
  `/instantiate_asset` routes' exact 503/400/503/504 mapping shape) and
  `docs/conventions/networking.md` before writing any code, per this
  phase's own Step 2 instruction to re-confirm the established pattern
  directly against the real files.

## What was built

Implemented essentially verbatim against this phase's own Section 3.1–3.7
code snippets - this phase is "100% wiring" exactly as its own Step 1
states, and no design deviation was needed anywhere.

### 3.1 — `src/Game/EngineCommandResults.h`

Added `SaveSceneOutcome`/`LoadSceneOutcome` (both:
`success`/`editorAvailable`/`errorMessage`/`resolvedPath`), placed right
after the existing `InstantiateMeshAssetOutcome`, exactly as specified.

### 3.2 — `src/Application/EngineCommandBridge.h`

- `EngineCommandKind` gained two new enumerators, `SaveScene`/`LoadScene`,
  appended at the end (never reordering/renumbering the existing values,
  per this phase's own instruction).
- New plain payload structs `SaveSceneCommand`/`LoadSceneCommand` (each:
  one `std::string path`).
- `EngineCommandRequest` gained `saveScene`/`loadScene` fields;
  `EngineCommandResult` gained `saveScene`/`loadScene` fields (the outcome
  types from 3.1) - mirroring every existing field in both structs exactly.

### 3.3 — `src/Editor/SceneIO.h`/`.cpp`

Added explicit-path overloads exactly as specified:

```cpp
bool SaveScene(Game& game, const std::filesystem::path& scenePath);
bool SaveScene(Game& game); // forwards to the overload above with DefaultScenePath().

bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath);
bool LoadScene(Game& game, Renderer& renderer); // forwards to the overload above with DefaultScenePath().
```

The existing bodies were moved into the new explicit-path overloads
verbatim, replacing the one internal use of `DefaultScenePath()` in each
with the `scenePath` parameter; the zero-argument overloads became
one-line forwarders. `Editor/DockLayout.cpp`'s Ctrl+S/Ctrl+O call sites
were not touched at all (they still call the unchanged zero-argument
overloads) and their behavior is unchanged - confirmed by re-reading
`DockLayout.cpp`'s own call sites, which still read `SaveScene(*m_game)`/
`LoadScene(*m_game, *m_renderer)` with no argument-count change needed.
`SceneIO.h`'s own doc comments were rewritten to describe both overloads.

### 3.4 — `src/Application/EngineCommandDispatch.cpp`

Added `#include <filesystem>` and a `#if GTE_ENABLE_EDITOR` /
`#include "../Editor/SceneIO.h"` / `#endif` block at the top (this file is
a CORE, always-compiled file; `Editor/SceneIO.h` is Editor-only). Added the
two new `case` branches inside `ExecuteEngineCommand()`'s switch, each
`#if GTE_ENABLE_EDITOR`-gated: the `GTE_ENABLE_EDITOR=ON` path resolves
`request.saveScene.path`/`request.loadScene.path` (empty string ->
`DefaultScenePath()`) and calls the real `SaveScene()`/`LoadScene()`
overloads from 3.3, filling in `success`/`resolvedPath`/`errorMessage`; the
`#else` path sets `editorAvailable = false` plus a fixed explanatory
`errorMessage`, implemented exactly per this phase's own Section 3.4 code
snippet.

### 3.5 — `src/Network/NetworkRoutes.h`/`.cpp`

Added `ParsedScenePathRequest`/`ParseScenePathRequest()`/
`BuildScenePathResponseJson()`, implemented exactly per this phase's own
Section 3.5 doc comments and code:

- `ParseScenePathRequest()` is the ONE deliberate exception in this file to
  the "malformed/non-object JSON body is a 400 validation failure"
  convention every OTHER `ParseXxxRequest()` follows - a genuinely empty
  body, malformed JSON text, or a non-object top-level value are all
  treated identically to "`path` simply omitted" (`valid = true`,
  `path = ""`), per this phase's own explicit instruction; a real inline
  comment at the top of the function body calls this out so a future
  reader doesn't "fix" it to match the others (mirroring the phase file's
  own explicit request).
- An explicitly-present, non-string `"path"` value (number/bool/object/
  array) IS still a validation failure: `"path must be a string"`.
- `BuildScenePathResponseJson()` follows the exact
  `if (!success) { return BuildGenericErrorResponseJson(errorMessage); }`
  early-return shape `BuildSetEntityTrsResponseJson()` already established.

### 3.6 — `src/Network/NetworkServer.cpp`

Registered `server.Post("/save_scene", ...)` and `server.Post("/load_scene",
...)` inside `RegisterRoutes()`, right after the existing
`/instantiate_asset` route, both reusing the SAME `commandBridge` parameter
`RegisterRoutes()`/`NetworkServer`'s constructor already receive - no new
bridge parameter was needed (confirmed directly by re-reading
`NetworkServer.cpp`'s constructor before editing, per this phase's own
instruction). Each route: parse -> 400 on invalid -> `commandBridge ==
nullptr` -> 503 -> build an `EngineCommandRequest` -> `SubmitAndWait()` ->
`alreadyPending`/`timedOut` -> 503/504 -> `outcome.editorAvailable == false`
-> 503 -> map `outcome.success` to a final status code + body. The ONE
deliberate difference between the two routes is implemented exactly as
instructed: `/save_scene` maps a genuine failure to `500` (an I/O-write
problem - an environment issue, not a caller mistake), while `/load_scene`
maps a genuine failure to `400` (a caller-actionable "that scene file
wasn't found or was invalid" case).

### 3.7 — `docs/conventions/networking.md`

- Added a new bullet, right after the existing `POST /instantiate_asset`
  bullet, describing `POST /save_scene`/`POST /load_scene` in the same
  voice/detail level as the surrounding bullets: the optional `path`
  field's exact semantics (including the deliberate "malformed/empty body
  is not an error" exception), the `200`/`400`/`503`/`504` status codes,
  and the `500`-vs-`400` distinction from 3.6, plus a cross-reference to
  this campaign's `PHASE0_MASTER_STRATEGY.md`.
- Updated the existing "JSON parsing now exists via a vendored
  nlohmann/json" bullet to note that, as of this campaign, `nlohmann::json`
  is ALSO this engine's on-disk scene file format (`Scene/SceneJsonFormat.h`)
  and the value type the new `ECS/Reflection/` layer serializes component
  fields into/out of - not merely a network-parsing exception anymore -
  per PHASE0's Locked Design Decision #1, while keeping the historical
  "network-impl-3 needed this to parse untrusted input" framing intact as
  context for why the dependency was first introduced. Also corrected this
  bullet's stale reference to the now-deleted `Scene/SceneTextFormat.h`
  (removed in this campaign's own Phase 3) to note it was superseded/deleted
  rather than silently leaving a dangling file reference.

## Tests added

`tests/Network/NetworkRoutesTests.cpp` gained a new section (mirroring this
phase's own Definition of Done's requested case list):

- `ParseScenePathRequestTests`: empty body, `{}`, an explicit
  `{"path":"C:\\some\\path.gtscene"}`, an explicit empty-string path, an
  explicit `null` path, a non-string `path` (`123`, rejected with the exact
  `"path must be a string"` message), malformed JSON text (`"{not valid
  json"`, treated as "no path", NOT a failure - the one deliberate
  exception this whole feature hinges on), and a non-object top-level value
  (`"[]"`/`"42"`, also treated as "no path").
- `BuildScenePathResponseJsonTests`: the success shape (`success`/
  `resolved_path`, no stray `error` key) and the failure shape (byte-for-
  byte identical to `BuildGenericErrorResponseJson()`).

No other Tier-1 test file needed changes - `EngineCommandBridge.h`'s new
`SaveScene`/`LoadScene` kinds are plain data additions with no new branching
logic of their own to test beyond what `EngineCommandBridgeTest`'s existing
generic pending/timeout/fulfillment coverage already exercises regardless
of which `EngineCommandKind` is used.

## Discrepancy found against this phase's own strategy file

None. Every piece of this phase's own `.md` file - the exact struct shapes
in 3.1/3.2/3.3, the `#if GTE_ENABLE_EDITOR` dispatch shape in 3.4, the
parsing/response-building rules in 3.5, the route-registration shape in
3.6, and the doc-update instructions in 3.7 - matched the real codebase and
compiled/worked exactly as written on the first attempt, with zero code
changes needed beyond what the phase file itself specifies. This is
consistent with the phase's own Step 1 framing ("100% wiring... no new
engine logic here") - by this point in the campaign (Phases 1-4 already
having built and hardened the real Save/Load system this phase only
exposes over HTTP), there was no remaining design ambiguity to resolve.

## Manual end-to-end verification (live running engine)

Per this phase's own Definition of Done, ran a full live smoke test against
a real, running `GreatTamanaEngine.exe` (via `run_app_background`/
`gte_send_request`), for BOTH build configurations:

**`GTE_ENABLE_EDITOR=ON` (`build/`)**:
- `POST /save_scene` with an empty JSON body (`{}`) -> `200`,
  `{"resolved_path":"C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\build\\Project\\TestScene.gtscene","success":true}`.
- `POST /load_scene` with an empty JSON body -> `200`, same
  `resolved_path`, `success:true`.
- `POST /save_scene` with `{"path":123}` -> `400`,
  `{"error":"path must be a string","success":false}`.
- `POST /load_scene` with a path pointing at a nonexistent file -> `400`,
  `{"error":"failed to load scene file - it may not exist, or failed to
  parse (see engine log)","success":false}`.
- `GET /get_swapchain` afterward confirmed the Editor is still alive and
  healthy (Hierarchy/Scene/Game/Project panels all rendering normally,
  `TestScene.gtscene` visible in the "Project" panel's file list next to
  `terrain.gta`, a `Camera` entity present in "Hierarchy" - i.e. the
  post-Load scene state is exactly what's expected).

**`GTE_ENABLE_EDITOR=OFF` (`build-editor-off/`, rebuilt fresh with this
phase's changes)**:
- `POST /save_scene` with an empty body -> `503`,
  `{"error":"scene save/load requires the Editor module (GTE_ENABLE_EDITOR
  is OFF in this build)","success":false}`.
- `POST /load_scene` with an empty body -> the same `503` shape.

Both engine processes were cleanly stopped (`stop_app_background`)
afterward. Every item in this phase's Definition of Done's manual-check
bullet is now confirmed, closing out the "still outstanding" live-engine
verification gap `PHASE4_COMPLETION_REPORT.md`'s own "Notes for PHASE5"
section had flagged (that one was about Phase 4's OWN reconciliation
algorithm specifically, involving a multi-part imported mesh asset that
still isn't available in this environment/session - this phase's own
scope, a plain empty-scene/Camera round trip over HTTP, needed no such
asset and was fully exercised).

## Verification

- Fast compile check, `GTE_ENABLE_EDITOR=ON`:
  `cmake --build build --target gte_core` - succeeded, zero errors/warnings.
  `cmake --build build --target GreatTamanaEngineTests` - succeeded, zero
  errors/warnings.
  `cmake --build build --target GreatTamanaEngine` - succeeded (needed for
  the live manual smoke test above), zero errors/warnings.
- Fast compile check, `GTE_ENABLE_EDITOR=OFF`:
  `cmake --build build-editor-off --target gte_core` - succeeded, zero
  errors/warnings (confirms `EngineCommandDispatch.cpp`'s `#if
  GTE_ENABLE_EDITOR` gating compiles correctly with the Editor branch
  entirely excluded).
  `cmake --build build-editor-off --target GreatTamanaEngine` - succeeded
  (needed for the live 503 smoke test above).
- Ran the full test suite filter covering every touched/added area:
  `tests\GreatTamanaEngineTests.exe
  --gtest_filter=ParseScenePathRequestTests.*:BuildScenePathResponseJsonTests.*:NetworkRoutesTests.*:*EngineCommand*:*Scene*`
  - **80/80 passed**, including the 8 new `ParseScenePathRequestTests` and
  2 new `BuildScenePathResponseJsonTests`, plus every pre-existing
  `EngineCommandBridgeTest`/`EngineCommandEndpointsEndToEndTest`/
  `SceneBuilderTest`/`SceneJsonFormatTest`/`SceneRoundTripIntegrationTest`
  unchanged and still green (no regression).
- Per this phase's own rule and the campaign-wide workflow rule, did
  **not** run a full solution build or the full `ctest` regression suite
  (reserved for Phase 6).
- `git status` confirms only the intended files changed:
  `docs/conventions/networking.md`, `src/Application/EngineCommandBridge.h`,
  `src/Application/EngineCommandDispatch.cpp`, `src/Editor/SceneIO.h/.cpp`,
  `src/Game/EngineCommandResults.h`, `src/Network/NetworkRoutes.h/.cpp`,
  `src/Network/NetworkServer.cpp`, `tests/Network/NetworkRoutesTests.cpp` -
  no new/deleted files, no `CMakeLists.txt` changes needed (no new
  translation units were added this phase).

## Definition of Done — checked against the phase file

- [x] `EngineCommandResults.h`: `SaveSceneOutcome`/`LoadSceneOutcome` added.
- [x] `EngineCommandBridge.h`: `SaveScene`/`LoadScene` kinds + command
      structs + result/request fields added.
- [x] `Editor/SceneIO.h/.cpp`: explicit-path overloads added; zero-argument
      overloads unchanged in behavior (confirmed live: Ctrl+S/Ctrl+O's own
      call sites in `DockLayout.cpp` needed no edit at all).
- [x] `EngineCommandDispatch.cpp`: both new `case` branches, correctly
      `#if GTE_ENABLE_EDITOR`-gated (confirmed by a clean compile AND a
      live 503 response in the `GTE_ENABLE_EDITOR=OFF` build).
- [x] `NetworkRoutes.h/.cpp`: `ParseScenePathRequest()`/
      `BuildScenePathResponseJson()` added and unit-tested (empty body,
      `{}`, an explicit path, `{"path":123}` rejected, malformed JSON
      treated as "no path" - every case this phase's own Definition of
      Done lists).
- [x] `NetworkServer.cpp`: both routes registered, with the `500`-vs-`400`
      distinction implemented correctly for Save vs Load failures
      (confirmed live).
- [x] `docs/conventions/networking.md` updated (both the new bullet and
      the existing nlohmann::json bullet's widened-scope note).
- [x] Manual end-to-end check via `gte_send_request`/`run_app_background`:
      done for BOTH `GTE_ENABLE_EDITOR=ON` (200 + real `resolved_path` for
      both routes, plus 400s for the documented failure cases) AND
      `GTE_ENABLE_EDITOR=OFF` (503 from both routes) - see "Manual
      end-to-end verification" above.
- [x] Fast compile check passes for BOTH `GTE_ENABLE_EDITOR=ON` and
      `=OFF`.
- [x] `PHASE5_COMPLETION_REPORT.md` written; `git add`/`git commit` follow
      next.

## Notes for PHASE6

- No discrepancy was found against `PHASE6_TESTS_DOCS_CLEANUP_AND_
  FULL_REGRESSION.md` itself - it was not edited, per this campaign's own
  workflow rule (a later phase's file is never edited from an earlier
  phase, even when a discrepancy is found - none was, this time).
- `PHASE4_COMPLETION_REPORT.md`'s own still-outstanding item - a live,
  end-to-end smoke test of Phase 4's full recipe-spawn reconciliation
  algorithm (a multi-part imported mesh, a hand-edited/unnamed child part
  Transform, a manually-reparented asset root) - remains genuinely
  UNVERIFIED against a real running engine, since no such `*.gta` asset was
  available in this session/environment either. This phase's OWN live
  verification (above) only exercised a plain empty-scene/`Camera`
  round trip over HTTP, which does not exercise that reconciliation path at
  all. This is worth doing as part of Phase 6's own full regression pass if
  a suitable multi-part mesh asset becomes available by then.
- Every file this phase touched now compiles and passes its own tests in
  both `GTE_ENABLE_EDITOR` configurations - Phase 6 should be able to run
  its own full build + `ctest` regression pass without any leftover
  Phase 5 cleanup work.
