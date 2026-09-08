# PHASE3 — `src/Scene/` Module: `SceneDocument` + the `.gtscene` Text Format (v2)

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`.
Depends on PHASE2 (`PrimitiveSource`, `PrimitiveType` already available).
PHASE4 depends on this phase's `SceneDocument`/`SerializeSceneDocument()`/
`DeserializeSceneDocument()`.

**Revision note (v2):** the second-iteration audit (see `PHASE0_MASTER_STRATEGY.md`)
found v1's `assetGuid=` validation rule was over-strict and mis-scoped: it
rejected the WHOLE file if any `assetGuid=` line parsed to `Guid::Invalid()`,
regardless of that object's `kind` — which would (a) wrongly fail an
otherwise-valid `Primitive` record that happened to carry a stray/leftover
`assetGuid=` line (a field that is only ever meaningful for `Asset`), and (b)
did nothing to catch an `Asset` record that simply omits `assetGuid=`
entirely, which legitimately (per the "absent isn't malformed" rule) keeps
`SceneObjectRecord::assetGuid` at its default — which IS `Guid::Invalid()` —
silently producing an unresolvable `Asset` record instead of failing loudly
at parse time. Section 3.2's format spec, section 3.3's implementation
notes, and section 3.5's test list below are revised accordingly; everything
else in this phase is unchanged from v1.

## Step 1: The Goal (Where are we going?)

Create a brand-new, always-compiled, engine-data-plain module,
`src/Scene/`, mirroring how `src/Physics/` and `src/Jobs/` were bootstrapped
as fresh top-level modules (per PHASE0's Design Decision #4). This phase
delivers exactly two things, both **zero ECS/Renderer/filesystem
dependency**, fully Tier-1-testable:

1. `SceneDocument.h` — the plain-data, in-memory shape of "a serializable
   scene" (a flat list of object records — never a real ECS `Registry`).
2. `SceneTextFormat.h/.cpp` — pure `SceneDocument ⇄ std::string` conversion:
   `SerializeSceneDocument()` and `DeserializeSceneDocument()`. This is the
   actual `*.gtscene` file FORMAT definition — a small, hand-rolled,
   line-oriented text format, deliberately not JSON (no JSON library is
   vendored in this engine — see PHASE0, Culprit G).

## Step 2: The Situation / The Problem (Where are we now?)

- No `src/Scene/` folder exists yet.
- The two "kinds" of scene object this campaign must represent are already
  well-defined by earlier phases: a primitive (`PrimitiveType` — pre-existing,
  `src/Renderer/Primitives/PrimitiveMeshGenerator.h`) and an asset instance
  (identified by `Guid` — pre-existing, `src/Assets/AssetTypes.h`). Both are
  plain, dependency-light headers already safe to include from a new
  Tier-1-testable module (confirmed by direct inspection: `PrimitiveType` is
  a bare `enum class` with no Renderer/Vulkan dependency; `Guid` is a plain
  128-bit POD struct, with `IsValid()`, `Generate()`, `ToString()`, `Parse()`,
  and an `Invalid()` factory returning the all-zero value — see
  `src/Assets/AssetTypes.h`).
- `src/Assets/GtaFile.h` is this codebase's own precedent for "a from-scratch
  file format, hand-designed, with a fixed magic/version header, read/write
  functions that never throw and degrade to `std::nullopt`/`false` on any
  malformed input" — this phase's `.gtscene` text format follows the exact
  same philosophy, just as plain text instead of a packed binary struct.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — `src/Scene/SceneDocument.h` (new file)

```cpp
#pragma once

#include "../Assets/AssetTypes.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"
#include "../Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <string>
#include <vector>

namespace gte {

// Which kind of scene object one SceneObjectRecord describes - the engine
// currently only knows how to (re)create two kinds of top-level scene
// object (see task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md):
// one spawned from a built-in PrimitiveType (Game::CreatePrimitiveEntity()),
// and one spawned from an imported asset file, referenced by its stable
// AssetDatabase Guid (Game::CreateMeshEntityFromGtaFile()). Explicit numeric
// values are NOT pinned here (unlike AssetType) because this enum is never
// stored as a raw integer in the .gtscene TEXT format itself - see
// SceneTextFormat.cpp, which writes/reads it as the literal strings
// "Primitive"/"Asset".
enum class SceneObjectKind {
    Primitive,
    Asset,
};

// One serializable top-level scene object - a ROOT entity only (see
// Scene/SceneBuilder.h's own doc comment for why a multi-part asset's CHILD
// entities are never individually represented here). Plain data, no
// Registry/Entity/Renderer dependency of any kind - the exact same
// "component-style" plain-struct philosophy AGENTS.md already documents for
// every real ECS component (see ECS/Components/Transform.h), just living
// outside the ECS entirely since a SceneDocument is a serialization-time
// snapshot, never a live entity.
struct SceneObjectRecord {
    SceneObjectKind kind = SceneObjectKind::Primitive;

    // Optional cosmetic display name (see ECS/Components/Name.h) - empty
    // means "no Name component on the entity this record produces/came
    // from".
    std::string name;

    // World-space* transform this object is spawned/restored with -
    // *actually the entity's own Transform::position/rotation/scale, which
    // is genuinely world-space for every entity this campaign ever
    // serializes, since only ROOT entities (Transform::parent ==
    // kInvalidEntity) are ever captured - see SceneBuilder.h.
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();

    // Meaningful only when kind == SceneObjectKind::Primitive - which
    // built-in shape to recreate via Game::CreatePrimitiveEntity().
    PrimitiveType primitiveType = PrimitiveType::Cube;

    // Meaningful only when kind == SceneObjectKind::Asset -
    // AssetDatabase::FindByGuid()'s key for resolving this back to an
    // absolute *.gta path at load time (see Scene/SceneBuilder.h and
    // Editor/SceneIO.h). Guid::Invalid() (the default) when kind ==
    // Primitive. DeserializeSceneDocument() (see SceneTextFormat.h below)
    // NEVER produces a kind == Asset record with an Invalid() Guid here -
    // that combination is rejected as a parse failure instead (v2 - see
    // SceneTextFormat.h's own doc comment).
    Guid assetGuid;
};

// A whole serializable scene - just a flat list of top-level object
// records. Deliberately NOT a tree/hierarchy (see SceneObjectRecord's own
// doc comment above) - every record is spawned independently and
// positioned/rotated/scaled in world space.
struct SceneDocument {
    std::vector<SceneObjectRecord> objects;
};

} // namespace gte
```

### 3.2 — `src/Scene/SceneTextFormat.h` (new file)

```cpp
#pragma once

#include "SceneDocument.h"

#include <optional>
#include <string>

namespace gte {

// The engine's first hand-rolled TEXT-based file format (see README.md's
// long-standing "text file can stay still for now" note, and
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md, Culprit G,
// for why this is a small custom format rather than JSON - no JSON library
// is vendored in this engine, and a from-scratch, line-oriented format is
// simple enough to hand-write a forgiving parser for). Deliberately pure:
// takes/returns only a SceneDocument (Scene/SceneDocument.h) and a
// std::string - no filesystem I/O, no Registry, no Renderer - so this is
// genuinely Tier-1-testable exactly like Assets/GtaFile.h's own
// header/payload (de)serialization (see tests/Scene/SceneTextFormatTests.cpp).
// Actual file reading/writing is a separate, thin concern - see
// Editor/SceneIO.h.
//
// --- Format spec ("*.gtscene") ---
// Line 1: "GTSCENE <version>" - a fixed magic token + the format's integer
//   version (currently always "1" - kSceneTextFormatVersion below). Any
//   other first line (missing, wrong magic, unparsable/mismatched version)
//   makes DeserializeSceneDocument() return std::nullopt outright - the
//   same "reject the whole file rather than guess" philosophy
//   Assets/GtaFile.h's own magic check already uses.
// Every following non-blank line is either:
//   - the literal "OBJECT" - starts a new SceneObjectRecord (defaults:
//     SceneObjectKind::Primitive, PrimitiveType::Cube, identity transform,
//     empty name, Guid::Invalid()).
//   - the literal "END" - closes the CURRENT record (started by the most
//     recent "OBJECT") and appends it to the result's `objects` list. An
//     "END" with no currently-open record, or a file that never closes its
//     last "OBJECT" with a matching "END", makes the WHOLE deserialize fail
//     (return std::nullopt) - a malformed file is never partially applied.
//     THIS is also the one point where a whole-record, cross-field
//     invariant is checked (v2): if the record's `kind` is
//     SceneObjectKind::Asset, its `assetGuid` MUST be a valid
//     (`Guid::IsValid()`) value, whether that's because no `assetGuid=` line
//     was ever seen for this object, or because one WAS seen but its value
//     parsed to Guid::Invalid() (garbage/all-zero hex) - either way, the
//     WHOLE deserialize fails. This check does NOT run at all when `kind`
//     is Primitive - `assetGuid` is simply unused/ignored for a Primitive
//     record either way, present or not, valid or not (see the
//     "kind=Primitive with a stray assetGuid=" note in the key list below).
//   - a "key=value" pair (split on the FIRST '=' only) belonging to the
//     currently-open record. Recognized keys:
//       kind=Primitive|Asset
//       name=<any text, must not itself contain a newline>
//       primitiveType=<PrimitiveType's own ToString() spelling - "Cube"/
//         "Sphere"/"Capsule"/"Cone"/"Plane", see
//         Renderer/Primitives/PrimitiveMeshGenerator.h>
//       assetGuid=<Guid::ToString() format - 32 lowercase hex digits>. A
//         SYNTACTICALLY malformed value here (wrong length, non-hex
//         characters) fails the whole parse immediately, exactly like any
//         other malformed recognized key (see below) - this is checked
//         regardless of `kind`, since a syntax error is a syntax error.
//         What is deliberately NOT checked here (moved to the END-time
//         cross-field check above, v2) is whether the parsed Guid is
//         semantically Guid::Invalid() - a kind=Primitive object carrying a
//         stray assetGuid= line (valid-looking or not) is tolerated; only a
//         kind=Asset object's FINAL assetGuid is required to be valid.
//       position=<x>,<y>,<z>   (three '%f'-parsable floats)
//       rotation=<x>,<y>,<z>,<w>  (four '%f'-parsable floats, Quat order)
//       scale=<x>,<y>,<z>
//     An UNRECOGNIZED key is silently ignored (forward-compatibility - a
//     future version's extra key doesn't break an older reader). A
//     recognized key with a value that fails to parse (wrong field count,
//     non-numeric text, an unrecognized `kind`/`primitiveType` spelling, or
//     a syntactically malformed `assetGuid`) makes the WHOLE deserialize
//     fail (return std::nullopt) - never silently substitutes a default for
//     a field that was PRESENT but malformed (as opposed to a field that
//     was simply absent, which legitimately keeps SceneObjectRecord's own
//     default - e.g. an object with no "name=" line at all is simply
//     unnamed, not an error; an object with no "assetGuid=" line at all is
//     only an error if its kind is Asset - see the END-time check above).
//   - a blank line (ignored, purely for readability - SerializeSceneDocument()
//     never emits one itself, but DeserializeSceneDocument() tolerates it).
inline constexpr int kSceneTextFormatVersion = 1;

// Renders `document` into the format above - deterministic (the same
// SceneDocument value always produces byte-identical text) and always
// succeeds (there is no invalid SceneDocument value - every field already
// has a valid default). Floats are formatted with a fixed 6-decimal-place
// precision (matches Guid::ToString()'s own "always the same, predictable
// width" convention).
std::string SerializeSceneDocument(const SceneDocument& document);

// Parses text previously produced by SerializeSceneDocument() (or any text
// conforming to the format spec above) back into a SceneDocument. Returns
// std::nullopt for anything malformed per the rules above - never throws.
// Guaranteed (v2): every SceneObjectKind::Asset record in a successfully
// returned SceneDocument has `assetGuid.IsValid() == true` - callers (see
// Scene/SceneBuilder.h, Editor/SceneIO.h) never need to re-check this
// themselves before calling AssetDatabase::FindByGuid() with it.
std::optional<SceneDocument> DeserializeSceneDocument(const std::string& text);

} // namespace gte
```

### 3.3 — `src/Scene/SceneTextFormat.cpp` (new file)

Implementation notes (write straightforward, defensive C++ - no external
parsing library, matching this codebase's existing hand-rolled-parser
precedent in `Assets/PmxLoader.cpp`/`VmdLoader.cpp`):

- `SerializeSceneDocument()`: build a `std::ostringstream` (or plain
  `std::string` + `+=`), write `"GTSCENE " + std::to_string(kSceneTextFormatVersion) + "\n"`,
  then for each `SceneObjectRecord`: `"OBJECT\n"`, `"kind=" + (kind == Primitive ? "Primitive" : "Asset") + "\n"`,
  `"name=" + record.name + "\n"` (only emit this line at all when `record.name`
  is non-empty - keeps a typical, unnamed object's text compact and matches
  the "absent means default" parse rule), `"primitiveType=" + ToString(record.primitiveType) + "\n"`
  (only when `kind == Primitive`), `"assetGuid=" + record.assetGuid.ToString() + "\n"`
  (only when `kind == Asset`), then always `"position=%f,%f,%f\n"` /
  `"rotation=%f,%f,%f,%f\n"` / `"scale=%f,%f,%f\n"` (use `snprintf` with
  `"%.6f"` per component, comma-joined), then `"END\n"`.
- `DeserializeSceneDocument()`: split `text` into lines (handle both `\n`
  and `\r\n` - strip a trailing `\r` off each line defensively, since a
  hand-edited file on Windows may use either), trim leading/trailing
  whitespace per line, skip blank lines. Validate the FIRST non-blank line
  is exactly `"GTSCENE " + std::to_string(kSceneTextFormatVersion)` (an
  exact string compare - simplest possible version gate; a future version
  bump adds explicit handling here, mirroring `GtaFile.h`'s own
  `kGtaCurrentVersion` philosophy). Then iterate remaining lines with a
  small state machine: `bool insideObject = false;` `SceneObjectRecord current;`
  `bool sawAssetGuidLine = false;` (v2 - reset alongside `current` on every
  `"OBJECT"`, tracked purely so the END-time check below can tell "no
  assetGuid= line at all" apart from "one was seen, and its value happened
  to parse fine" without needing a second lookup) — on `"OBJECT"`: if
  `insideObject` is already true, fail (nested/unclosed OBJECT -
  malformed), else reset `current = SceneObjectRecord{};`,
  `sawAssetGuidLine = false;`, and set `insideObject = true`; on `"END"`: if
  NOT `insideObject`, fail; else, **first run the v2 cross-field check**:
  if `current.kind == SceneObjectKind::Asset && !current.assetGuid.IsValid()`,
  fail the whole parse right here (covers BOTH "no assetGuid= line was ever
  seen for this Asset object" and "one was seen but parsed to
  Guid::Invalid()" - `sawAssetGuidLine` itself doesn't even need to be
  read for this specific check, `IsValid()` already covers both cases, but
  is kept available for a clearer assertion/diagnostic message if desired);
  if that check passes, `result.objects.push_back(current); insideObject = false;`;
  otherwise (only valid when `insideObject`): split on the first `'='`,
  dispatch on the key name, parse the value into the matching field, failing
  the whole parse on any malformed value (wrong token count for a CSV field,
  `std::from_chars`/`strtof` reporting a parse error, an unrecognized
  `kind=`/`primitiveType=` spelling, or a syntactically malformed
  `assetGuid=` - i.e. NOT exactly 32 hex characters; note `Guid::Parse()`
  itself never fails outright and instead degrades to `Guid::Invalid()` per
  its own doc comment, so a SYNTAX check on the raw string - length and
  hex-ness - must happen here BEFORE calling `Guid::Parse()`, otherwise a
  syntactically garbage string would silently become a semantically
  "valid-looking absence" that only the END-time check would ever catch;
  simplest correct approach: validate the raw 32-hex-digit shape by hand
  first, THEN call `Guid::Parse()` on it - a value that already fails the
  shape check fails the parse immediately, same as any other malformed
  recognized key). On a successfully-parsed `assetGuid=` line, set
  `sawAssetGuidLine = true` regardless of `kind` (harmless bookkeeping for a
  Primitive record - never read/enforced in that case). At the very end, if
  `insideObject` is still true (an "OBJECT" was never closed with a matching
  "END"), fail the whole parse (return `std::nullopt`).
- For the three CSV vector/quat fields (`position`/`rotation`/`scale`),
  write one small local helper, e.g. `ParseFloatCsv(const std::string& value, int expectedCount, float* out)`
  returning `bool`, reused for all three (2 use `Vec3`'s 3 components, one
  uses `Quat`'s 4) - split on `,`, trim each token, `std::strtof`/
  `std::from_chars`, fail on wrong token count or any token that doesn't
  fully consume (trailing garbage after the number).

### 3.4 — `CMakeLists.txt` wiring

Add a new block of source entries to `gte_core`'s
`add_library(gte_core STATIC ...)` list in the root `CMakeLists.txt`. Insert
it as its own clearly-commented group — a sensible spot is right after the
`src/Physics/*` entries end and before `src/Assets/*` begins (currently
around line 265–266, right before `src/Assets/AssetTypes.h`):

```
    src/Scene/SceneDocument.h
    src/Scene/SceneTextFormat.h
    src/Scene/SceneTextFormat.cpp
```

This is deliberately in the main, unconditional source list (NOT inside any
`if(GTE_ENABLE_EDITOR)`/`if(GTE_ENABLE_PROJECT_PANEL)` block) — this module
must always compile, per PHASE0's Design Decision #4 (mirrors `src/Physics/`/
`src/Jobs/`'s own unconditional placement).

### 3.5 — Tests: `tests/Scene/SceneTextFormatTests.cpp` (new file)

Register it in `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES`
list (add `Scene/SceneTextFormatTests.cpp` — a sensible spot is right after
the `Assets/RigFileTests.cpp` entry, since this mirrors that file's own
"encode/decode a from-scratch format" test shape). Also add this new file's
own descriptive paragraph to the "Test taxonomy" comment block at the top
of `tests/CMakeLists.txt`, matching every other entry's own documented
style (per `AGENTS.md`'s "Job System" Phase 4 audit table precedent: *"every
existing Tier-1 test file in this codebase has a matching descriptive
paragraph"*).

Cover, at minimum:
- **Round-trip, empty document**: `SerializeSceneDocument(SceneDocument{})`
  then `DeserializeSceneDocument()` on the result returns a `SceneDocument`
  with an empty `objects` vector.
- **Round-trip, one Primitive record**: a hand-built `SceneObjectRecord`
  (`kind = Primitive`, a non-default `primitiveType`, a non-identity
  position/rotation/scale, a non-empty `name`) survives
  serialize→deserialize with every field bit-for-bit (or within a tight
  float epsilon) equal to the original.
- **Round-trip, one Asset record**: same, but `kind = Asset` with a real,
  non-invalid `Guid` (e.g. `Guid::Generate()`) and no `primitiveType`
  significance — confirm the deserialized record's `assetGuid` matches
  exactly (`Guid`'s own `operator==`).
- **Round-trip, multiple mixed records**: at least one `Primitive` and one
  `Asset` record in the same document, confirm both survive in order.
- **Malformed input rejection** (`DeserializeSceneDocument()` returns
  `std::nullopt` for each): empty string; a first line that isn't
  `"GTSCENE 1"` at all (wrong magic, or right magic wrong version, e.g.
  `"GTSCENE 2"`); an `"OBJECT"` with no matching `"END"` before EOF; an
  `"END"` with no preceding `"OBJECT"`; a `"kind="` value that is neither
  `"Primitive"` nor `"Asset"`; a `"primitiveType="` value that isn't one of
  `PrimitiveType`'s own known spellings; a `"position="` line with the wrong
  number of comma-separated tokens (e.g. only 2, or 4); a `"position="` line
  containing non-numeric text; an `"assetGuid="` line that is syntactically
  malformed (wrong length / non-hex characters, e.g. `"assetGuid=not-a-guid"`).
- **(v2) `kind=Asset` cross-field validation** (`DeserializeSceneDocument()`
  returns `std::nullopt` for each):
  - a hand-crafted `kind=Asset` object block with NO `"assetGuid="` line at
    all (previously — v1 — this silently "succeeded" into an unresolvable
    record; v2 must reject it outright);
  - a hand-crafted `kind=Asset` object block whose `"assetGuid="` line
    parses to `Guid::Invalid()` (e.g. `"assetGuid=00000000000000000000000000000000"` —
    a syntactically well-formed but all-zero 32-hex-digit string).
- **(v2) `kind=Primitive` tolerates a stray `assetGuid=`** — a hand-crafted
  `kind=Primitive` object block that ALSO carries a well-formed
  `"assetGuid="` line (any value, valid or `Guid::Invalid()`-looking) still
  parses SUCCESSFULLY — proves the cross-field check above is correctly
  scoped to `kind == Asset` only, not applied unconditionally like v1's
  rule was.
- **Unknown-key forward compatibility**: a hand-crafted text blob containing
  a valid object block PLUS one extra, made-up `"futureField=whatever"` line
  inside it still parses successfully, ignoring that one line.
- **Optional-field omission**: a hand-crafted `Primitive` object block with
  no `"name="` line at all deserializes to a record with an empty `name` —
  proves "absent" (not present at all) is treated differently from
  "present but malformed" (which fails the whole parse, per the spec).

## Step 4: What We Will NOT Do (Focus)

- We will **not** support escaping (e.g. a `name=` value that itself
  contains a literal newline or the `=` character) — an accepted, documented
  simplification of this deliberately minimal format; a name containing
  such characters is simply not round-trippable today.
- We will **not** make this format extensible via nested objects/arrays —
  every record is a flat `key=value` list; there is no scene-object
  hierarchy in the file at all (see PHASE0's Design Decision #3 — only root
  entities are ever represented).
- We will **not** add any filesystem I/O to this module — reading/writing
  the actual `TestScene.gtscene` file on disk is PHASE5's job
  (`Editor/SceneIO.h`), never this one.
- We will **not** (v2) extend the new `kind`-aware cross-field validation to
  any OTHER field/kind combination beyond `Asset`/`assetGuid` — `Primitive`/
  `primitiveType` has no equivalent "must be present and valid" requirement
  added here, since an absent/malformed-but-still-parseable `primitiveType`
  already safely defaults to `PrimitiveType::Cube` with no downstream
  resolution failure possible (unlike an invalid `Guid`, which PHASE4/
  PHASE5's Load path could never resolve to anything).
