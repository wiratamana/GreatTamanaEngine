#pragma once

// Generalized early-exit guard: call this at the very TOP of any provider
// body that performs a side effect (a RenderPassBlackboard::Publish() call,
// a cached-callback hand-off, a member-variable write some OTHER,
// independently-toggled pass might later read) BEFORE that side effect
// happens - never only immediately before declaring the pass. A disabled
// pass must never leak its side effect just because the late "don't declare
// it" decision happens after the side effect already ran.
//
// `registry` may be nullptr - returns true unconditionally in that case, so
// callers never need their own separate null check. Read-only: the real
// registration (RenderPassToggleRegistry::NoteDeclaredWithOwner()) happens
// automatically, later, the moment AddRenderPass()/AddBlitPass() actually
// runs for this exact name - this guard never registers anything itself.

#include "RenderPassToggleRegistry.h"

namespace gte::rg {

inline bool ShouldDeclareBuiltInPassThisFrame(RenderPassToggleRegistry* registry, const char* name)
{
    return registry == nullptr || registry->IsEnabled(name);
}

} // namespace gte::rg
