#pragma once

// editor-core-separation-20 campaign, PHASE2
// (task_manager/editor-core-separation-20/PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md)
// - the ONE pure decision every one of AtmosphereLutRenderer's 5 toggle-aware
// AddXxxPass() methods needs before it is allowed to call
// builder.AddRenderPass() at all: "is this pass individually enabled THIS
// FRAME, and are every one of its upstream TextureHandle/VolumeTextureHandle
// dependencies already valid THIS FRAME?" Extracted out of those methods'
// own guard clauses specifically so this exact branching is Tier-1-testable
// with ZERO live VkDevice/Renderer/RenderGraphBuilder involved (see
// AGENTS.md's "Testability & Regression Safety" section) - the 5 methods
// themselves remain genuinely Tier-2 (they still need a live GPU to do
// anything useful once this check passes), but the SKIP DECISION itself
// never touched the GPU in the first place, so it does not need to stay
// bundled inline with the GPU-owning class's own .cpp file.
//
// This header deliberately does NOT include RenderPassToggleRegistry.h or
// RenderGraphTypes.h - every caller resolves its own two booleans FIRST
// (`toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled(name)`
// for the first; a logical AND of every relevant TextureHandle::IsValid()/
// VolumeTextureHandle::IsValid() call for the second) and passes them in as
// plain bool values - this keeps this function usable from a Tier-1 test
// with no dependency on either type at all.
//
// A caller with NO upstream handle to check at all (AddTransmittanceLutPass,
// the very first pass in the chain) passes `allUpstreamHandlesValid = true`
// unconditionally - vacuously true, matching how an empty AND is true.
namespace gte {

inline bool ShouldDeclareAtmospherePassThisFrame(bool passEnabledThisFrame, bool allUpstreamHandlesValid) noexcept
{
    return passEnabledThisFrame && allUpstreamHandlesValid;
}

} // namespace gte
