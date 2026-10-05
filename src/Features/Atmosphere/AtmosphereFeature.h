#pragma once

#include "AtmosphereLutRenderer.h"
#include "AtmosphereTypes.h"

namespace gte {

class Core;

namespace rg {
class RenderPassToggleRegistry;
} // namespace rg

// The ONE object owning everything feature-specific - AtmosphereSettings/
// AtmosphereLutRenderer and every one of this feature's render-graph pass
// registrations. Constructed and torn down by whichever bootstrap code
// decides this feature should exist for this run - never by Core itself.
class AtmosphereFeature {
public:
    explicit AtmosphereFeature(Core& core);

    AtmosphereFeature(const AtmosphereFeature&) = delete;
    AtmosphereFeature& operator=(const AtmosphereFeature&) = delete;
    AtmosphereFeature(AtmosphereFeature&&) = delete;
    AtmosphereFeature& operator=(AtmosphereFeature&&) = delete;

    AtmosphereSettings& Settings() noexcept { return m_settings; }
    AtmosphereLutRenderer& Renderer() noexcept { return m_renderer; }

private:
    // Registers all 4 render passes + 2 finalize-for-sampling hooks onto
    // m_core. Called once, from the constructor.
    void RegisterPasses();

    Core& m_core;
    rg::RenderPassToggleRegistry* m_toggleRegistry = nullptr;
    AtmosphereSettings m_settings;
    AtmosphereLutRenderer m_renderer;
};

// Shaped exactly like this engine's own external-Project registration-
// function convention - see Core::RegisterProjectRenderPassProvider().
void RegisterAtmosphereFeature(Core& core);

} // namespace gte
