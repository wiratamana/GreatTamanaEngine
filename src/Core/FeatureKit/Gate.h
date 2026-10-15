#pragma once

#include "View.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gte::fx {

// The pass names a registration answers to in the toggle panel. Empty: the registered callback name.
// A callback that declares several separately toggled passes lists them all.
struct Gate {
    std::vector<std::string> passNames;

    Gate() = default;
    Gate(std::initializer_list<const char*> names)
    {
        for (const char* name : names) {
            passNames.emplace_back(name);
        }
    }
};

using PassEnabledFn = std::function<bool(std::string_view passName)>;

// Whether the callback may run: any listed pass (or the registered name, when the gate is empty) is enabled.
// A missing predicate means "no toggle registry": everything is enabled.
inline bool GateAllows(const Gate& gate, std::string_view registeredName, const PassEnabledFn& isEnabled)
{
    if (!isEnabled) {
        return true;
    }
    if (gate.passNames.empty()) {
        return isEnabled(registeredName);
    }
    for (const std::string& name : gate.passNames) {
        if (isEnabled(name)) {
            return true;
        }
    }
    return false;
}

// Per-view callbacks never run for a view the kit cannot name (no view data) or for the shared scope;
// shared callbacks run regardless of the view.
enum class CallbackScope : std::uint8_t { PerView, Shared };

inline bool ShouldInvokeFeatureCallback(const std::optional<View>& view, CallbackScope registered) noexcept
{
    if (registered == CallbackScope::Shared) {
        return true;
    }
    return view.has_value() && view->scope != rg::ViewScope::Shared;
}

} // namespace gte::fx
