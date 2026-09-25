#pragma once

// editor-core-separation-9 campaign, PHASE1
// (PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md) - pure, Tier-1-testable
// gte_core-internal translation functions mapping the new, curated,
// ABI-safe plugin vocabulary (plugins/gte_plugin_abi/PluginRenderResource.h)
// onto the real internal rg::ResourceAccess/rg::TextureDesc/rg::BufferDesc
// types (src/Renderer/RenderGraph/RenderGraphTypes.h). No live VkDevice/
// Renderer& involved anywhere in this file - mirrors
// tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp's own established
// "plain data in, plain data out" precedent for this exact class of pure
// enum/struct mapping logic. Nothing calls these functions yet - PHASE2's
// real PluginRenderPassBuilderAdapter_v3 is the first real consumer.

#include "../../../plugins/gte_plugin_abi/PluginRenderResource.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {

// Exhaustive switch, no `default:` case (mirrors rg::IsWriteAccess()/
// rg::ToString(ResourceAccess)'s own established convention,
// RenderGraphTypes.h) - a future PluginResourceAccess enumerator therefore
// fails to compile here until this function is updated too.
rg::ResourceAccess ToRgAccess(PluginResourceAccess access) noexcept;

// VK_FORMAT_UNDEFINED is never produced by this function - every
// PluginTextureDesc::Format enumerator maps to a concrete VkFormat.
// `hasDepth` is always false on the returned rg::TextureDesc: a
// plugin-created transient texture never carries a companion depth buffer
// this campaign (PluginTextureDesc has no such field at all).
rg::TextureDesc ToRgTextureDesc(const PluginTextureDesc& desc) noexcept;

// `sizeBytes` round-trips exactly into rg::BufferDesc::size. `usage` is
// always the same fixed VK_BUFFER_USAGE_STORAGE_BUFFER_BIT flag combination
// every internal compute-consumer buffer in this engine already uses for an
// equivalent purpose (see src/Game/Animation/GpuSkinningRigCache.cpp's own
// GpuSkinningBindPose/GpuSkinningWeights/GpuSkinningUv buffers) - sufficient
// for both PluginResourceAccess::ComputeShaderRead and ComputeShaderWrite,
// since a plugin-created buffer is always storage-buffer-capable.
rg::BufferDesc ToRgBufferDesc(const PluginBufferDesc& desc) noexcept;

} // namespace gte
