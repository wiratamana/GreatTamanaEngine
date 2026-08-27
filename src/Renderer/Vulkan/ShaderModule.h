#pragma once

#include <volk.h>

#include <string>

namespace gte {

// Loads a compiled SPIR-V binary off disk and wraps it in a VkShaderModule -
// the one shared primitive both Pipeline (graphics) and ComputePipeline
// (compute - see ComputePipeline.h) build their own pipelines from, so this
// exact "read file bytes -> vkCreateShaderModule" dance exists in exactly
// ONE place rather than two near-identical private copies.
//
// `spirvPath` must point at an already-compiled SPIR-V binary (see
// cmake/CompileShaders.cmake's gte_add_shader(), which compiles
// src/Shaders/*.vert/*.frag/*.comp into "<exe dir>/shaders/*.spv" at build
// time) - NOT a path to GLSL source.
//
// The returned VkShaderModule is only ever needed transiently, to build a
// VkPipeline/VkComputePipeline from - the caller is responsible for
// destroying it (vkDestroyShaderModule) once that pipeline has been built,
// exactly as Pipeline's own constructor already does for its two shader
// modules today.
//
// Throws std::runtime_error (never returns VK_NULL_HANDLE) if the file
// can't be opened or vkCreateShaderModule fails - mirrors Pipeline.cpp's
// own pre-extraction error message style exactly, so this refactor changes
// no observable behavior.
VkShaderModule LoadShaderModule(VkDevice device, const std::string& spirvPath);

} // namespace gte
