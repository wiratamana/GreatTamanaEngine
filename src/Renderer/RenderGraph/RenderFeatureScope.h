#pragma once

// RAII marker: passes declared while it is alive belong to the named feature. Scopes nest as a stack
// on the builder, so an inner scope overrides an outer one until it ends.

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
