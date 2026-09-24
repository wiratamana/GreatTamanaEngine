#pragma once

namespace gte {

// A short, stable, human-readable identity for logs/diagnostics - see the
// source design doc's Section 3.2. Plain fixed-size char buffers, never
// std::string (Locked Design Decision #3, PHASE0_MASTER_STRATEGY.md).
struct GtePluginModuleInfo {
    char name[64] = {};        // e.g. "DemoRenderFeature"
    char version[16] = {};     // e.g. "1.0.0" - free-form, plugin author's own scheme, not compared by the host
    char description[128] = {}; // one-line, human-readable
};

} // namespace gte
