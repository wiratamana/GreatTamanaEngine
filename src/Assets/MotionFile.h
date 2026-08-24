#pragma once

#include "MotionData.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gte {

// (De)serializes a MotionData - the model name plus bone/morph/camera/
// light/shadow/IK keyframe tracks extracted from a MikuMikuDance .vmd file
// (see VmdLoader.h) - to/from a flat, engine-private binary blob. This is
// exactly what ends up as the PAYLOAD section of a *.gta AssetType::Animation
// file (see GtaFile.h and AssetImporter.h's VMD -> *.gta import pipeline),
// the motion-import equivalent of MeshFile.h's role for AssetType::Mesh
// geometry and RigFile.h's role for a Mesh asset's bone/morph/physics
// METADATA. Deliberately its own format/file (not an extension of
// MeshFile.h/RigFile.h) - a motion's data shape (named, unsorted, per-track
// keyframe lists) has nothing in common with either of those, and nothing
// outside this engine ever reads a *.gta file regardless (same "format
// unification, not interchange" rationale as every other *File.h in this
// engine).
//
// On-disk layout (all integers little-endian, all floats IEEE 754 binary32 -
// same conventions as MeshFile.h/RigFile.h; every variable-length section is
// length-prefixed with a uint32_t count/byte-length immediately before it,
// so a reader never needs to know a section's size in advance):
//
//   8 bytes  : magic ("GTEMOTN1", no null terminator - exactly 8 bytes)
//   string   : modelName (uint32_t byte length, then that many UTF-8 bytes)
//   uint32_t : bone keyframe count, followed by that many fixed-size-per-
//              record entries (name string + uint32 frame + Vec3 + Quat +
//              64 raw interpolation bytes)
//   uint32_t : morph keyframe count, followed by that many records (name
//              string + uint32 frame + float weight)
//   uint32_t : camera keyframe count, followed by that many records (uint32
//              frame + float distance + 2 Vec3 + 24 raw interpolation bytes
//              + uint32 field-of-view degrees + bool isPerspective)
//   uint32_t : light keyframe count, followed by that many records (uint32
//              frame + 2 Vec3)
//   uint32_t : shadow keyframe count, followed by that many records (uint32
//              frame + 1 byte shadow type + float distance)
//   uint32_t : IK keyframe count, followed by that many variable-size
//              records (uint32 frame + bool visible + a length-prefixed
//              array of {name string, bool enabled} IK-bone states)
//
// Encodes/decodes never fail on a well-formed (even if entirely empty)
// MotionData; Decode only fails (returns std::nullopt) on a genuinely
// malformed/truncated/wrong-magic blob - never throws, same failure
// contract as MeshFile.h's/RigFile.h's own Decode functions.
inline constexpr char kMotionFileMagic[8] = { 'G', 'T', 'E', 'M', 'O', 'T', 'N', '1' };

std::vector<std::uint8_t> EncodeMotionDataToBytes(const MotionData& motion);

std::optional<MotionData> DecodeMotionDataFromBytes(const std::vector<std::uint8_t>& bytes);

} // namespace gte
