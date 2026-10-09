#pragma once

// RAII marker: every pass declared while one of these is alive belongs to
// the named feature. Pushed on construction, popped on destruction - a
// stack, so nesting is safe (an inner scope temporarily overrides an outer
// one, then control reverts automatically). Builder-local state, not
// thread_local/global - zero multithreading hazard, nothing to reset
// between tests.

#include "RenderGraphBuilder.h"

#include <string_view>

namespace gte::rg {

class RenderFeatureScope {
public:
    RenderFeatureScope(RenderGraphBuilder& builder, std::string_view featureName)
        : m_builder(builder)
    {
        m_builder.PushOwningFeatureScope(featureName);
    }

    ~RenderFeatureScope() { m_builder.PopOwningFeatureScope(); }

    RenderFeatureScope(const RenderFeatureScope&) = delete;
    RenderFeatureScope& operator=(const RenderFeatureScope&) = delete;

private:
    RenderGraphBuilder& m_builder;
};

} // namespace gte::rg
