#include "PresetFeatureSettings.h"

#include "PresetSceneSupport.h"
#include "../Core/Plugins/RenderFeatureCompositor.h"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace gte {

std::string FeatureDebugValueText(const fx::FeatureDebugValue& value)
{
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return v;
            } else {
                return std::to_string(v);
            }
        },
        value);
}

std::vector<fx::FeatureDebugArg> SnapshotFeatureSettings(
    const fx::IFeatureDebugEndpoint& endpoint, std::span<const std::string_view> keys)
{
    const fx::FeatureDebugResult state = endpoint.GetState();
    std::vector<fx::FeatureDebugArg> saved;
    for (const fx::FeatureDebugField& field : state.fields) {
        if (std::find(keys.begin(), keys.end(), field.key) != keys.end()) {
            saved.push_back({ field.key, FeatureDebugValueText(field.value) });
        }
    }
    return saved;
}

bool TrySetFeatureEnabled(RenderFeatureCompositor& compositor, const std::string& name, bool enabled) noexcept
{
    try {
        return compositor.SetFeatureEnabled(name, enabled);
    } catch (...) {
        return false;
    }
}

bool SetFeaturesEnabled(RenderFeatureCompositor* compositor, const char* category, std::span<const std::string> turnOn,
    std::span<const std::string> turnOff) noexcept
{
    if (compositor == nullptr) {
        if (turnOn.empty() && turnOff.empty()) {
            return true;
        }
        PresetLogError(category, "Cannot switch features back: the render feature compositor is missing.");
        return false;
    }
    bool complete = true;
    for (const std::string& name : turnOn) {
        if (!TrySetFeatureEnabled(*compositor, name, true)) {
            complete = false;
            PresetLogError(category, "Could not turn the feature back on: ", name);
        }
    }
    for (const std::string& name : turnOff) {
        if (!TrySetFeatureEnabled(*compositor, name, false)) {
            complete = false;
            PresetLogError(category, "Could not turn the feature back off: ", name);
        }
    }
    return complete;
}

bool RestoreFeatureSettings(fx::IFeatureDebugEndpoint* endpoint, const char* category, std::string_view label,
    std::span<const fx::FeatureDebugArg> saved) noexcept
{
    if (saved.empty()) {
        return true;
    }
    if (endpoint == nullptr) {
        PresetLogError(category, "Cannot restore the settings: the feature is missing: ", label);
        return false;
    }
    try {
        const fx::FeatureDebugResult restored = endpoint->Set(saved);
        if (!restored.ok) {
            PresetLogError(category, std::string(label) + " refused the saved settings: ", restored.message);
            return false;
        }
    } catch (...) {
        PresetLogError(category, "Restoring the settings threw: ", label);
        return false;
    }
    return true;
}

} // namespace gte
