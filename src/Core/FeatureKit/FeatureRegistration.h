#pragma once

#include "Feature.h"
#include "../Plugins/BuiltinFeatureModuleRegistry.h"
#include "../Plugins/IEngineFeatureModule.h"

#include <functional>
#include <memory>
#include <stdexcept>

namespace gte::fx {

using FeatureFactory = std::function<std::unique_ptr<Feature>(FeatureHost&)>;

namespace detail {

// The engine-side module that owns one feature, runs its Setup() and commits the registrations to Core.
std::unique_ptr<IEngineFeatureModule> MakeModule(Core& core, const char* name, FeatureFactory factory);

// The feature behind a module made by MakeModule(), or null for any other module.
Feature* FeatureOfModule(IEngineFeatureModule& module) noexcept;

} // namespace detail

// The feature behind `module`, or null when the module does not come from GTE_REGISTER_RENDER_FEATURE.
inline Feature* FindFeature(IEngineFeatureModule& module) noexcept
{
    return detail::FeatureOfModule(module);
}

// The feature of type T behind `module`. Throws std::logic_error when the module does not hold a T.
template <typename T>
T& FeatureOf(IEngineFeatureModule& module)
{
    T* feature = dynamic_cast<T*>(detail::FeatureOfModule(module));
    if (feature == nullptr) {
        throw std::logic_error("FeatureOf<T>: the module does not hold a feature of that type");
    }
    return *feature;
}

} // namespace gte::fx

// Registers ClassName (derived from fx::Feature, constructible from fx::FeatureHost&) as a built-in render feature.
#define GTE_REGISTER_RENDER_FEATURE(NameLiteral, ClassName)                                                          \
    namespace {                                                                                                      \
    std::unique_ptr<::gte::IEngineFeatureModule> GteMakeRenderFeatureModule##ClassName(::gte::Core& core)            \
    {                                                                                                                \
        return ::gte::fx::detail::MakeModule(core, NameLiteral,                                                      \
            [](::gte::fx::FeatureHost& host) -> std::unique_ptr<::gte::fx::Feature> {                                \
                return std::make_unique<ClassName>(host);                                                            \
            });                                                                                                      \
    }                                                                                                                \
    }                                                                                                                \
    GTE_REGISTER_BUILTIN_FEATURE_MODULE(NameLiteral, &GteMakeRenderFeatureModule##ClassName)
