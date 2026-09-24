#pragma once

namespace gte {

struct GtePluginModuleInfo;

// The one interface every plugin implements directly (source design doc,
// Section 3.2). Pure virtual, no data members, no multiple inheritance, no
// exceptions crossing this boundary - safe under Locked Design Decision #3.
class IPluginModule {
public:
    virtual ~IPluginModule() = default;

    // The generic capability lookup (source design doc Section 4.3) -
    // COM/Source-Engine-style string-versioned interface query, e.g.
    // QueryCapability("IRenderFeatureModule_v1"). Returns nullptr for any
    // capability this plugin doesn't implement, or any version string it
    // doesn't recognize - never guesses, never returns a mismatched type
    // through a stale pointer. The returned pointer's REAL static type is
    // always one of gte_plugin_abi's own curated interface types (e.g.
    // IRenderFeatureModule_v1*) - the caller is responsible for calling
    // QueryCapability() with the exact name/version string documented next
    // to that interface's own declaration, and static_cast-ing the result
    // to that exact type, exactly mirroring COM's QueryInterface()
    // discipline.
    virtual void* QueryCapability(const char* capabilityNameAndVersion) = 0;

    virtual void GetModuleInfo(GtePluginModuleInfo& outInfo) const = 0;
};

} // namespace gte
