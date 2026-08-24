#pragma once

#include "MotionData.h"

#include <string>

namespace gte {

// Result of one LoadVmdMotion() call below - always fully populated (mirrors
// PmxLoadResult's own "always success + message, never a half-filled
// struct" convention - see src/Assets/PmxLoader.h).
struct VmdLoadResult {
    bool success = false;

    // The parsed motion (bone/morph/camera/light/shadow/IK keyframe tracks -
    // see MotionData.h) - any/all of its lists may legitimately be empty
    // depending on what kind of VMD this was (a character motion vs. a
    // camera-work file - see MotionData's own doc comment).
    MotionData motion;

    std::string message; // Human-readable status - always set, success or failure.
};

// Parses a MikuMikuDance .vmd motion file at `filePath` (a plain filesystem
// path, UTF-8 encoded - matches every other path-taking function in this
// engine, e.g. PmxLoader.h's LoadPmxModel()/AssetImporter.h's
// ImportAssetFile()) and extracts its bone/morph/camera/light/shadow/IK
// keyframe tracks into this engine's own plain, saba/glm-free MotionData
// (src/Assets/MotionData.h) - this engine's own types, never a saba:: or
// glm:: type (see FetchSaba.cmake's header comment for why that boundary
// matters: only VmdLoader.cpp itself includes a saba/glm header). Uses
// saba::ReadVMDFile() (third_party/saba/src/Saba/Model/MMD/VMDFile.h)
// internally - the same "wrap a third-party format reader behind a small
// engine-native function" shape as PmxLoader.h's LoadPmxModel() wrapping
// saba::ReadPMXFile(). Never throws; a missing/corrupt/unreadable file
// yields success == false with a descriptive message and an otherwise-
// default-constructed (empty) result, exactly like LoadPmxModel()'s own
// failure contract. Every VMD-native, fixed-size Shift-JIS-encoded name
// field (bone/morph/model/IK-bone names) is converted to UTF-8 here (via
// saba's own MMDFileString<Size>::ToUtf8String()) - MotionData never stores
// a raw Shift-JIS byte string. No frame-rate/timing remapping is performed -
// a VMD's own frame numbers are kept exactly as authored (MMD's fixed 30fps
// grid); a future step wiring this into a playback/animation system should
// account for that if a different engine-wide timebase is ever introduced.
VmdLoadResult LoadVmdMotion(const std::string& filePath);

} // namespace gte
