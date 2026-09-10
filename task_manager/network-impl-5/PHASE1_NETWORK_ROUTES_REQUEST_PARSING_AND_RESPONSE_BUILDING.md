# PHASE1 — `NetworkRoutes.h/.cpp`: Request Parsing + Response Building

**v2 — revised after a focused double-check pass against the ACTUAL current
`src/Network/NetworkRoutes.h`/`.cpp` source (and `tests/Network/
NetworkRoutesTests.cpp`) found a few claims in the original draft that didn't
quite match the real code, plus a couple of genuinely ambiguous spots that
two different implementers could reasonably fill in two different,
incompatible ways. See the "v2 revision notes" callouts inline below for
exactly what changed and why — nothing in PHASE0's Locked Design Decisions was
touched or relitigated.**

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: nothing (pure JSON/string
logic, exactly like every existing function in this file). Does **not**
depend on Phase 2/3/4 — this phase's own new functions can be written,
compiled, and Tier-1-tested in total isolation, before a single line of
`Game.h`/`EngineCommandBridge.h` is touched.

## Step 1 — The Goal

Add four new pure functions (two parsers, two response builders) to the
EXISTING `src/Network/NetworkRoutes.h/.cpp` — no new file, mirroring
`ParseInstantiatePrimitiveRequest()`/`ParseDeleteEntityRequest()`/
`BuildInstantiatePrimitiveResponseJson()`/`BuildDeleteEntityResponseJson()`'s
own exact shape and validation-order convention:

1. `ParseSetEntityTrsRequest(const std::string& jsonBody)` →
   `ParsedSetEntityTrsRequest`.
2. `ParseInstantiateLightRequest(const std::string& jsonBody)` →
   `ParsedInstantiateLightRequest`.
3. `BuildSetEntityTrsResponseJson(...)` → `std::string`.
4. `instantiate_light`'s SUCCESS response reuses the EXISTING
   `BuildInstantiatePrimitiveResponseJson()` verbatim — see Step 3.4 below
   for why no new builder function is needed for it at all.

## Step 2 — The Situation

- This file is deliberately **Math/Vec3/Quat-free** — every existing parsed
  field is a plain `float`/`bool`/`std::string` (e.g.
  `ParsedInstantiatePrimitiveRequest::worldX/Y/Z`, not a `Vec3`).
  `NetworkServer.cpp` is the one place that later converts these into real
  `Vec3`/`Quat` values (Phase 4 does this for both new routes). This
  campaign's new parsers/builders MUST preserve that same boundary — do not
  `#include "../Math/Vec3.h"` or `"../Math/Quat.h"` in this file.
- `nlohmann::json` is already vendored and already `#include`d at the top of
  `NetworkRoutes.cpp` (`network-impl-3` campaign) — no new dependency work
  of any kind in this phase.
- `ParseInstantiatePrimitiveRequest()`'s own doc comment (already in
  `NetworkRoutes.h`) is the exact validation-order convention to mirror:
  checked in a fixed order, the FIRST failure found is what `errorMessage`
  reports; unrecognized extra JSON fields are silently ignored
  (forward-compatible).
- **[v2 correction — verified against the real `.cpp` AND
  `tests/Network/NetworkRoutesTests.cpp`]** The malformed-JSON error message
  text is the LITERAL, exact string `"malformed JSON body"` — nothing is
  ever appended to it. `NetworkRoutes.h`'s own existing header comment for
  `ParseInstantiatePrimitiveRequest()` currently (mis-)describes this as
  `"malformed JSON body: <parser's own message>"`, but the real
  `ParseJsonNoThrow()`/`ParseInstantiatePrimitiveRequest()` implementation in
  `NetworkRoutes.cpp` never appends anything (`result.errorMessage =
  "malformed JSON body";`, full stop), and
  `tests/Network/NetworkRoutesTests.cpp`'s `MalformedJsonTextFails`/
  `ParseDeleteEntityRequestTests.MalformedJsonFails` both assert the exact
  string `"malformed JSON body"` with nothing appended. **This phase's two
  new parsers must use the literal string `"malformed JSON body"` for this
  case, NOT append any parser-provided detail** — copying the pre-existing
  header comment's slightly-too-specific wording (as the original v1 draft of
  this phase document did) would introduce a real, visible inconsistency
  between the new endpoints' error text and every existing endpoint's. (Fixing
  the pre-existing header comment itself is a one-line, unrelated
  documentation nit outside this campaign's scope — feel free to fix it in
  passing if convenient, but it is not required by this phase.)
- Per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions #1/#2/#4/#9,
  this phase's two new parsers have a meaningfully different validation
  shape than `ParseInstantiatePrimitiveRequest()`'s own "each numeric field
  independently optional, defaulting to 0" rule:
  - `set_entity_trs`'s three optional groups (`translation`/
    `rotation_euler_degrees`/`scale`) are each **all-or-nothing** — if the
    JSON key is present at all, `x`/`y`/`z` must ALL be present and be JSON
    numbers, or the whole request is invalid. This is a genuinely different
    rule from `world_position`'s own "each axis independently optional,
    defaults to 0" rule in the EXISTING function — do not accidentally copy
    that per-axis-optional behavior here.
  - `instantiate_light`'s `world_position`/`color` fields, by contrast, DO
    use the existing per-component-optional-with-a-default convention
    (creation-time defaults, not a mutation — see Locked Design Decision
    rationale in PHASE0), while its `rotation_euler_degrees` field uses the
    SAME all-or-nothing rule as `set_entity_trs`'s own (for internal
    consistency between the two new endpoints' rotation fields).
- `Vec3::One()`/`DirectionalLight`'s own default field values
  (`ECS/Components/DirectionalLight.h`) are `color = (1,1,1)`,
  `illuminanceLux = 100000.0f`, `active = true` — this phase's own default
  values for `ParsedInstantiateLightRequest`'s color/illuminance/active
  fields should match these exactly, so "field omitted entirely" and
  "field explicitly set to the component's own default" produce identical
  behavior. **[v2 — verified directly against the real
  `src/ECS/Components/DirectionalLight.h`: confirmed exact match, no
  correction needed here.]**
- **[v2 addition — resolves a genuine ambiguity the v1 draft left open]**
  What does an EXPLICIT JSON `null` mean for `translation`/
  `rotation_euler_degrees`/`scale` (the three new all-or-nothing groups,
  in EITHER endpoint)? E.g. `{"name":"X","translation":null}`. The v1 draft
  never said, which is a real gap two implementers could fill two different,
  incompatible ways (some readers would assume `null` behaves like "absent,"
  by analogy with the existing `"parent"` field's own
  `contains("parent") && !is_null()` handling; others would let the ordinary
  `is_object()` check reject it as invalid, since `null` is not an object).
  **Locked for this phase: an explicit JSON `null` for `translation`/
  `rotation_euler_degrees`/`scale` is treated EXACTLY the same as the key
  being absent entirely** (the corresponding `hasX` flag stays `false`, no
  validation error) — this mirrors the existing `"parent"` field's own
  null-means-absent convention and is the least surprising choice for a
  caller who serializes "no value" as JSON `null` (a common pattern in many
  JSON-producing client libraries/languages). This rule applies ONLY to the
  three all-or-nothing groups (new in this phase) — it does **not** change
  `world_position`'s or `color`'s existing/mirrored per-component-optional
  behavior (an explicit `"world_position": null` continues to fail with
  `"world_position must be an object"`, exactly matching
  `ParseInstantiatePrimitiveRequest()`'s own pre-existing, unchanged
  behavior for that field — do not "fix" that consistency gap as part of
  this phase; it's out of scope and not part of Locked Design Decisions).

## Step 3 — The Plan

### 3.1 — New struct + parser: `ParsedSetEntityTrsRequest`/`ParseSetEntityTrsRequest()`

Add to `NetworkRoutes.h`, immediately after the existing
`ParsedDeleteEntityRequest`/`ParseDeleteEntityRequest()` declarations:

```cpp
// --- network-impl-5 campaign
// (PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md) - real
// JSON request parsing/response building for the new POST /set_entity_trs
// and POST /instantiate_light endpoints (Phase 4). Same PURE, JSON-only,
// zero-Game/ECS/Renderer/Math dependency discipline as everything above -
// see this file's own header comment.

// Parsed, VALIDATED result of a POST /set_entity_trs request body.
// `valid == false` means `errorMessage` explains exactly why - every OTHER
// field is meaningless in that case. See ParseSetEntityTrsRequest()'s own
// doc comment below for the exact validation rules.
struct ParsedSetEntityTrsRequest {
    bool valid = false;
    std::string errorMessage;
    std::string name;

    // All-or-nothing (network-impl-5's Locked Design Decision #2 -
    // PHASE0_MASTER_STRATEGY.md): hasTranslation is only ever true when
    // "translation" was present (and non-null) AND supplied all of x/y/z as
    // numbers. An explicit JSON null is treated exactly like the key being
    // absent (hasTranslation stays false) - see this file's own Step 2 note.
    bool hasTranslation = false;
    float translationX = 0.0f;
    float translationY = 0.0f;
    float translationZ = 0.0f;

    // Euler degrees, (pitchX, yawY, rollZ) - matches
    // Quat::FromEulerDegrees()'s own parameter order/units exactly. There is
    // NO quaternion input field anywhere in this campaign - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.
    bool hasRotationEulerDegrees = false;
    float rotationPitchXDegrees = 0.0f;
    float rotationYawYDegrees = 0.0f;
    float rotationRollZDegrees = 0.0f;

    bool hasScale = false;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
};

// Parses `jsonBody` (the raw POST body) for POST /set_entity_trs.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. `jsonBody` must parse as valid JSON at all, and the top-level value
//      must be a JSON OBJECT - otherwise "malformed JSON body" (this EXACT
//      string, verbatim - no parser-provided detail appended; see this
//      file's own Step 2 note, and match
//      ParseInstantiatePrimitiveRequest()'s real, tested behavior, not its
//      own slightly-stale header comment) / "request body must be a JSON
//      object".
//   2. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name".
//   3. "translation" is OPTIONAL. If absent entirely, OR present but JSON
//      `null`, hasTranslation stays false and translationX/Y/Z stay at their
//      defaults (meaningless). If present and non-null, it must be a JSON
//      OBJECT and ALL THREE of its "x"/"y"/"z" members must be present and
//      JSON NUMBERS - ANY of them missing or non-numeric is a validation
//      FAILURE: "translation must be an object with numeric x, y, and z
//      fields" (deliberately all-or-nothing - see PHASE0's Locked Design
//      Decision #2 for why this is NOT the same per-axis-optional rule
//      ParseInstantiatePrimitiveRequest()'s own "world_position" field
//      uses; see this file's own Step 2 note for why null is treated as
//      "absent" here specifically, unlike "world_position").
//   4. "rotation_euler_degrees" is OPTIONAL, same all-or-nothing x/y/z rule
//      (including the same null-means-absent treatment) as "translation"
//      above - otherwise "rotation_euler_degrees must be an object with
//      numeric x, y, and z fields". There is no "rotation_quaternion" field
//      recognized anywhere in this function - if a caller sends one, it is
//      silently ignored as an unrecognized extra field (same
//      forward-compatible convention as everything else in this file), NOT
//      an error.
//   5. "scale" is OPTIONAL, same all-or-nothing x/y/z rule (including the
//      same null-means-absent treatment) as "translation" - otherwise
//      "scale must be an object with numeric x, y, and z fields".
// A request with valid == true and every hasX flag false (no
// translation/rotation_euler_degrees/scale key present at all, or all three
// explicitly null) is NOT an error - see PHASE0's Locked Design Decision #6
// (a harmless no-op, doubling as a de-facto "read the current transform"
// query once Phase 2/4 exist).
ParsedSetEntityTrsRequest ParseSetEntityTrsRequest(const std::string& jsonBody);
```

### 3.2 — New struct + parser: `ParsedInstantiateLightRequest`/`ParseInstantiateLightRequest()`

```cpp
// Parsed, VALIDATED result of a POST /instantiate_light request body.
// `valid == false` means `errorMessage` explains exactly why - every OTHER
// field is meaningless in that case.
struct ParsedInstantiateLightRequest {
    bool valid = false;
    std::string errorMessage;

    // "" (absent from the request entirely) means "use the default light
    // type" - Game::InstantiateLight() (Phase 2) is what actually resolves
    // "" (and, case-insensitively, "directional") to a real light kind, and
    // rejects anything else - this function only validates the JSON SHAPE
    // (a string, if present at all), never the semantic value, exactly
    // mirroring ParseInstantiatePrimitiveRequest()'s own "shape" field
    // convention (see that function's own doc comment for the identical
    // reasoning).
    std::string lightType;

    // REQUIRED - see PHASE0's Locked Design Decision #4 (unlike
    // "lightType" above, an empty/missing "name" IS a validation failure
    // here, enforced at THIS layer).
    std::string name;

    // Per-axis-optional, defaulting to 0.0f for any missing axis - CREATION-
    // time semantics, identical convention to
    // ParseInstantiatePrimitiveRequest()'s own "world_position" field (this
    // is deliberately NOT the same all-or-nothing rule
    // ParseSetEntityTrsRequest() above uses for "translation" - see
    // PHASE0_MASTER_STRATEGY.md's Step 2 note on why creation-time fields
    // and mutation-time fields use different optionality rules). An
    // explicit "world_position": null is NOT treated as absent here -
    // it fails with "world_position must be an object", exactly matching
    // ParseInstantiatePrimitiveRequest()'s own real, pre-existing behavior
    // for this same field (the new null-means-absent convention introduced
    // by this phase applies only to the brand-new all-or-nothing groups,
    // never retroactively to this pre-existing field - see this file's own
    // Step 2 note).
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;

    // All-or-nothing, same rule (and same field meaning/units, including the
    // null-means-absent treatment) as ParseSetEntityTrsRequest()'s own
    // "rotation_euler_degrees" above - see that struct's own doc comment.
    // Absent entirely (or explicitly null) -> false (Phase 2's
    // Game::InstantiateLight() then applies the "late-afternoon" default
    // rotation per PHASE0's Locked Design Decision #5, NOT identity).
    bool hasRotationEulerDegrees = false;
    float rotationPitchXDegrees = 0.0f;
    float rotationYawYDegrees = 0.0f;
    float rotationRollZDegrees = 0.0f;

    // Per-component-optional, defaulting to 1.0f for any missing component -
    // CREATION-time semantics matching DirectionalLight::color's own default
    // (Vec3::One()) - see ECS/Components/DirectionalLight.h. Same
    // "explicit null is NOT treated as absent" caveat as "world_position"
    // above (an explicit "color": null fails with "color must be an
    // object" - see below).
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;

    // Optional; defaults to DirectionalLight::illuminanceLux's own default
    // (100000.0f) when absent. If present, MUST be a non-negative JSON
    // number - a negative value is a validation failure ("illuminance_lux
    // must be a non-negative number") since a negative light intensity is
    // physically meaningless and almost certainly a caller mistake worth
    // surfacing loudly rather than silently accepting.
    float illuminanceLux = 100000.0f;

    // Optional bool; defaults to DirectionalLight::active's own default
    // (true) when absent. If present, must be a JSON boolean - otherwise
    // "active must be a boolean".
    bool active = true;

    // Same "absent/null/empty string all mean no parent; any other JSON
    // type is a validation failure" rule as
    // ParseInstantiatePrimitiveRequest()'s own "parent" field - reused
    // verbatim, see that function's own doc comment (point 5) for the
    // exact rule.
    bool hasParent = false;
    std::string parentName;
};

// Parses `jsonBody` (the raw POST body) for POST /instantiate_light.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. Same "must parse as a JSON object" rule as
//      ParseSetEntityTrsRequest() above - including the same EXACT
//      "malformed JSON body" literal string (see this file's own Step 2
//      note; do not append any parser-provided detail).
//   2. "light_type" is OPTIONAL; if present, must be a JSON STRING (may be
//      empty) - otherwise "light_type must be a string". The actual
//      "directional"-or-nothing-else semantic check happens in Phase 2's
//      Game::InstantiateLight(), not here (see ParsedInstantiateLightRequest::lightType's
//      own doc comment above).
//   3. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name" (identical message text
//      to ParseInstantiatePrimitiveRequest()'s own, for consistency).
//   4. "world_position" is OPTIONAL, per-axis-optional-defaults-to-0 - same
//      rule/validation-failure message shape as
//      ParseInstantiatePrimitiveRequest()'s own "world_position" field
//      (point 4 of that function's own doc comment, including its exact
//      "world_position must be an object"/"world_position.x/y/z must be a
//      number" message text and its existing null-is-not-treated-specially
//      behavior) - reuse that exact parsing helper/logic if this file
//      already factored it out as a private helper; otherwise write the
//      equivalent logic here (and consider factoring a shared private
//      helper at that point - see this file's own Step 2 note on
//      premature-abstraction discipline before doing so speculatively).
//   5. "rotation_euler_degrees" is OPTIONAL, all-or-nothing x/y/z rule
//      (including null-means-absent) - identical shape/message convention
//      to ParseSetEntityTrsRequest()'s own field of the same name.
//   6. "color" is OPTIONAL; if present and non-null, must be a JSON OBJECT -
//      otherwise "color must be an object" (mirrors "world_position must be
//      an object" exactly, including that an explicit "color": null is NOT
//      treated as absent - see ParsedInstantiateLightRequest::colorR's own
//      doc comment above). Each of its "r"/"g"/"b" members is itself
//      OPTIONAL (missing -> 1.0f for that component) but if present must be
//      a JSON NUMBER - otherwise the exact message "color.r must be a
//      number" / "color.g must be a number" / "color.b must be a number"
//      (three DISTINCT message strings, one per component that actually
//      failed - mirroring "world_position.x must be a number"'s own
//      per-axis-specific message shape exactly, never one message with a
//      literal "r/g/b" substring in it).
//   7. "illuminance_lux" is OPTIONAL; if present, must be a JSON NUMBER
//      that is >= 0 - otherwise "illuminance_lux must be a non-negative
//      number".
//   8. "active" is OPTIONAL; if present, must be a JSON BOOLEAN - otherwise
//      "active must be a boolean".
//   9. "parent" is OPTIONAL - identical rule to
//      ParseInstantiatePrimitiveRequest()'s own "parent" field (point 5 of
//      that function's own doc comment) - reuse that exact logic.
// Unrecognized extra JSON fields are silently ignored, same
// forward-compatible convention as everywhere else in this file.
ParsedInstantiateLightRequest ParseInstantiateLightRequest(const std::string& jsonBody);
```

### 3.3 — Response builder: `TransformSnapshotView` + `BuildSetEntityTrsResponseJson()`

`set_entity_trs`'s response needs to echo back up to 13 plain floats (a
position, an Euler-degrees rotation, a raw quaternion, AND a scale) plus 3
"changed" bools and an entity handle. A flat 15+ parameter function would be
its own readability hazard - group the 13 transform floats into ONE small,
plain, Math-free struct this file owns (mirroring `TextureListEntryView`'s
own already-established precedent later in this same file - its own doc
comment, "a plain, scalars-only struct THIS file owns", is the exact
philosophy to copy here too):

```cpp
// A plain, Math/Vec3/Quat-free snapshot of one entity's resulting local
// transform, for BuildSetEntityTrsResponseJson()'s own response body below.
// NetworkServer.cpp (Phase 4) is the one place that copies a real
// SetEntityTrsOutcome's Vec3/Quat fields (src/Game/EngineCommandResults.h,
// Phase 2) into this struct, one field at a time - same "a struct crossing
// a layer boundary is never accepted directly here" boundary
// TextureListEntryView's own doc comment already establishes for this file.
// The rotation is intentionally echoed in BOTH representations (Euler
// degrees for human readability, raw quaternion for a caller that wants the
// exact, non-lossy value) - this is READ-ONLY OUTPUT, so it creates none of
// the input ambiguity PHASE0's Locked Design Decision #1 rules out for
// INPUT.
//
// NOTE ON NAMING: this struct's field is "positionX/Y/Z" (matching
// Transform::position's own field name, ECS/Components/Transform.h) even
// though the REQUEST'S corresponding JSON key/struct field is
// "translation"/"translationX/Y/Z" (ParsedSetEntityTrsRequest above). This
// asymmetry is deliberate, not an inconsistency to "fix": the request key
// describes an ACTION ("translate this entity by/to..."), while the
// response key describes the entity's actual CURRENT STATE (its Transform's
// real "position" field) - matching each one's own most natural name in its
// own context, exactly like the request's "world_position" (an
// instantiate-time placement) and this struct's own "positionX/Y/Z" (a
// read-back of live component state) already differ in
// ParsedInstantiatePrimitiveRequest/DirectionalLight elsewhere in this
// codebase. Do not rename either side to "fix" this - it would only make
// one of the two contexts read less naturally.
struct TransformSnapshotView {
    float positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;
    float rotationEulerXDegrees = 0.0f, rotationEulerYDegrees = 0.0f, rotationEulerZDegrees = 0.0f;
    float rotationQuatX = 0.0f, rotationQuatY = 0.0f, rotationQuatZ = 0.0f, rotationQuatW = 1.0f;
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
};

// Builds the response body for POST /set_entity_trs.
// Success shape:
//   {"success":true,"entity":{"index":<uint>,"generation":<uint>},
//    "changed":{"translation":<bool>,"rotation":<bool>,"scale":<bool>},
//    "transform":{
//      "position":{"x":..,"y":..,"z":..},
//      "rotation_euler_degrees":{"x":..,"y":..,"z":..},
//      "rotation_quaternion":{"x":..,"y":..,"z":..,"w":..},
//      "scale":{"x":..,"y":..,"z":..}}}
// Failure shape: identical to BuildGenericErrorResponseJson() below -
// {"success":false,"error":"<errorMessage>"} - via an internal `if
// (!success) { return BuildGenericErrorResponseJson(errorMessage); }` early
// return, EXACTLY mirroring BuildInstantiatePrimitiveResponseJson()'s own
// real, verified implementation (confirmed directly against
// NetworkRoutes.cpp - not just its header comment) - never a second,
// parallel builder function. On failure, entityIndex/entityGeneration/
// translationChanged/rotationChanged/scaleChanged/resultingTransform are
// all ignored - only errorMessage is read. This function is only ever
// called by /set_entity_trs's own SUCCESS path (Phase 4); Phase 4's route
// handler calls BuildGenericErrorResponseJson() DIRECTLY for its own
// failure path instead (the exact same split
// BuildInstantiatePrimitiveResponseJson()'s own callers already use) -
// this function's own internal `!success` branch above exists purely so
// its signature/behavior matches its sibling builders' shape exactly, not
// because Phase 4 is expected to rely on it for the failure case.
std::string BuildSetEntityTrsResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration,
    bool translationChanged, bool rotationChanged, bool scaleChanged,
    const TransformSnapshotView& resultingTransform);
```

Only emit the `"transform"`/`"changed"` JSON keys when `success == true` —
confirmed directly against `BuildDeleteEntityResponseJson()`'s real
implementation in `NetworkRoutes.cpp` (not just inferred from its header
comment): on failure it returns `BuildGenericErrorResponseJson(errorMessage)`
immediately, before touching `entity`/anything else, so the failure body
never contains a stray `"entity"`/`"transform"`/`"changed"` key at all. Follow
that exact same shape here.

### 3.4 — `instantiate_light`'s success response: reuse `BuildInstantiatePrimitiveResponseJson()` verbatim

**No new builder function for `instantiate_light` is needed at all.** Its
success/failure JSON shape (`{"success":...,"entity":{...},"name":...,
"parent_requested_but_not_found":...,"requested_parent_name":...}`) is
BYTE-FOR-BYTE identical to `instantiate_primitive`'s own — both spawn a
named, possibly-parented entity and report exactly those same five pieces
of information. Add a short comment immediately above
`BuildInstantiatePrimitiveResponseJson()`'s existing declaration in
`NetworkRoutes.h` noting this reuse explicitly (e.g. "network-impl-5
campaign: POST /instantiate_light's route handler (Phase 4) calls this
SAME function for its own success/failure response - the two endpoints'
response shapes are intentionally identical, so no second builder was
written"), so a future reader isn't confused about why `instantiate_light`
has no `BuildInstantiateLightResponseJson()` of its own anywhere in this
file. Do NOT rename `BuildInstantiatePrimitiveResponseJson()` to something
more generic as part of this campaign - an unrelated rename is scope creep;
leave the existing name exactly as-is and just add the explanatory comment.

## Verification for this phase

- Fast compile check.
- New test cases in `tests/Network/NetworkRoutesTests.cpp` (existing file -
  extend it, do not create a new one) covering, at minimum:
  - For `ParseSetEntityTrsRequest()`: a fully valid request with all three
    groups present; a valid request with NONE of the three groups present
    (still `valid == true`, per Locked Design Decision #6); a valid request
    where all three groups are explicitly JSON `null` (still `valid ==
    true`, same as absent — **[v2 addition]**, exercises this phase's own
    new null-means-absent rule); a request with a `translation` object
    missing one axis (`valid == false`); a request where `translation` is
    present but not even a JSON object, e.g. a string or number (`valid ==
    false`, same error message as the missing-axis case — **[v2 addition]**,
    since the doc comment states one message covers both causes); a
    malformed-JSON body asserting the EXACT string `"malformed JSON body"`
    (**[v2 addition]** — this is what would have caught the v1 draft's own
    message-text mistake before it ever reached production code); a missing
    `name`; an unrecognized extra top-level field is silently ignored
    (**[v2 addition]**, mirrors `ParseInstantiatePrimitiveRequestTests.
    ExtraUnrecognizedFieldIsIgnored`); a `rotation_quaternion` field is
    silently ignored rather than rejected or consulted (**[v2 addition]** —
    directly exercises this function's own documented "no such field exists"
    behavior from Locked Design Decision #1).
  - For `ParseInstantiateLightRequest()`: a fully valid request; a missing
    `name` (`valid == false`); an unrecognized `light_type` value is **NOT**
    rejected here (this function only validates JSON shape, not the
    semantic light-type value - assert `valid == true` for e.g. `light_type:
    "point"`, since that specific rejection is Phase 2's job, not this
    function's - a test asserting the OPPOSITE would be actively wrong per
    this function's own documented contract); a negative `illuminance_lux`
    (`valid == false`); an `active` field that isn't a boolean (`valid ==
    false`); a `color` field that is present but not a JSON object, e.g. a
    string (`valid == false`, message `"color must be an object"` — **[v2
    addition]**, closes a gap the v1 draft's own doc comment left this exact
    message text unspecified for); a `color` object with a non-numeric `r`
    asserting the exact message `"color.r must be a number"` (**[v2
    addition]**, confirms the three-distinct-messages reading of the doc
    comment rather than one combined "r/g/b" string).
  - Also cover `BuildSetEntityTrsResponseJson()`'s own output with a
    round-trip `nlohmann::json::parse()` assertion on a couple of
    representative cases (success with all three `changed` flags mixed
    true/false; failure — confirming the failure body has no stray
    `"entity"`/`"transform"`/`"changed"` key, mirroring
    `BuildDeleteEntityResponseJson()`'s own real, verified failure shape).
- Write `PHASE1_COMPLETION_REPORT.md` in this same folder, `git add`/`git
  commit`.
