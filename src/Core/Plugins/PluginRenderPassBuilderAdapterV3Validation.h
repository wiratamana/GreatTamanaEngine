#pragma once

// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.6) - the PURE, plain-
// data validation logic PluginRenderPassBuilderAdapter_v3's own
// Dispatch()/DrawFullscreenTriangle()/CreateTexture()/CreateBuffer() methods
// use (Step 3.4) - extracted here so it is Tier-1-testable with NO live
// VkDevice/Renderer& involved anywhere, per AGENTS.md's own "Testability &
// Regression Safety" instruction: "Before wiring new logic directly into a
// GPU/SDL-owning class, ask whether it can instead be extracted as a small
// pure function/class that takes already-resolved plain values - if it can,
// do that". Mirrors PluginRenderResourceTranslation.h's own PHASE1 precedent
// for "pure logic lives in its own small header, separate from the
// GPU-owning class that calls it".
//
// See tests/Core/Plugins/PluginRenderPassBuilderAdapterV3ValidationTests.cpp
// for the matching Tier-1 test coverage.

#include <cstddef>
#include <cstdint>

namespace gte {

// Mirrors PluginRenderPassBuilderAdapter_v3::ResolveTranslatedTexture()/
// ResolveBuffer()'s own shared handle-validation check: a handle is valid
// only if its `generation` matches the CURRENT adapter instance's own
// generation (never per-frame-reset/per-handle - a fresh, process-wide
// monotonic value assigned once, at adapter-construction time - see that
// class's own doc comment) AND its `index` is within the live translation
// table's current size.
inline bool IsPluginResourceHandleValid(std::uint32_t handleGeneration, std::uint32_t handleIndex,
    std::uint32_t currentAdapterGeneration, std::size_t tableSize) noexcept
{
    return handleGeneration == currentAdapterGeneration && static_cast<std::size_t>(handleIndex) < tableSize;
}

// Locked Architecture Decision #12 (PHASE0_MASTER_STRATEGY.md) - the
// 32-CreateTexture()/CreateBuffer()-calls-per-AddRenderGraphPasses()-
// invocation cap. `alreadyCreatedCount` is the count of resources ALREADY
// created BEFORE this call (0-based) - returns true if creating one MORE
// resource (this call) is still allowed.
inline bool IsWithinResourceCreationCountCap(std::uint32_t alreadyCreatedCount) noexcept
{
    constexpr std::uint32_t kMaxResourceCreationCallsPerInvocation = 32;
    return alreadyCreatedCount < kMaxResourceCreationCallsPerInvocation;
}

// Locked Architecture Decision #12 - the 8192x8192 per-texture dimension cap.
inline bool IsWithinTextureDimensionCap(std::uint32_t width, std::uint32_t height) noexcept
{
    constexpr std::uint32_t kMaxTextureDimension = 8192;
    return width <= kMaxTextureDimension && height <= kMaxTextureDimension;
}

// Locked Product Decision #2 (PHASE0_MASTER_STRATEGY.md) - the
// 64-groups-per-dimension IPluginCommandRecorder::Dispatch() cap, checked
// independently per axis.
inline bool IsWithinDispatchGroupCountCap(
    std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ) noexcept
{
    constexpr std::uint32_t kMaxGroupsPerDimension = 64;
    return groupsX <= kMaxGroupsPerDimension && groupsY <= kMaxGroupsPerDimension
        && groupsZ <= kMaxGroupsPerDimension;
}

// Dispatch()/DrawFullscreenTriangle() step 3 - a registered operation's own
// `maxParamBytes` must match a caller-supplied `paramSize` EXACTLY (never
// merely `<= 128`) - see PluginRenderOperationRegistry's own PluginRenderOpInfo
// doc comment for why this is a real Vulkan-push-constant-layout correctness
// requirement, not just a logical-mismatch nicety.
inline bool IsExactParamSizeMatch(std::size_t paramSize, std::size_t opMaxParamBytes) noexcept
{
    return paramSize == opMaxParamBytes;
}

// Dispatch()/DrawFullscreenTriangle() step 5 - per-slot bound-resource
// validation outcome.
enum class PluginOpSlotBindingResult : std::uint8_t {
    Ok,
    Unbound,                       // no BindTexture()/BindBuffer() call covered this slot this `execute` invocation.
    WrongKind,                     // a BindTexture() call against a buffer-kind slot, or vice versa.
    PrivateOutputTargetAsSampler,  // a CombinedImageSampler slot bound to the plugin's own private output target.
};

// `slotIsBuffer`/`isCombinedImageSamplerSlot` describe the REGISTERED
// operation's own slot table entry (PluginRenderOpSlot); `bindingHasValue`/
// `bindingIsBuffer` describe what was ACTUALLY bound via BindTexture()/
// BindBuffer() this `execute` invocation; `boundIsPrivateOutputTarget` is
// only meaningful when `bindingHasValue && !bindingIsBuffer` (a texture
// binding) - true when that specific bound PluginTextureHandle resolves to
// the plugin's own cached GetPrivateOutputTarget() entry (Step 2.5's own
// "no external sampler is tracked for a plugin's own in-progress private
// output" rule).
inline PluginOpSlotBindingResult ValidatePluginOpSlotBinding(bool slotIsBuffer, bool bindingHasValue,
    bool bindingIsBuffer, bool isCombinedImageSamplerSlot, bool boundIsPrivateOutputTarget) noexcept
{
    if (!bindingHasValue) {
        return PluginOpSlotBindingResult::Unbound;
    }
    if (bindingIsBuffer != slotIsBuffer) {
        return PluginOpSlotBindingResult::WrongKind;
    }
    if (isCombinedImageSamplerSlot && boundIsPrivateOutputTarget) {
        return PluginOpSlotBindingResult::PrivateOutputTargetAsSampler;
    }
    return PluginOpSlotBindingResult::Ok;
}

} // namespace gte
