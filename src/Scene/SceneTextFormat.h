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
