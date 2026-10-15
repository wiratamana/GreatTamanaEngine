#pragma once

#include "FeatureSetup.h"

#include <optional>
#include <string>

namespace gte {
class Core;
class Registry;
} // namespace gte

namespace gte::fx {

// What a feature may ask of the engine. A narrow facade: no renderer, no render graph, no raw device.
class FeatureHost {
public:
    explicit FeatureHost(Core& core) noexcept
        : m_core(core)
    {
    }

    Registry& Scene() const noexcept;

private:
    Core& m_core;
};

// Base class of a render feature. Setup() runs once after construction and only collects registrations.
class Feature {
public:
    virtual ~Feature() = default;

    // Stable name; also the owner shown for the feature's passes.
    virtual const char* Name() const = 0;

    virtual void Setup(FeatureSetup& setup) = 0;
};

// Receives the collected registrations once a whole Setup() has succeeded.
class IRegistrationSink {
public:
    virtual ~IRegistrationSink() = default;

    // May throw; the first failure stops the commit.
    virtual void Commit(const Registration& registration) = 0;
};

// Runs feature.Setup() and commits to `sink` only if Setup() neither threw nor recorded an error, so a feature is
// never half registered because of its own mistake. Returns the failure text, or nothing on success.
std::optional<std::string> SetupAndCommit(Feature& feature, IRegistrationSink& sink);

} // namespace gte::fx
