#pragma once

// Renderer-internal view of rg::GpuBuffer. Not for code outside src/Renderer.

#include "GpuBuffer.h"
#include "../Buffer.h"

#include <memory>
#include <string>

namespace gte::rg {

struct GpuBufferAccess {
    static GpuBuffer Adopt(std::shared_ptr<Buffer> buffer, const char* name, GpuBufferUse use);
    static Buffer& Underlying(GpuBuffer& buffer);
    static const Buffer& Underlying(const GpuBuffer& buffer);
    static std::shared_ptr<Buffer> Shared(const GpuBuffer& buffer);
    static Renderer& RendererOf(DeviceRef device, const char* what);
};

// How a GpuBufferUse maps onto the engine's buffer usage and memory kind; `error` is set when the use is invalid.
struct GpuBufferPlan {
    BufferUsage usage = BufferUsage::None;
    BufferMemoryUsage memory = BufferMemoryUsage::GpuOnly;
    std::string error;

    bool Ok() const noexcept { return error.empty(); }
};

GpuBufferPlan PlanGpuBuffer(GpuBufferUse use);

} // namespace gte::rg
