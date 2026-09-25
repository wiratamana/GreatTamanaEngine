#include "PluginRenderPassBuilderAdapter_v2.h"

#include "RenderFeatureCompositor.h"

namespace gte {

void PluginRenderPassBuilderAdapter_v2::AddSolidFillPass(const char* debugName, float r, float g, float b, float a)
{
    RenderFeatureOpsPushConstants pushConstants{};
    pushConstants.opCodeAndPad[0] = 0.0f; // Solid Fill - RenderFeatureOps.comp
    pushConstants.colorRgba[0] = r;
    pushConstants.colorRgba[1] = g;
    pushConstants.colorRgba[2] = b;
    pushConstants.colorRgba[3] = a;
    m_compositor.DispatchOps(m_builder, m_privateTarget, m_privateTargetStateKey, debugName, pushConstants);
}

void PluginRenderPassBuilderAdapter_v2::AddRadialVignettePass(const char* debugName, float centerX, float centerY,
    float innerRadius, float outerRadius, float r, float g, float b, float a)
{
    RenderFeatureOpsPushConstants pushConstants{};
    pushConstants.opCodeAndPad[0] = 1.0f; // Radial Vignette - RenderFeatureOps.comp
    pushConstants.colorRgba[0] = r;
    pushConstants.colorRgba[1] = g;
    pushConstants.colorRgba[2] = b;
    pushConstants.colorRgba[3] = a;
    pushConstants.centerAndRadius[0] = centerX;
    pushConstants.centerAndRadius[1] = centerY;
    pushConstants.centerAndRadius[2] = innerRadius;
    pushConstants.centerAndRadius[3] = outerRadius;
    m_compositor.DispatchOps(m_builder, m_privateTarget, m_privateTargetStateKey, debugName, pushConstants);
}

void PluginRenderPassBuilderAdapter_v2::AddColorGradePass(const char* debugName, float brightness, float contrast,
    float saturation, float tintR, float tintG, float tintB, float tintStrength)
{
    RenderFeatureOpsPushConstants pushConstants{};
    pushConstants.opCodeAndPad[0] = 2.0f; // Color Grade - RenderFeatureOps.comp
    pushConstants.colorRgba[0] = tintR;
    pushConstants.colorRgba[1] = tintG;
    pushConstants.colorRgba[2] = tintB;
    pushConstants.gradeParams[0] = brightness;
    pushConstants.gradeParams[1] = contrast;
    pushConstants.gradeParams[2] = saturation;
    pushConstants.gradeParams[3] = tintStrength;
    m_compositor.DispatchOps(m_builder, m_privateTarget, m_privateTargetStateKey, debugName, pushConstants);
}

} // namespace gte
