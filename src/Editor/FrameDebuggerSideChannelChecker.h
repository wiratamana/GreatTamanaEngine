#pragma once

// Detects a disabled pass whose hand-off data is still published. The check covers a curated list of
// blackboard keys; a new key with this risk must be added to KnownRiskBlackboardKeyRules().

#include "../Renderer/RenderGraph/RenderPipeline.h"

#include <functional>
#include <string>
#include <vector>

namespace gte {

// One curated key: published only while its gating toggle is enabled.
struct KnownRiskBlackboardKeyRule {
    rg::RenderPassId key;
    std::string keyDebugName;     // Shown in the log message.
    std::string gatingToggleName; // The toggle whose disabled state must also mean "key not published".
};

// The curated table; the same vector on every call.
const std::vector<KnownRiskBlackboardKeyRule>& KnownRiskBlackboardKeyRules();

// Names of the rules whose toggle is disabled while their key was published this frame.
// An empty callable yields an empty result.
std::vector<std::string> DetectDisabledPassBlackboardKeyLeaks(const std::vector<KnownRiskBlackboardKeyRule>& rules,
    const std::function<bool(const std::string&)>& isEnabledLookup,
    const std::function<bool(rg::RenderPassId)>& wasPublishedLookup);

} // namespace gte
