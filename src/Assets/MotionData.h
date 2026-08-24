#pragma once

#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Plain, engine-native MikuMikuDance motion (.vmd) data - this engine's
// "Motion Importer" counterpart to SkeletonData.h/MorphData.h/PhysicsData.h's
// "Model Importer" half (see PmxLoader.h's own file comment for that side).
// Produced by VmdLoader.h's LoadVmdMotion() (from saba::VMDFile - see
// third_party/saba/src/Saba/Model/MMD/VMDFile.h), same "engine-native,
// saba/glm-free copy of the third-party format's data" shape as every other
// MMD-import struct in this engine - no saba::/glm:: type ever crosses this
// header's own public API.
//
// A VMD file is a flat, per-track list of KEYFRAMES (never a hierarchical
// timeline/clip structure) - each keyframe names WHAT it animates by a
// human-authored NAME STRING (a bone/morph name), not an index into any
// particular model's SkeletonData/MorphData, since a single .vmd motion is
// commonly authored once and reused across many different character models
// that each have their own independent bone/morph numbering (unlike a .pmx's
// own internal bone-index references, which are only ever meaningful within
// that one file). A consumer wiring a MotionData onto a specific model must
// resolve every track's name -> that model's own SkeletonData::bones /
// MorphData::morphs index by name lookup itself; nothing here performs that
// resolution or assumes any particular target skeleton/morph set exists.
// Deliberately a plain data struct with no behavior of its own (no
// interpolation evaluation/playback here) - same "ECS component"-style
// philosophy as MeshData/SkeletonData (see AGENTS.md, "Entity-Component-
// System").
//
// Every keyframe list below is stored EXACTLY in on-disk order - a VMD file
// never guarantees its keyframes are sorted by frame number (real-world
// files routinely aren't) - a consumer that needs sorted/binary-searchable
// playback must sort by `frame` itself, once, after loading.

// One bone's local translation/rotation offset at one frame - the "Bones"
// half of a VMD motion. Mirrors saba::VMDMotion field for field.
struct BoneKeyframe {
    std::string boneName; // Matched against a target model's SkeletonData::Bone::name, not an index.
    std::uint32_t frame = 0; // VMD's own fixed 30fps frame grid.

    // Local bone-space translation/rotation offset FROM the bind pose at
    // this frame - added on top of SkeletonData::Bone::position/an identity
    // rotation, never an absolute model-space transform.
    Vec3 translation = Vec3::Zero();
    Quat rotation = Quat::Identity();

    // Raw MMD bezier interpolation control-point bytes, verbatim from the
    // source VMD, deliberately UNINTERPRETED - 4 curves in order (X
    // translation, Y translation, Z translation, rotation), 4 control bytes
    // each, exactly as saba::VMDMotion::m_interpolation stores them. No
    // interpolation evaluation happens anywhere in this engine yet (matches
    // PhysicsData.h's own "DATA only, no simulation" precedent) - a future
    // playback system decodes this itself; see the MMD community's own "VMD
    // file format" documentation for the exact per-byte bezier meaning if
    // that future work is done.
    std::array<std::uint8_t, 64> interpolation = {};
};

// A single morph's (blend shape's) weight at one frame - the "Morphs" half
// of a VMD motion, matching MorphData.h's own PMX morph list in spirit. A
// VMD only ever DRIVES a morph's weight over time; it carries no morph
// TOPOLOGY/offsets of its own - those live in the target model's own
// MorphData, resolved by name.
struct MorphKeyframe {
    std::string morphName; // Matched against a target model's Morph::name.
    std::uint32_t frame = 0;
    float weight = 0.0f; // 0.0 = base mesh, 1.0 = fully-applied morph (same convention as Morph's own weighted offsets).
};

// One frame of virtual "camera work" - present only in a camera-motion VMD
// (most character motion .vmd files carry none of these - see MotionData's
// own "every list may legitimately be empty" contract below). Distinct from
// this engine's own ECS Camera component (src/ECS/Components/Camera.h) -
// this is just imported MMD authoring data, not wired to any live camera.
struct CameraKeyframe {
    std::uint32_t frame = 0;
    // Distance from `interest` the eye sits at, back along the camera's own
    // facing direction (MMD convention: authored values are usually
    // negative).
    float distance = 0.0f;
    Vec3 interest = Vec3::Zero(); // The point the camera looks at (the eye itself is derived from interest + distance + rotation).
    Vec3 rotateRadians = Vec3::Zero(); // Euler angles, radians.

    // Raw bezier interpolation bytes, verbatim from the source VMD - 6
    // curves (X, Y, Z, rotation, distance, field-of-view), 4 bytes each = 24
    // bytes total, same "opaque, undecoded" contract as
    // BoneKeyframe::interpolation above.
    std::array<std::uint8_t, 24> interpolation = {};

    std::uint32_t fieldOfViewDegrees = 0; // MMD stores this as a plain integer degree value.
    bool isPerspective = true;
};

// One frame of a directional "light" (MMD's simple single-color/single-
// direction light - present only in a small minority of authored VMDs).
struct LightKeyframe {
    std::uint32_t frame = 0;
    Vec3 color = Vec3::Zero(); // RGB, 0..1 range (MMD convention).
    // The light's own direction vector - MMD/saba call this field
    // "position", but it's actually a direction, not a world-space point
    // (matches saba::VMDLight::m_position's own real meaning).
    Vec3 direction = Vec3::Zero();
};

// One frame of MMD's built-in ground-shadow toggle/mode.
struct ShadowKeyframe {
    std::uint32_t frame = 0;
    std::uint8_t shadowType = 0; // 0 = off, 1 = mode1, 2 = mode2 (MMD's own fixed enumeration).
    float distance = 0.0f;
};

// One frame of the "IK enable/disable" track - lets an animator toggle a
// specific bone's own PMX-authored IK solving on/off over time (e.g.
// temporarily disabling foot IK for a jump) without needing a bone keyframe
// at all.
struct IkEnableState {
    std::string ikBoneName; // Matched against a target model's SkeletonData::Bone::name (an IK bone).
    bool enabled = true;
};

struct IkKeyframe {
    std::uint32_t frame = 0;
    bool visible = true; // MMD's own "model display" on/off flag, bundled into the same track as the per-bone IK toggles below.
    std::vector<IkEnableState> states;
};

// A full, flat VMD motion - every track kept as its own independent,
// unsorted, name-addressed keyframe list (see this file's own top comment).
// `modelName` is the VMD's own authoring-time target model name
// (informational only - MMD historically used it as a soft compatibility
// hint, e.g. to warn if a motion looks like it was authored for a different
// model - never required to match anything at import time). A typical
// character-motion .vmd populates boneKeyframes/morphKeyframes and leaves
// cameraKeyframes/lightKeyframes/shadowKeyframes/ikKeyframes empty; a
// camera-work .vmd is usually the opposite - every list is independently
// optional, never assumed non-empty.
struct MotionData {
    std::string modelName;

    std::vector<BoneKeyframe> boneKeyframes;
    std::vector<MorphKeyframe> morphKeyframes;
    std::vector<CameraKeyframe> cameraKeyframes;
    std::vector<LightKeyframe> lightKeyframes;
    std::vector<ShadowKeyframe> shadowKeyframes;
    std::vector<IkKeyframe> ikKeyframes;
};

} // namespace gte
