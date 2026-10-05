#version 450

// Depth-only write pass for a per-layer TextureArray attachment (see
// src/Editor/ArrayLayerRenderValidation.h/.cpp) - no color attachments, so
// this fragment stage writes nothing at all. Legal, standard GLSL/SPIR-V/
// Vulkan for a pipeline built with an empty colorFormats span.

void main()
{
}
