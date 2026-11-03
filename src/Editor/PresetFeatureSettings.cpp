#include "PresetFeatureSettings.h"

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

} // namespace gte
