#pragma once

#include "ViewScopeUtil.h"

#include <array>
#include <type_traits>

namespace gte::rg {

// One T per ViewScope. Array storage, so slot addresses are stable for the object's lifetime.
template <typename T>
class PerView {
    static_assert(std::is_default_constructible_v<T>, "PerView<T> needs a default-constructible T");

public:
    T& For(ViewScope scope) noexcept { return m_slots[ViewScopeIndex(scope)]; }
    const T& For(ViewScope scope) const noexcept { return m_slots[ViewScopeIndex(scope)]; }

private:
    std::array<T, kViewScopeCount> m_slots{};
};

} // namespace gte::rg
