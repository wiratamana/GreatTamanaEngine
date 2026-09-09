# PHASE1 — JSON Dependency + Request Parsing / Response Building

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first — locked decisions #1 and #4
are directly load-bearing for this phase).

## Step 1 — The Goal

Give the engine the ability to (a) **parse** an arbitrary, possibly-malformed
JSON POST request body into a plain, validated C++ struct, and (b) **build**
a correctly-escaped JSON response body for the two new endpoints — all as
pure, dependency-free, Tier-1-tested functions with **zero** knowledge of
httplib, sockets, `Registry`, `Game`, or `Renderer`. By the end of this phase,
nothing about networking or the ECS has changed yet — this is purely "the
engine now knows how to turn JSON text into validated data, and back."

## Step 2 — The Situation

- No JSON library exists anywhere in this repository today (confirmed by
  `search_in_dir` across `src/`, `third_party/`, `cmake/`). `src/Network/NetworkRoutes.h/.cpp`'s
  existing `BuildCaptureJsonBody()` hand-formats a JSON string with no
  escaping — safe today only because it carries just integers + base64 text.
  That trick does not extend to arbitrary caller-supplied entity/parent names
  (Phase 5's actual payload), which can contain `"`/`\`/control characters
  that MUST be escaped to produce valid JSON.
- `cmake/FetchHttplib.cmake` is the exact template to mirror: a single-header,
  header-only C++ library, fetched straight from GitHub (no submodule, no
  system package), staged into a gitignored `third_party/<name>/` folder,
  exposed as one INTERFACE CMake target other targets link against.
- **CONFIRMED (second-iteration review): `.gitignore` does NOT exclude
  `third_party/` generically** — it lists each fetched dependency's folder
  individually (`/third_party/httplib/`, `/third_party/imgui/`, etc. — see
  3.2 below for the exact new line this phase must add for
  `/third_party/json/`).

## Step 3 — The Plan

### 3.1 — `cmake/FetchJson.cmake` (new file)

Mirror `cmake/FetchHttplib.cmake` file-for-file in structure and header-comment
style (this codebase's established convention — see also `FetchSTB.cmake`),
adapted for `nlohmann/json`:

- **Library**: [nlohmann/json](https://github.com/nlohmann/json) — MIT
  licensed, the standard modern-C++ JSON library. Its GitHub repository
  commits a ready-to-use, fully self-contained single header at
  `single_include/nlohmann/json.hpp` (this is nlohmann/json's own documented
  "amalgamated header" distribution mechanism — verify this path is still
  correct against the actual repository at implementation time, the same
  defensive verification `NetworkServer.cpp`'s own header comment already
  models: *"API verified directly against the actually-vendored ... before
  writing this file"*).
- **Download mechanism**: identical to `FetchHttplib.cmake`'s own
  `_httplib_download_and_stage()` — a direct `file(DOWNLOAD ...)` of
  `https://raw.githubusercontent.com/nlohmann/json/<ref>/single_include/nlohmann/json.hpp`
  (no ZIP/archive extraction needed, single file). Sanity-check the downloaded
  content actually looks like the real header (e.g. `string(FIND "${_content}" "nlohmann" _found)`
  on the first ~64-256 bytes) before trusting it, exactly mirroring
  `_httplib_download_and_stage()`'s own "a bad ref yields a 404 page with HTTP
  200 in some environments" defensive check.
- **Staged layout** (so `#include <nlohmann/json.hpp>` resolves correctly with
  a plain include-directory add):
  ```
  third_party/json/nlohmann/json.hpp
  third_party/json/.gte_fetched_ref   <- plain text, the resolved ref actually staged
  ```
  (Note the extra `nlohmann/` subfolder under `third_party/json/` — this is
  what makes `target_include_directories(... "${CMAKE_SOURCE_DIR}/third_party/json")`
  + `#include <nlohmann/json.hpp>` work, mirroring exactly how
  `third_party/httplib/httplib.h` + `target_include_directories(...
  "${CMAKE_SOURCE_DIR}/third_party/httplib")` + `#include <httplib.h>` works
  today.)
- **Tag resolution**: `NLOHMANN_JSON_RELEASE_TAG` cache var, default `"latest"`,
  resolved via the GitHub releases API (`https://api.github.com/repos/nlohmann/json/releases/latest`,
  `tag_name` field) — copy `_httplib_resolve_tag()`/`_httplib_github_get_json()`
  verbatim in structure, renamed to a `_json_...` prefix to avoid symbol
  collision with the httplib module (both files get `include()`-d into the
  same top-level `CMakeLists.txt`, so function names across the two files
  must not collide — check this explicitly since both are plain CMake
  functions, not scoped).
- **Force-redownload switch**: `NLOHMANN_JSON_FORCE_REDOWNLOAD` option,
  mirroring `HTTPLIB_FORCE_REDOWNLOAD`.
- **Defined target**: `nlohmann_json` — an `INTERFACE` library,
  `target_include_directories(nlohmann_json INTERFACE "${CMAKE_SOURCE_DIR}/third_party/json")`.
  Unlike `httplib` (which needs `ws2_32`/`crypt32` linked), `nlohmann/json` is
  header-only pure C++ with **no** platform library dependency — do not add
  any `target_link_libraries()` call for it.
- **Public entry function**: `fetch_json()` (mirrors `fetch_httplib()`) —
  ensures the header is staged, then defines the target if not already
  defined (idempotent, safe to call multiple times / across a re-configure).

### 3.2 — Wire it into the top-level `CMakeLists.txt`

- Add `include(FetchJson)` right next to the existing `include(FetchHttplib)`
  line (`list(APPEND CMAKE_MODULE_PATH ...)` block, near the top of the file).
- Add `fetch_json()` right next to the existing `fetch_httplib()` call.
- Find wherever `gte_core` currently links against the `httplib` target
  (`target_link_libraries(gte_core ... httplib ...)`, or wherever it's
  actually consumed — `grep`/`search_in_dir` for `httplib` in `CMakeLists.txt`
  to find the exact line) and add `nlohmann_json` alongside it in that SAME
  `target_link_libraries(...)` call, so any `src/Network/*.cpp` translation
  unit can `#include <nlohmann/json.hpp>` directly, exactly the same way it
  already can `#include <httplib.h>`.
- **CONFIRMED (second-iteration review): `.gitignore` does NOT use a generic
  `/third_party/*` wildcard** - it lists each fetched dependency's folder
  individually (`/third_party/httplib/`, `/third_party/imgui/`,
  `/third_party/stb/`, etc. - see the existing "cpp-httplib artifacts fetched
  by cmake/FetchHttplib.cmake" entry to copy the comment style from). Add a
  new, equivalent `/third_party/json/` line (with its own one-line comment
  pointing at `cmake/FetchJson.cmake`) - this is REQUIRED, not conditional on
  what the existing pattern turns out to be.

### 3.3 — Extend `src/Network/NetworkRoutes.h` (existing file — add, don't remove anything)

Add these new declarations, grouped under a clear `// --- network-impl-3
campaign` comment banner (matching this file's existing per-campaign comment
style):

```cpp
// Parsed, VALIDATED result of a POST /instantiate_primitive request body.
// `valid == false` means `errorMessage` explains exactly why (malformed JSON
// text itself, or a missing/wrong-typed/empty required field) - every OTHER
// field is meaningless in that case. See ParseInstantiatePrimitiveRequest()'s
// own doc comment below for the exact validation rules.
struct ParsedInstantiatePrimitiveRequest {
    bool valid = false;
    std::string errorMessage;
    std::string shape;
    std::string name;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    bool hasParent = false;
    std::string parentName; // meaningful only when hasParent is true
};

// Parses `jsonBody` (the raw POST body) for POST /instantiate_primitive.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. `jsonBody` must parse as valid JSON at all, and the top-level value
//      must be a JSON OBJECT (not an array/string/number/etc) - otherwise
//      "malformed JSON body: <parser's own message>" / "request body must be
//      a JSON object".
//   2. "shape" must be present, a JSON STRING, and non-empty after parsing -
//      otherwise "missing or invalid required field: shape". This function
//      does NOT itself validate the shape NAME is a recognized PrimitiveType
//      (cube/sphere/capsule/cone/plane) - that is
//      PrimitiveMeshGenerator::TryParsePrimitiveTypeName()'s job (Phase 2),
//      called later by Game::InstantiatePrimitive() (Phase 3). This function
//      only validates the JSON SHAPE of the request, never its semantic
//      meaning - keeps this function's own test suite independent of
//      PrimitiveType ever gaining/losing a value.
//   3. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name".
//   4. "world_position" is OPTIONAL. If absent entirely, worldX/Y/Z all
//      default to 0.0f. If present, it must be a JSON OBJECT; each of its
//      "x"/"y"/"z" members is itself OPTIONAL (missing -> 0.0f for that axis)
//      but if present must be a JSON NUMBER (otherwise "world_position.x/y/z
//      must be a number").
//   5. "parent" is OPTIONAL. Absent entirely, JSON null, OR an empty string
//      all mean "no parent requested" (hasParent = false, parentName left
//      empty). Any other JSON STRING means hasParent = true, parentName = that
//      string. Any other JSON type (number/bool/object/array) for "parent" is
//      a validation failure: "parent must be a string or null".
// Unrecognized extra JSON fields are silently ignored (forward-compatible -
// a future client sending an extra field never breaks an older engine build).
ParsedInstantiatePrimitiveRequest ParseInstantiatePrimitiveRequest(const std::string& jsonBody);

// Parsed, VALIDATED result of a POST /delete_entity request body:
// `{"name": "..."}`. Same "valid == false means errorMessage explains why"
// contract as above. Validation: jsonBody must parse as a JSON object;
// "name" must be present, a JSON string, and non-empty - otherwise "missing
// or invalid required field: name".
struct ParsedDeleteEntityRequest {
    bool valid = false;
    std::string errorMessage;
    std::string name;
};
ParsedDeleteEntityRequest ParseDeleteEntityRequest(const std::string& jsonBody);

// Response-JSON builders for the two new endpoints (Phase 5's actual route
// handlers call these). Deliberately take only PLAIN SCALAR parameters -
// never a Game-layer/EngineCommandBridge struct type - so this file keeps its
// existing "pure, httplib-independent, and now also completely Game/ECS-
// independent" contract from its own file header comment. Built via
// nlohmann::json (correct string escaping) rather than hand-formatted like
// BuildCaptureJsonBody() above, because an entity/parent NAME - unlike a
// base64 image or a plain integer - can legitimately contain characters that
// need real JSON-string escaping (a quote, a backslash, ...).
//
// Success shape:
//   {"success":true,"entity":{"index":<uint>,"generation":<uint>},
//    "name":"<resolvedName>",
//    "parent_requested_but_not_found":<bool>,
//    "requested_parent_name":"<...>"}   (only meaningful/non-empty when the
//                                        previous field is true)
// Failure shape (BuildGenericErrorResponseJson below): {"success":false,"error":"<message>"}
std::string BuildInstantiatePrimitiveResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& resolvedName,
    bool parentRequestedButNotFound, const std::string& requestedParentName);

// Success shape: {"success":true,"entity":{"index":<uint>,"generation":<uint>}}
// Failure shape: identical to BuildGenericErrorResponseJson() below.
std::string BuildDeleteEntityResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t deletedEntityIndex, std::uint32_t deletedEntityGeneration);

// Shared failure-shape builder used by BOTH new endpoints AND any future one
// (malformed JSON, bridge unavailable/busy, timeout - see Phase 5):
// {"success":false,"error":"<errorMessage>"}
std::string BuildGenericErrorResponseJson(const std::string& errorMessage);
```

Remember to add `#include <cstdint>` to this header for `std::uint32_t` (it
currently only includes `<string>`).

### 3.4 — Implement in `src/Network/NetworkRoutes.cpp`

`#include <nlohmann/json.hpp>` at the top of this `.cpp` file only (never in
the `.h` — keep the header itself free of the third-party type, matching this
codebase's general "keep third-party types out of headers when possible"
instinct, e.g. `NetworkServer.h`'s own forward-declare-`httplib::Server`
precedent). Use `nlohmann::json::parse(jsonBody, /*cb*/nullptr,
/*allow_exceptions*/false)` (the non-throwing overload — verify this exact
overload's name/signature against the actually-vendored `json.hpp` before
writing the call, same defensive-verification discipline as every other
third-party integration in this codebase) so a malformed body is a plain
`.is_discarded()` check, never a thrown/caught exception in a route-adjacent
pure function. Implement every validation rule from the doc comments above
literally and in the stated order.

For `world_position`'s per-axis numeric extraction, use
`nlohmann::json::value("x", 0.0f)`-style safe accessors ONLY where the axis is
allowed to be absent — but still explicitly re-check `.contains("x") &&
!j["x"].is_number()` first to produce the required "must be a number" error
for a present-but-wrong-typed value (a bare `.value("x", 0.0f)` call would
silently coerce/ignore a wrong type in some nlohmann/json configurations
rather than reporting the validation failure this spec requires — verify the
exact behavior against the real vendored version rather than assuming).

### 3.5 — Tests: extend `tests/Network/NetworkRoutesTests.cpp` (existing file)

Add a new, clearly-separated test section for these new functions (this file
already exists and is already wired into `tests/CMakeLists.txt` — no build
system change needed here). Cover, at minimum:

- **`ParseInstantiatePrimitiveRequest`**: fully valid payload (all fields
  present); `world_position` entirely absent (defaults to `0,0,0`);
  `world_position` present but only some of `x`/`y`/`z` present (missing axes
  default to `0`); `parent` absent; `parent` explicit JSON `null`; `parent` an
  empty string `""` (all three of these must yield `hasParent == false`);
  `parent` a non-empty string (`hasParent == true`); missing `shape`; `shape`
  present but not a string (e.g. a number); empty-string `shape`; missing
  `name`; empty-string `name`; `world_position` present but not an object (e.g.
  a string); `world_position.x` present but not a number; malformed JSON text
  entirely (e.g. `"{not valid json"`); top-level JSON value that parses but
  isn't an object (e.g. `"[]"` or `"42"`); an extra, unrecognized field
  present alongside valid required fields (must still parse successfully,
  ignoring it).
- **`ParseDeleteEntityRequest`**: valid `{"name":"X"}`; missing `name`;
  empty-string `name`; malformed JSON; non-object top-level value.
- **`BuildInstantiatePrimitiveResponseJson`/`BuildDeleteEntityResponseJson`/`BuildGenericErrorResponseJson`**:
  assert the exact expected JSON text for a success case and a failure case
  each, AND a case where `resolvedName`/`errorMessage` contains a character
  that requires JSON escaping (e.g. a name literally containing a `"` — assert
  the OUTPUT is still valid, parseable JSON with the original string
  recoverable after round-tripping it back through `nlohmann::json::parse()`
  in the test itself — this is the concrete regression test proving the
  "hand-formatting doesn't generalize to arbitrary names" problem Step 2 above
  called out is actually solved, not just asserted in a comment).

## Verification for this phase

- `cmake -S . -B build` (fresh configure) actually downloads and stages
  `third_party/json/nlohmann/json.hpp` without errors.
- A fast compile check of `gte_core`/`GreatTamanaEngineTests` succeeds
  (`cmake --build build` — no full rebuild-from-scratch required if only this
  phase's files changed, but confirm no other translation unit broke).
- Run just this phase's new/changed tests (e.g. `ctest -C Debug -R
  NetworkRoutes --output-on-failure`, or the equivalent gtest filter against
  the built test executable directly) and confirm every new case passes.
- Write a `PHASE1_COMPLETION_REPORT.md` in this same folder (mirrors
  `network-impl-1`/`network-impl-2`'s own `PHASEn_COMPLETION_REPORT.md`
  convention) summarizing what was verified, then `git add`/`git commit`.
