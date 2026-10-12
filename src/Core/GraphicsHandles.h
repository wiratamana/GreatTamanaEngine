#pragma once

#include <cstdint>

namespace gte {

// Strongly-typed, trivially-copyable, 8-byte opaque handles standing in for
// a native Vulkan object outside src/Renderer. Each packs a generational
// slot index (low 32 bits = index, high 32 bits = generation) minted by a
// Renderer-internal registry - never constructed from a raw integer by
// calling code. Renderer/GraphicsHandleRegistry.h is the ONLY place these
// unwrap to a real Vk* value.
enum class DescriptorSetHandle : std::uint64_t { Invalid = 0 };
enum class SamplerHandle : std::uint64_t { Invalid = 0 };
enum class ImageViewHandle : std::uint64_t { Invalid = 0 };

// Handle to a caller-built descriptor-set LAYOUT (not a set) - see
// Renderer::CreateDescriptorSetLayout()/DestroyDescriptorSetLayout().
enum class DescriptorSetLayoutHandle : std::uint64_t { Invalid = 0 };

// Handle to a persistent, non-graph-tracked VkBuffer (e.g. a small
// CpuToGpu buffer re-uploaded every frame) - see Buffer::ObjectHandle().
enum class BufferObjectHandle : std::uint64_t { Invalid = 0 };

} // namespace gte
