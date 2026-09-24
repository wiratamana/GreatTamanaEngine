#pragma once

#include "GtePluginAbiFingerprint.h"
#include "IPluginModule.h"

namespace gte {

// Every plugin .dll exports EXACTLY these three extern "C" functions, under
// EXACTLY these three names (case-sensitive) - the ONLY functions ever
// resolved by literal name via GetProcAddress(). See PHASE2's PluginHost
// for the real GetProcAddress() call sites, and every demo plugin project
// (PHASE2/3/4) for the real, exported definitions.
using PFN_GTE_GetPluginAbiFingerprint = GtePluginAbiFingerprint(*)();
using PFN_GTE_CreatePluginModule = IPluginModule*(*)();
using PFN_GTE_DestroyPluginModule = void(*)(IPluginModule*);

inline constexpr const char* kGteGetPluginAbiFingerprintExportName = "GTE_GetPluginAbiFingerprint";
inline constexpr const char* kGteCreatePluginModuleExportName = "GTE_CreatePluginModule";
inline constexpr const char* kGteDestroyPluginModuleExportName = "GTE_DestroyPluginModule";

} // namespace gte

// Note (deliberate simplification from the source design doc's own Section
// 3.1 signature `IPluginModule* GTE_CreatePluginModule(IEngineServices*
// hostServices)`): this campaign's curated capability interfaces (PHASE3's
// IPluginRenderPassBuilder, PHASE4's IPluginPanelDrawContext) are each
// handed to a plugin per-call, at the exact point a capability method is
// invoked (e.g. AddRenderGraphPasses(IPluginRenderPassBuilder&)) rather
// than once, up-front, at construction time via a single monolithic
// IEngineServices* - there is no host-services object this campaign's own
// Milestones 0-3 actually need to hand over at construction time at all
// (GetModuleInfo()/render-pass-building/panel-drawing need nothing else).
// If a genuinely new capability later needs something at construction
// time, GTE_CreatePluginModule's signature is exactly where that grows a
// real parameter - deliberately NOT invented here, ahead of any real need,
// per this codebase's own "don't design for a need you don't have yet"
// precedent.
