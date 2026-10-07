#pragma once

// task_manager/editor-core-separation-22 campaign, PHASE6
// (PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md, Step 3.3) - Clause C of
// this campaign's own extended "iron rule" (see PHASE0_MASTER_STRATEGY.md's
// Step 1.2):
//
//   "A pass's own toggle-off state must gate EVERY observable side effect
//    its own declaration code produces - not only whether its own
//    RenderPassDesc reaches the graph, but also any data (blackboard
//    publish, cached callback, member-variable mutation) that some OTHER,
//    independently-toggled pass might read and reproduce that effect from,
//    regardless of the first pass's own disabled state."
//
// THIS IS A DELIBERATELY NARROW, BEST-EFFORT DETECTOR, NOT A FULLY GENERAL
// ONE - see PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md's own Step 3.3 and
// PHASE6_COMPLETION_REPORT.md's "honest scoping" section for the full
// reasoning why a fully general version is not attempted here: building one
// would require tracking, per blackboard key, WHICH pass published it and
// WHETHER that pass's own toggle was honored before every single publish -
// a much bigger undertaking than a permanent regression tripwire for the
// EXACT bug shape PHASE1/PHASE3 of this campaign already fixed needs to be.
// Instead, this file implements a small, explicit, HAND-MAINTAINED allowlist
// of "known-risk blackboard keys" - every key PHASE2's own audit ledger
// (PHASE2_COMPLETION_REPORT.md) identified as carrying this exact risk
// shape, whether a confirmed leak (kGameSkyBackgroundCallbackKey/
// kGameSkyBackgroundReplayCallbackKey) or already-honest but structurally
// identical (kGpuSkinningOutputsKey).
// ANY FUTURE NEW BLACKBOARD KEY WITH THIS SAME RISK SHAPE MUST BE MANUALLY
// ADDED TO KnownRiskBlackboardKeyRules() BELOW BY WHOEVER ADDS IT - this is
// NOT automatic, and this file does not claim otherwise.

#include "../Renderer/RenderGraph/RenderPipeline.h" // rg::RenderPassId, rg::RenderPassBlackboard::WasPublishedThisFrame()'s own key type.

#include <functional>
#include <string>
#include <vector>

namespace gte {

// One entry of the curated allowlist (Step 3.3 item 1). `key` is the exact
// rg::RenderPassId a real src/Core/Core.cpp provider Publish()es a
// previously-leaky (or leak-shaped) value under. This header DELIBERATELY
// duplicates each literal string that produces `key` (see this file's own
// .cpp) rather than sharing a single header-defined constant with Core.cpp
// - mirrors FrameDebuggerData.cpp's own kFrameDebuggerGameClearColor
// precedent ("duplicate a hardcoded engine constant with a comment
// documenting the value it must be kept in sync with" - already an
// accepted pattern in this codebase). RenderPassId's own hash is a pure,
// deterministic compile-time function of the literal's bytes (see
// RenderPipeline.h's own consteval operator""_passId), so two
// independently-written, byte-identical literals ALWAYS produce the exact
// same RenderPassId value - if Core.cpp's own literal for one of these keys
// ever changes, this file's own copy must be updated to match, or this
// detector silently stops matching that key (see the .cpp's own per-entry
// comment for the exact Core.cpp line each one mirrors).
struct KnownRiskBlackboardKeyRule {
    rg::RenderPassId key;
    std::string keyDebugName; // Human-readable identity, for the guard's own log message only.
    std::string gatingToggleName; // The RenderPassToggleRegistry name whose disabled state must ALSO mean "key not published".
};

// THE curated allowlist itself (Step 3.3 item 1). Returns the SAME static
// vector every call (never rebuilt/reallocated per call) - a plain,
// read-only, hand-maintained table.
const std::vector<KnownRiskBlackboardKeyRule>& KnownRiskBlackboardKeyRules();

// Pure decision, mirroring RenderPassHonestyChecker.h's own dependency-
// injection shape exactly (`isEnabledLookup`/`wasPublishedLookup` injected
// as plain callables so this function needs no live
// RenderPassToggleRegistry/RenderPassBlackboard object to unit test).
// Returns every rule's own `keyDebugName` whose `gatingToggleName` currently
// reports DISABLED via `isEnabledLookup`, yet `wasPublishedLookup(rule.key)`
// still reports true - the exact side-channel-leak shape PHASE1/PHASE3 of
// this campaign fixed (a disabled pass's own data hand-off surviving it,
// letting some OTHER, independently-toggled pass reproduce its effect). A
// rule whose toggle is enabled, or whose key was never published this frame
// either way, is never reported. Returns an empty vector if either callable
// is empty (a defensive, unreachable-in-production guard only - every real
// call site always supplies both).
std::vector<std::string> DetectDisabledPassBlackboardKeyLeaks(const std::vector<KnownRiskBlackboardKeyRule>& rules,
    const std::function<bool(const std::string&)>& isEnabledLookup,
    const std::function<bool(rg::RenderPassId)>& wasPublishedLookup);

} // namespace gte
