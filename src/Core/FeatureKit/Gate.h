#pragma once

#include "View.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gte::fx {

// The pass names a registration answers to in the toggle panel. Empty: the registered callback name.
// Always() is for work that is not a render pass and must never be silenced by a toggle.
struct Gate {
    std::vector<std::string> passNames;
    bool always = false;

    Gate() = default;
    Gate(std::initializer_list<const char*> names)
    {
        for (const char* name : names) {
            passNames.emplace_back(name);
        }
    }

    static Gate Always()
    {
        Gate gate;
        gate.always = true;
        return gate;
    }
};

// Whether the callback may run: any listed pass (or the registered name, when the gate is empty) is enabled.
// `isEnabled` is any callable taking a std::string_view; no allocation happens here.
template <typename Pred>
bool GateAllows(const Gate& gate, std::string_view registeredName, Pred&& isEnabled)
{
    if (gate.always) {
        return true;
    }
    if (gate.passNames.empty()) {
        return isEnabled(registeredName);
    }
    for (const std::string& name : gate.passNames) {
        if (isEnabled(std::string_view(name))) {
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
