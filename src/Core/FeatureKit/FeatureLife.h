#pragma once

#include <memory>
#include <utility>

namespace gte::fx {

// Lifetime token of one feature module. Core cannot unregister a built-in pass, so every callback a module
// registers is wrapped in WhileAlive: once the module is gone, the callback does nothing.
struct FeatureLife {};

template <typename Fn>
class WhileAlive {
public:
    WhileAlive(std::weak_ptr<FeatureLife> life, Fn fn)
        : m_life(std::move(life))
        , m_fn(std::move(fn))
    {
    }

    template <typename... Args>
    void operator()(Args&&... args) const
    {
        if (!m_life.expired()) {
            m_fn(std::forward<Args>(args)...);
        }
    }

private:
    std::weak_ptr<FeatureLife> m_life;
    Fn m_fn;
};

} // namespace gte::fx
