# PHASE1 — COMPLETION REPORT: `NetworkRoutes.h/.cpp`: Request Parsing + Response Building

Status: **DONE.** All four new pure functions specified by
`PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md` (v2) were
added to the existing `src/Network/NetworkRoutes.h`/`.cpp`, with matching
unit tests added to the existing `tests/Network/NetworkRoutesTests.cpp`.
Zero new files were created, exactly as `PHASE0_MASTER_STRATEGY.md` requires.

## What was implemented

### `src/Network/NetworkRoutes.h`

- `struct ParsedSetEntityTrsRequest` + `ParseSetEntityTrsRequest(const std::string&)`
  — parses `POST /set_entity_trs`'s body: required `name`, plus three
  independently-optional, all-or-nothing groups (`translation`,
  `rotation_euler_degrees`, `scale`), each requiring all of `x`/`y`/`z` as
  JSON numbers when the key is present and non-null.
- `struct ParsedInstantiateLightRequest` + `ParseInstantiateLightRequest(const std::string&)`
  — parses `POST /instantiate_light`'s body: optional `light_type` (JSON-shape
  only, semantic validation deferred to Phase 2), required `name`, optional
  per-axis `world_position` (creation-time convention, matches
  `ParseInstantiatePrimitiveRequest()`), optional all-or-nothing
  `rotation_euler_degrees`, optional per-component `color`, optional
  non-negative `illuminance_lux`, optional boolean `active`, and optional
  `parent` (identical rule to `ParseInstantiatePrimitiveRequest()`'s own
  `parent` field).
- `struct TransformSnapshotView` — a plain, Math/Vec3/Quat-free struct
  bundling the 13 floats `set_entity_trs`'s response needs to echo back
  (position, rotation as both Euler degrees and raw quaternion, scale).
- `BuildSetEntityTrsResponseJson(...)` — builds `set_entity_trs`'s success
  response (`entity`/`changed`/`transform`), falling back to
  `BuildGenericErrorResponseJson()` on failure exactly like
  `BuildInstantiatePrimitiveResponseJson()`/`BuildDeleteEntityResponseJson()`
  already do.
- `instantiate_light`'s success/failure response reuses
  `BuildInstantiatePrimitiveResponseJson()` verbatim, per the phase doc's
  Step 3.4 — no new builder function was written for it. A short comment was
  added directly above that function's existing declaration explaining this
  reuse, so a future reader isn't confused about the absence of a
  `BuildInstantiateLightResponseJson()`.

### `src/Network/NetworkRoutes.cpp`

- A new private helper, `TryParseAllOrNothingXyz()` (anonymous namespace),
  implements the shared "absent/null → not-present, no error; present →
  must be an object with all of numeric x/y/z, or fail with `"<fieldName>
  must be an object with numeric x, y, and z fields"`" rule used by FOUR
  distinct call sites: `ParseSetEntityTrsRequest()`'s `translation`/
  `rotation_euler_degrees`/`scale`, and `ParseInstantiateLightRequest()`'s
  own `rotation_euler_degrees`. This keeps the four call sites byte-for-byte
  consistent instead of hand-duplicating the same four-line JSON check four
  times — a small, justified private helper, not the "premature
  abstraction" the phase doc cautioned against avoiding pre-emptively (it
  was written after confirming all four call sites genuinely need the exact
  same rule, not speculatively).
- Both new parsers implement the exact validation order/messages documented
  in the header, including the **exact literal `"malformed JSON body"`**
  string (no parser-provided detail appended) for a discarded/invalid JSON
  parse — matching the real, tested behavior of the pre-existing
  `ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()`, not
  their slightly-stale header comment (the v2 revision's own correction was
  followed here, never reintroduced).
- `BuildSetEntityTrsResponseJson()` only emits `"transform"`/`"changed"` on
  success, returning `BuildGenericErrorResponseJson(errorMessage)` unchanged
  on failure — verified with a dedicated test asserting the failure body has
  no stray `"entity"`/`"transform"`/`"changed"` key.

### `tests/Network/NetworkRoutesTests.cpp`

Added 22 new test cases (11 `ParseSetEntityTrsRequestTests`, 9
`ParseInstantiateLightRequestTests`, 2 `BuildSetEntityTrsResponseJsonTests`),
covering every case the phase doc's "Verification for this phase" section
calls out by name, including all of the v2-added cases:

- All-three-groups-present success case, no-groups-present success case
  (Locked Design Decision #6), and all-three-groups-explicitly-`null`
  success case (the v2-added null-means-absent rule).
- A missing-axis `translation` failure and a not-an-object `translation`
  failure, asserting they share the EXACT SAME error message (confirms the
  doc comment's "one message covers both causes" reading).
- The exact literal `"malformed JSON body"` string for both new parsers.
- A missing `name` failure for both new parsers.
- An unrecognized extra top-level field being silently ignored.
- A `rotation_quaternion` field being silently ignored (never consulted) by
  `ParseSetEntityTrsRequest()`.
- `ParseInstantiateLightRequest()`: a fully valid payload exercising every
  field; minimal-payload defaults (`lightType == ""`, color `(1,1,1)`,
  `illuminanceLux == 100000.0f`, `active == true`, no parent) matching
  `DirectionalLight`'s own component defaults exactly; an unrecognized
  `light_type` value (e.g. `"point"`) is accepted as `valid == true` at this
  layer (semantic rejection is explicitly Phase 2's job); a negative
  `illuminance_lux` failure; a non-boolean `active` failure; a
  not-an-object `color` failure asserting the exact message `"color must be
  an object"`; a non-numeric `color.r` asserting the exact,
  component-specific message `"color.r must be a number"`.
- `BuildSetEntityTrsResponseJson()`: a success case with mixed
  `changed.translation`/`changed.rotation`/`changed.scale` flags, asserting
  every echoed transform field round-trips through
  `nlohmann::json::parse()`; a failure case confirming the body matches
  `BuildGenericErrorResponseJson()` byte-for-byte and contains no stray
  `entity`/`transform`/`changed` key.

All 22 new tests pass. The full pre-existing `NetworkRoutesTests`/
`ParseInstantiatePrimitiveRequestTests`/`ParseDeleteEntityRequestTests`/
`BuildResponseJsonTests`/`ParseGetTextureQueryTests`/
`BuildListTexturesResponseJsonTests` suites (43 tests) still pass unchanged
— no regression.

## Deviations from the strategy document

None of substance. Every validation rule, message string, struct shape, and
function signature was implemented exactly as specified in the v2 revision
of `PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md`. The one
addition beyond the literal text of the plan is the private
`TryParseAllOrNothingXyz()` helper in the `.cpp` file's anonymous namespace
— the phase doc explicitly anticipated and permitted this ("reuse that
exact parsing helper/logic if this file already factored it out as a
private helper; otherwise write the equivalent logic here (and consider
factoring a shared private helper at that point...)"), so this is a
plan-sanctioned implementation choice, not a deviation.

## Verification performed

1. **Fast, targeted compile check** (not a full rebuild) via:
   - `cmake --build build --target CMakeFiles/gte_core.dir/src/Network/NetworkRoutes.cpp.obj`
     — compiled cleanly, zero warnings/errors.
   - `cmake --build build --target tests/CMakeFiles/GreatTamanaEngineTests.dir/Network/NetworkRoutesTests.cpp.obj`
     — compiled cleanly, zero warnings/errors.
2. Since both translation units compiled cleanly in isolation, and this
   phase's own new code has zero Game/ECS/Renderer/bridge dependency (per
   its own scope), a full link of `GreatTamanaEngineTests` was also performed
   as an extra confidence check (not strictly required by "fast compile
   check only", but cheap here since nothing else needed rebuilding) —
   linked successfully, and the FULL test suite was run:
   **1227 tests total, 1226 passed** (the one gap is the pre-existing,
   documented machine-gated smoke test this repository's test suite always
   reports as skipped — see `README.md`'s own "1 pre-existing machine-gated
   smoke test skipped" notes elsewhere in this codebase's history), zero
   failures, zero regressions.
3. Narrowly re-ran just the new/relevant `Network` test suites via
   `--gtest_filter` to directly confirm all 22 new tests plus all 43
   pre-existing `Network`-related tests pass.

## What's next

Phase 2 (`PHASE2_GAME_LEVEL_SET_ENTITY_TRS_AND_INSTANTIATE_LIGHT_APIS.md`)
can now build `Game::SetEntityTrs()`/`Game::InstantiateLight()` — this
phase's new parsers/builders are complete, tested, and ready to be wired up
by Phase 3 (bridge/dispatch) and Phase 4 (HTTP route registration). No open
questions or blockers were found; nothing in `Application.h/.cpp` was
touched (as expected — this phase never needed to).
