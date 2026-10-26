#pragma once

#include "../Editor/SceneCameraControl.h"

#include <nlohmann/json.hpp>

#include <string>

namespace gte::Network {

// Parsed query of GET /scene_camera/set_pose. Position is in world units, angles in degrees.
struct ParsedSetPoseQuery {
    bool valid = false;
    std::string errorMessage; // names the first bad key
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

// All five values are required. Each must be one whole finite decimal number inside the
// limits of EditorCamera::SetPose.
ParsedSetPoseQuery ParseSetPoseQuery(const std::string& xParam, const std::string& yParam, const std::string& zParam,
    const std::string& yawParam, const std::string& pitchParam);

// {"position":{"x","y","z"},"yaw_degrees","pitch_degrees"}. Embedded in the Scene camera state.
nlohmann::json ScenePoseToJson(const SceneCameraPose& pose);

// The same object plus "success":true. Body of the pose and set_pose routes.
std::string BuildSceneCameraPoseJson(const SceneCameraReport& report);

// {"success":true,"message":"exit requested"}. Body of POST /exit.
std::string BuildExitReplyJson();

} // namespace gte::Network
