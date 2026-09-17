#pragma once

#include "SceneDocument.h"

#include <optional>
#include <string>

namespace gte {

// The engine's SECOND on-disk *.gtscene format - replaces the
// scene-serialization-1 hand-rolled TEXT grammar (SceneTextFormat.h/.cpp,
// deleted in this same phase) with JSON, per
// task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #1. The file keeps the same name/extension
// (Editor/SceneIO.h's DefaultScenePath(), "TestScene.gtscene") - only the
// bytes inside it change shape. Deliberately pure: takes/returns only a
// SceneDocument (Scene/SceneDocument.h) and a std::string - no filesystem
// I/O, no Registry, no Renderer - genuinely Tier-1-testable, exactly like
// the old SceneTextFormat.h (see tests/Scene/SceneJsonFormatTests.cpp).
// Actual file reading/writing is a separate, thin concern - see
// Editor/SceneIO.h.
//
// --- Format spec ("*.gtscene", v2) --- see
// PHASE0_MASTER_STRATEGY.md's Appendix A for a fully worked example.
//
// Top-level JSON object:
//   "gtscene_version": <int>  - MUST be exactly kSceneJsonFormatVersion
//     below for DeserializeSceneDocument() to accept the file at all - no
//     partial/best-effort migration from the old v1 TEXT format (PHASE0's
//     Locked Design Decision #5 explicitly accepts breaking any old file).
//   "entities": <array of entity objects> - each one:
//     "parent": null | <non-negative int> - null for a root entity,
//       otherwise the 0-based index of this entity's OWN parent within
//       THIS SAME "entities" array (order in the array carries no other
//       meaning - a parent may appear before OR after any of its
//       children). An index that is out of range for the array's own
//       final size fails the WHOLE document.
//     "sibling_index": <non-negative int> - defaults to 0 if absent.
//     "asset_guid": <string> - OPTIONAL, defaults to "" if absent. See
//       PHASE4 for how/when this gets populated - never populated by THIS
//       phase's own SceneBuilder.cpp.
//     "components": <object> - OPTIONAL, defaults to an empty object if
//       absent. One key per registered ComponentTypeRegistry typeName
//       (e.g. "Transform", "Camera") mapped to that component's own
//       serialized fields (ECS/Reflection/ComponentTypeRegistry.h). An
//       unrecognized key (e.g. a NEWER engine build's component this OLDER
//       build doesn't know about) is silently ignored at Load time -
//       forward-compatible, matching every other "unrecognized input"
//       precedent in this codebase.
//   Every other top-level or per-entity key is silently ignored
//   (forward-compatibility).
//
// Bumped from SceneTextFormat.h's old kSceneTextFormatVersion (which was 1)
// - PHASE0's Locked Design Decision #5 explicitly accepts breaking any old
// .gtscene file written by that older format; this is a NEW, unrelated
// format version number, not a continuation of the old one's numbering.
inline constexpr int kSceneJsonFormatVersion = 2;

// Renders `document` as pretty-printed JSON text (2-space indent, via
// nlohmann::json::dump(2) - human-diffable/hand-editable, matching this
// engine's existing "a *.gtscene file is meant to be inspectable" spirit).
// Always succeeds - there is no invalid SceneDocument value.
std::string SerializeSceneDocument(const SceneDocument& document);

// Parses text previously produced by SerializeSceneDocument() (or ANY text
// conforming to the shape above) back into a SceneDocument. Returns
// std::nullopt (never throws) for:
//   - text that isn't valid JSON at all,
//   - a top-level value that isn't a JSON object,
//   - a missing or non-integer "gtscene_version", or one that doesn't
//     exactly equal kSceneJsonFormatVersion (no partial/best-effort
//     migration from version 1 - see PHASE0's Locked Design Decision #5),
//   - a missing or non-array "entities",
//   - any entity that isn't itself a JSON object,
//   - any entity whose "components" key is present but not a JSON object,
//   - any entity whose "parent" is present, non-null, but not a
//     non-negative integer, OR is an integer that is out of range for
//     `document.entities.size()` at the time deserialization finishes (a
//     forward OR backward reference to a real array slot is fine - order
//     doesn't matter - but a reference to an index that doesn't exist at
//     all is rejected as malformed, the whole document fails),
//   - any entity whose "sibling_index" is present but not a non-negative
//     integer,
//   - any entity whose "asset_guid" is present but not a JSON string.
// "sibling_index" defaults to 0 if absent (never fails parsing on its
// own). "asset_guid" defaults to "" if absent. Every OTHER key inside one
// entity object (besides parent/sibling_index/asset_guid/components) is
// silently ignored, forward-compatible.
std::optional<SceneDocument> DeserializeSceneDocument(const std::string& text);

} // namespace gte
