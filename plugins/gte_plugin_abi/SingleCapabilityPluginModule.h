#pragma once

#include "GtePluginModuleInfo.h"
#include "IPluginModule.h"

#include <cstring>

namespace gte {

// Bounded, ALWAYS-null-terminated copy into a fixed GtePluginModuleInfo -
// removes the "std::strncpy(..., sizeof(field) - 1)" idiom every plugin
// author had to get right by hand (a real, live robustness gap closed at the
// SOURCE instead of relying on every future plugin author's own discipline -
// see editor-core-separation-4 campaign's PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md
// for the matching HOST-side defensive read, this is the PLUGIN-side
// defensive write half of the same concern).
inline GtePluginModuleInfo MakeModuleInfo(const char* name, const char* version, const char* description) noexcept
{
    GtePluginModuleInfo info{};
    auto copyBounded = [](char* dest, std::size_t destSize, const char* src) {
        std::size_t i = 0;
        for (; src[i] != '\0' && i + 1 < destSize; ++i) { dest[i] = src[i]; }
        dest[i] = '\0';
    };
    copyBounded(info.name, sizeof(info.name), name);
    copyBounded(info.version, sizeof(info.version), version);
    copyBounded(info.description, sizeof(info.description), description);
    return info;
}

// The reusable IPluginModule wrapper for the common "this plugin implements
// EXACTLY ONE capability" case (three of this repo's own four demo plugins,
// after this campaign - demo_render_feature, demo_render_feature_second,
// demo_editor_panel). A plugin that implements 2+ capabilities still
// hand-writes its own IPluginModule, exactly like today - this template does
// not replace that path, only removes the boilerplate for the simpler, more
// common one.
template <typename CapabilityInterface>
class SingleCapabilityPluginModule final : public IPluginModule {
public:
    SingleCapabilityPluginModule(CapabilityInterface& capability, const char* capabilityNameAndVersion, GtePluginModuleInfo info) noexcept
        : m_capability(capability)
        , m_capabilityNameAndVersion(capabilityNameAndVersion)
        , m_info(info)
    {
    }

    void* QueryCapability(const char* nameAndVersion) override
    {
        return (std::strcmp(nameAndVersion, m_capabilityNameAndVersion) == 0)
            ? static_cast<void*>(&m_capability)
            : nullptr;
    }

    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override { outInfo = m_info; }

private:
    CapabilityInterface& m_capability;
    const char* m_capabilityNameAndVersion;
    GtePluginModuleInfo m_info;
};

// editor-core-separation-5 campaign, PHASE1 - the zero-capability mirror of
// SingleCapabilityPluginModule<T> above, for a plugin that implements NO
// capability interface at all (demo_hello_world's own exact shape, migrated
// in PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md). Flagged by
// the source proposal document itself ("What about demo_hello_world (zero
// capabilities)?") as a natural follow-up for full consistency across every
// demo plugin - QueryCapability() unconditionally returns nullptr, never
// guesses, never partially implements a capability it wasn't given.
class ZeroCapabilityPluginModule final : public IPluginModule {
public:
    explicit ZeroCapabilityPluginModule(GtePluginModuleInfo info) noexcept : m_info(info) { }

    void* QueryCapability(const char*) override { return nullptr; }

    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override { outInfo = m_info; }

private:
    GtePluginModuleInfo m_info;
};

} // namespace gte
