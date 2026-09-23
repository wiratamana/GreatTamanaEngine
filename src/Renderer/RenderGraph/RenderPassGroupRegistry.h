#pragma once

#include "RenderGraphTypes.h" // RenderPassTag / RenderPassTagMask (PHASE1 relocated these here)

#include <cstddef>
#include <optional>

namespace gte::rg {

// render-pass-7 campaign (task_manager/render-pass-7), PHASE2 - Core Campaign 1's own new
// generic facility (source doc: CORE_EXPANSION_STRATEGY_v2.md, Section 3, Core Campaign 1).
// Lets ANY Layer-2 module register a human-readable Frame-Debugger-tree heading for its own
// RenderPassTag, from that module's own header/source file - Core itself never learns or
// cares what any tag or heading MEANS. See RenderPassTag's own doc comment
// (RenderGraphTypes.h) for the "no feature-specific tag VALUES live in Core" rule this
// registry exists to serve without breaking.
//
// PHASE2 is pure, additive vocabulary + mechanism with ZERO real consumers wired up yet -
// PHASE3 (Atmosphere self-registration) and PHASE4 (the Frame Debugger consumer) are the
// first real callers, not this phase. Always compiled in both GTE_ENABLE_EDITOR=ON and =OFF
// builds - unlike RenderPipeline.cpp's own debug-only PassIdDebugNameRegistry(), this facility
// must work identically in release, since it drives real, always-visible Editor UI.

// Registers (or, if `tag.bit` is already registered, RE-registers under a possibly-different
// `uiHeading`) the heading every surviving pass carrying this tag should be grouped under.
// Idempotent by tag bit - calling this twice for the SAME tag.bit is always safe (e.g. a
// feature class constructed more than once in the same process, or a test re-running this
// call). `uiHeading` MUST be a string-literal/static-storage-duration const char* - never
// owned or copied, mirrors PassRecord::name's own identical rule.
//
// Debug-only correctness net: if `tag.bit` was already registered under a DIFFERENT
// `uiHeading` (compared by pointer OR by content - mirrors RenderPipeline::Unregister()'s own
// "pointer-OR-content match" convention), this logs ONE soft warning - never a crash, never
// an assert - a mislabeled Frame Debugger heading is a cosmetic bug, not a correctness
// hazard, exactly like RenderPassBlackboard::ReportUnusedPublishesIfAny()'s own identical
// "soft, non-fatal" precedent for an analogous developer-facing nuisance.
void RegisterPassGroupLabel(RenderPassTag tag, const char* uiHeading) noexcept;

// Registration-order enumeration - lets a consumer (PHASE4's Frame Debugger) walk every
// registered (tag, label) pair in the EXACT order features registered them, which is what
// produces a deterministic, first-registered-wins Frame Debugger tree bucket ORDER (never
// alphabetical, never tied to real per-frame pass execution order).
std::size_t PassGroupLabelCount() noexcept;
RenderPassTag PassGroupLabelTagAt(std::size_t index) noexcept;   // Precondition: index < PassGroupLabelCount().
const char* PassGroupLabelUiHeadingAt(std::size_t index) noexcept; // Precondition: index < PassGroupLabelCount().

// Returns the registration-order INDEX (suitable for PassGroupLabelUiHeadingAt() above) of
// the FIRST registered entry whose tag bit is set anywhere in `tags`, or std::nullopt if
// `tags` carries no registered bit at all (the common "untagged"/"tagged but nobody
// registered a heading for it" case - e.g. GPU Skinning's own tag today, PHASE3). If a pass
// somehow carries more than one REGISTERED tag bit at once, the FIRST-REGISTERED one wins -
// a documented, deterministic tie-break, not a real scenario in production today.
std::optional<std::size_t> FindPassGroupIndexForTags(RenderPassTagMask tags) noexcept;

// Testing-only (mirrors this codebase's own "...ForTesting()" convention, e.g.
// FrameProfiler::ResetForTesting() / RenderPassBlackboard::SlotCapacityForTesting()) - clears
// every registered entry. REQUIRED so Tier-1 tests never leak state into each other via this
// otherwise-global, process-lifetime registry - call this at the start of every test that
// touches this registry (directly, or indirectly via constructing a self-registering class
// like AtmosphereLutRenderer, PHASE3).
void ResetPassGroupRegistryForTesting() noexcept;

} // namespace gte::rg
