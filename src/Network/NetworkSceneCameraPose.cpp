#include "NetworkSceneCameraPose.h"

#include "../Editor/EditorCamera.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace gte::Network {

namespace {

// Whole-string decimal parse. Rejects empty text, leading space, trailing text, nan and inf.
bool TryParseFiniteFloat(const std::string& text, float limit, float& out)
{
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        return false;
    }
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value) || std::fabs(value) > limit) {
        return false;
    }
    out = value;
    return true;
}

std::string BadKeyMessage(const char* key, float limit)
{
    return std::string("missing or invalid required query parameter: ") + key
        + " - must be a finite number within +/-" + std::to_string(static_cast<long long>(limit));
}

} // namespace

ParsedSetPoseQuery ParseSetPoseQuery(const std::string& xParam, const std::string& yParam, const std::string& zParam,
    const std::string& yawParam, const std::string& pitchParam)
{
    ParsedSetPoseQuery result;
    struct Field {
        const char* key;
        const std::string& text;
        float limit;
        float& out;
    };
    const Field fields[] = {
        { "x", xParam, EditorCamera::kMaxPoseUnits, result.x },
        { "y", yParam, EditorCamera::kMaxPoseUnits, result.y },
        { "z", zParam, EditorCamera::kMaxPoseUnits, result.z },
        { "yaw", yawParam, EditorCamera::kMaxPoseDegrees, result.yaw },
        { "pitch", pitchParam, EditorCamera::kMaxPoseDegrees, result.pitch },
    };
    for (const Field& field : fields) {
        if (!TryParseFiniteFloat(field.text, field.limit, field.out)) {
            result.errorMessage = BadKeyMessage(field.key, field.limit);
            return result;
        }
    }
    result.valid = true;
    return result;
}

nlohmann::json ScenePoseToJson(const SceneCameraPose& pose)
{
    return nlohmann::json::object({
        { "position",
            nlohmann::json::object({ { "x", pose.position[0] }, { "y", pose.position[1] }, { "z", pose.position[2] } }) },
        { "yaw_degrees", pose.yawDegrees },
        { "pitch_degrees", pose.pitchDegrees },
    });
}

std::string BuildSceneCameraPoseJson(const SceneCameraReport& report)
{
    nlohmann::json body = ScenePoseToJson(report.pose);
    body["success"] = true;
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string BuildExitReplyJson()
{
    return nlohmann::json::object({ { "success", true }, { "message", "exit requested" } }).dump();
}

} // namespace gte::Network
