# PHASE1 — COMPLETION REPORT: JSON Dependency + Request Parsing / Response Building

Parent: `PHASE0_MASTER_STRATEGY.md`
Phase document: `PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md`
Branch: `feature/network-impl`

## Summary

Implemented Phase 1 of the `network-impl-3` campaign exactly as specified:
vendored `nlohmann/json`'s single-header `json.hpp` via a new
`cmake/FetchJson.cmake`, and added pure, Tier-1-tested JSON request-parsing +
response-building functions to the existing `src/Network/NetworkRoutes.h/.cpp`.
Zero engine/ECS dependency anywhere in this phase — everything here compiles
and is fully testable in total isolation, exactly as the phase's own "Goal"
section required.

## What was done

### 1. `cmake/FetchJson.cmake` (new file)

Mirrors `cmake/FetchHttplib.cmake` file-for-file in structure and
header-comment style, adapted for `nlohmann/json`:

- Downloads `single_include/nlohmann/json.hpp` directly from GitHub's raw
  content endpoint (no ZIP/archive extraction — single file, same mechanism
  as `httplib.h`).
- `NLOHMANN_JSON_RELEASE_TAG` cache var (default `"latest"`, resolved via the
  GitHub releases API — `_json_resolve_tag()`/`_json_github_get_json()`,
  functions renamed with a `_json_` prefix specifically to avoid symbol
  collision with `FetchHttplib.cmake`'s own `_httplib_...` functions, since
  both files get `include()`-d into the same top-level `CMakeLists.txt`).
- `NLOHMANN_JSON_FORCE_REDOWNLOAD` option, mirroring `HTTPLIB_FORCE_REDOWNLOAD`.
- Staged layout: `third_party/json/nlohmann/json.hpp` +
  `third_party/json/.gte_fetched_ref` — the extra `nlohmann/` subfolder is
  what makes `#include <nlohmann/json.hpp>` resolve correctly with a plain
  `target_include_directories(... "${CMAKE_SOURCE_DIR}/third_party/json")`.
- Defines one `nlohmann_json` `INTERFACE` target (header-only, no platform
  library link needed — unlike `httplib`'s `ws2_32`/`crypt32`).
- `fetch_json()` public entry function, idempotent, same shape as
  `fetch_httplib()`.

### 2. `CMakeLists.txt` wiring

- Added `include(FetchJson)` right next to `include(FetchHttplib)`.
- Added `fetch_json()` right next to `fetch_httplib()`, with its own doc
  comment.
- Added `nlohmann_json` to the existing
  `target_link_libraries(gte_core PUBLIC SDL3::SDL3 volk vma stb_image
  stb_image_write KTX::ktx httplib ...)` call, alongside `httplib` — `PUBLIC`
  so `GreatTamanaEngineTests` (which links `gte_core`) picks up
  `nlohmann/json.hpp`'s include directory transitively, exactly like it
  already does for `httplib.h`.

### 3. `.gitignore`

Added a new `/third_party/json/` entry (with its own comment pointing at
`cmake/FetchJson.cmake`), right after the existing `/third_party/httplib/`
entry — confirmed the existing convention lists each fetched dependency's
folder individually rather than a generic wildcard, as the phase document's
own "second-iteration review" note called out.

### 4. `src/Network/NetworkRoutes.h` (extended)

Added, under a `// --- network-impl-3 campaign` banner:

- `ParsedInstantiatePrimitiveRequest` (struct) + `ParseInstantiatePrimitiveRequest()`
- `ParsedDeleteEntityRequest` (struct) + `ParseDeleteEntityRequest()`
- `BuildInstantiatePrimitiveResponseJson()`
- `BuildDeleteEntityResponseJson()`
- `BuildGenericErrorResponseJson()`

Every validation rule from the phase document's own doc-comment spec is
documented verbatim above each declaration. `#include <cstdint>` was added
for `std::uint32_t` (the header previously only included `<string>`).

### 5. `src/Network/NetworkRoutes.cpp` (extended)

`#include <nlohmann/json.hpp>` only in this `.cpp` file — the header (`.h`)
stays free of the third-party type, matching this codebase's existing
"keep third-party types out of headers when possible" convention (e.g.
`NetworkServer.h`'s own forward-declared `httplib::Server`).

- `ParseJsonNoThrow()` (anonymous-namespace helper) calls
  `nlohmann::json::parse(jsonBody, nullptr, /*allow_exceptions=*/false)` —
  verified against the actually-vendored `v3.12.0` header that this
  non-throwing overload exists and that a malformed body becomes a plain
  `.is_discarded()` result rather than a thrown exception.
- `ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()` implement
  every validation rule from the header's doc comments, in the exact stated
  order (malformed JSON → not-an-object → missing/wrong-typed/empty
  required fields → optional-field validation).
- For `world_position`'s per-axis extraction, each axis is explicitly checked
  with `.contains("x") && !j["x"].is_number()` BEFORE reading the value (never
  a bare `.value("x", 0.0f)`-only call), so a present-but-wrong-typed axis is
  correctly reported as a validation failure rather than silently coerced.
- The three response builders are implemented via real `nlohmann::json`
  object construction + `.dump()` (correct string escaping) rather than
  hand-formatted string concatenation — this is the concrete fix for the
  "hand-formatting doesn't generalize to arbitrary caller-supplied names"
  problem the phase document's "Situation" section called out.

### 6. `tests/Network/NetworkRoutesTests.cpp` (extended)

Added test coverage for every case enumerated in the phase document's own
"3.5 — Tests" section:

- `ParseInstantiatePrimitiveRequestTests` (18 tests): fully valid payload;
  `world_position` absent; partially-present axes; `parent` absent/`null`/
  empty-string (all → `hasParent == false`); `parent` non-empty string;
  missing/non-string/empty `shape`; missing/empty `name`; `world_position`
  not an object; `world_position.x` not a number; `parent` wrong type;
  malformed JSON text; top-level JSON array; top-level JSON number; an extra
  unrecognized field (ignored, request still parses).
- `ParseDeleteEntityRequestTests` (4 tests): valid payload; missing `name`;
  empty-string `name`; malformed JSON; non-object top-level value.
- `BuildResponseJsonTests` (8 tests): exact success/failure shapes for both
  new endpoints' response builders, `BuildGenericErrorResponseJson()`'s own
  exact shape, and two explicit "round-trips correctly through real JSON
  parsing" regression tests using a name/error message containing a literal
  `"` and `\` — the concrete proof the hand-formatting problem is actually
  solved, not just asserted in a comment.

All new tests live under new, clearly-named `TEST`/`TEST_P` suites, added
alongside (never replacing) the existing `NetworkRoutesTests` suite.

## Verification performed

1. **Fresh configure with internet access** (`cmake -S . -B build`):
   `nlohmann/json: resolving 'latest' via releases API` →
   `resolved ref 'v3.12.0'` → downloaded and staged to
   `third_party/json/nlohmann/json.hpp` without errors. Every other
   already-fetched dependency (SDL3, Vulkan, VMA, stb, httplib, KTX, saba,
   glm, imgui, imguizmo, GoogleTest) was already present and was correctly
   skipped — this campaign's new fetch step did not disturb any of them.
2. **Fast compile check** (`cmake --build build --target
   GreatTamanaEngineTests --config Debug`): full clean build of `gte_core` +
   `GreatTamanaEngineTests` succeeded with zero errors/warnings related to
   this change (270/270 build steps succeeded, including
   `src/Network/NetworkRoutes.cpp.obj` and
   `tests/.../Network/NetworkRoutesTests.cpp.obj`). No other translation
   unit broke.
3. **Targeted test run** (`ctest -C Debug -R
   "NetworkRoutes|ParseInstantiatePrimitiveRequest|ParseDeleteEntityRequest|BuildResponseJson"
   --output-on-failure`, run from `build/`): **43/43 tests passed** (0.06s
   average per test) — every pre-existing `NetworkRoutesTests` case plus
   every new case added in this phase.

No full build or full regression (`ctest` over the entire suite) was run,
per this campaign's workflow rules — only this phase's own fast compile
check + targeted test filter, as instructed.

## Deviations from the phase document

None. Every locked decision, file, and validation rule was implemented
exactly as specified. One incidental correction made along the way: while
editing `.gitignore` a duplicate `/third_party/httplib/` line was introduced
by an intermediate edit and immediately caught and removed before finalizing
— the committed `.gitignore` has exactly one entry per fetched dependency,
matching the pre-existing convention.

## Next phase

`PHASE2_ECS_ENTITY_LOOKUP_AND_UNIQUE_NAMING_UTILITIES.md` — new
`src/ECS/EntityQuery.h/.cpp` (`FindEntityByName`/`IsEntityNameInUse`/
`MakeUniqueEntityName`) plus `PrimitiveMeshGenerator::TryParsePrimitiveTypeName()`.
Pure ECS logic, Tier-1-tested, zero networking dependency — nothing in this
phase's own new code (`ParseInstantiatePrimitiveRequest()` et al.) needs any
changes for Phase 2 to build on top of it.
