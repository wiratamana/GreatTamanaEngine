#include "GpuBuffer.h"

#include "GpuBufferInternal.h"
#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace gte::rg {

struct GpuBuffer::Impl {
    std::shared_ptr<Buffer> buffer;
    const char* name = "";
    GpuBufferUse use = GpuBufferUse::None;
    mutable bool refusalLogged = false;
};

namespace {

constexpr GpuBufferUse kAnyDeviceUse = GpuBufferUse::Storage | GpuBufferUse::Uniform | GpuBufferUse::Indirect
    | GpuBufferUse::Vertex | GpuBufferUse::TransferSrc | GpuBufferUse::TransferDst;

void AddUsage(BufferUsage& usage, GpuBufferUse use, GpuBufferUse flag, BufferUsage bit) noexcept
{
    if (HasUse(use, flag)) {
        usage |= bit;
    }
}



} // namespace

GpuBufferPlan PlanGpuBuffer(GpuBufferUse use)
{
    GpuBufferPlan plan;
    const bool hostWrite = HasUse(use, GpuBufferUse::HostWrite);
    const bool hostRead = HasUse(use, GpuBufferUse::HostRead);
    if (hostWrite && hostRead) {
        plan.error = "a buffer is either HostWrite or HostRead, not both";
        return plan;
    }
    if (!HasUse(use, kAnyDeviceUse)) {
        plan.error = "the buffer has no use";
        return plan;
    }
    AddUsage(plan.usage, use, GpuBufferUse::Storage, BufferUsage::StorageBuffer);
    AddUsage(plan.usage, use, GpuBufferUse::Uniform, BufferUsage::UniformBuffer);
    AddUsage(plan.usage, use, GpuBufferUse::Indirect, BufferUsage::IndirectBuffer);
    AddUsage(plan.usage, use, GpuBufferUse::Vertex, BufferUsage::VertexBuffer);
    AddUsage(plan.usage, use, GpuBufferUse::TransferSrc, BufferUsage::TransferSrc);
    AddUsage(plan.usage, use, GpuBufferUse::TransferDst, BufferUsage::TransferDst);
    if (hostWrite) {
        plan.memory = BufferMemoryUsage::CpuToGpu;
    } else if (hostRead) {
        plan.memory = BufferMemoryUsage::GpuToCpu;
    } else {
        plan.memory = BufferMemoryUsage::GpuOnly;
        plan.usage |= BufferUsage::TransferDst;
    }
    return plan;
}

GpuBuffer GpuBufferAccess::Adopt(std::shared_ptr<Buffer> buffer, const char* name, GpuBufferUse use)
{
    auto impl = std::make_unique<GpuBuffer::Impl>();
    impl->buffer = std::move(buffer);
    impl->name = name != nullptr ? name : "";
    impl->use = use;
    return GpuBuffer(std::move(impl));
}

Buffer& GpuBufferAccess::Underlying(GpuBuffer& buffer)
{
    return *buffer.m_impl->buffer;
}

const Buffer& GpuBufferAccess::Underlying(const GpuBuffer& buffer)
{
    return *buffer.m_impl->buffer;
}

std::shared_ptr<Buffer> GpuBufferAccess::Shared(const GpuBuffer& buffer)
{
    return buffer.m_impl != nullptr ? buffer.m_impl->buffer : nullptr;
}

namespace {

void RefuseOnce(const GpuBuffer::Impl& impl, const std::string& text)
{
    if (!impl.refusalLogged) {
        impl.refusalLogged = true;
        GTE_LOG_ERROR("GpuBuffer", std::string("Buffer '") + impl.name + "': " + text);
    }
}

} // namespace

Renderer& GpuBufferAccess::RendererOf(DeviceRef device, const char* what)
{
    if (!device.IsValid()) {
        throw std::runtime_error(std::string(what) + ": the device reference is empty");
    }
    return *device.m_renderer;
}

GpuBuffer::GpuBuffer(DeviceRef device, std::size_t size, const char* name, GpuBufferUse use)
{
    const GpuBufferPlan plan = PlanGpuBuffer(use);
    if (!plan.Ok()) {
        throw std::invalid_argument("GpuBuffer '" + std::string(name != nullptr ? name : "") + "': " + plan.error);
    }
    Renderer& renderer = GpuBufferAccess::RendererOf(device, "GpuBuffer");
    m_impl = std::make_unique<Impl>();
    m_impl->buffer = std::make_shared<Buffer>(renderer.CreateBuffer(size, plan.usage, plan.memory, name));
    m_impl->name = name != nullptr ? name : "";
    m_impl->use = use;
}

GpuBuffer::GpuBuffer(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

GpuBuffer GpuBuffer::CreateWithData(DeviceRef device, const void* data, std::size_t size, const char* name, GpuBufferUse use)
{
    if (HasUse(use, GpuBufferUse::HostWrite) || HasUse(use, GpuBufferUse::HostRead)) {
        throw std::invalid_argument("GpuBuffer::CreateWithData needs a device-local use");
    }
    const GpuBufferPlan plan = PlanGpuBuffer(use);
    if (!plan.Ok()) {
        throw std::invalid_argument("GpuBuffer '" + std::string(name != nullptr ? name : "") + "': " + plan.error);
    }
    Renderer& renderer = GpuBufferAccess::RendererOf(device, "GpuBuffer::CreateWithData");
    return GpuBufferAccess::Adopt(
        std::make_shared<Buffer>(renderer.CreateDeviceLocalBuffer(data, size, plan.usage, name)), name, use);
}

GpuBuffer::GpuBuffer() noexcept = default;
GpuBuffer::~GpuBuffer() = default;
GpuBuffer::GpuBuffer(GpuBuffer&&) noexcept = default;
GpuBuffer& GpuBuffer::operator=(GpuBuffer&&) noexcept = default;

bool GpuBuffer::IsValid() const noexcept
{
    return m_impl != nullptr && m_impl->buffer != nullptr;
}

std::size_t GpuBuffer::Size() const noexcept
{
    return IsValid() ? static_cast<std::size_t>(m_impl->buffer->Size()) : 0;
}

const char* GpuBuffer::Name() const noexcept
{
    return m_impl != nullptr ? m_impl->name : "";
}

BufferObjectHandle GpuBuffer::Handle() const noexcept
{
    return IsValid() ? m_impl->buffer->ObjectHandle() : BufferObjectHandle::Invalid;
}

bool GpuBuffer::Upload(const void* data, std::size_t size, std::size_t offset)
{
    if (!IsValid()) {
        return false;
    }
    if (!HasUse(m_impl->use, GpuBufferUse::HostWrite)) {
        RefuseOnce(*m_impl, "Upload needs a HostWrite buffer");
        return false;
    }
    if (data == nullptr || !GpuBufferRangeFits(Size(), offset, size)) {
        RefuseOnce(*m_impl, "Upload range " + std::to_string(offset) + "+" + std::to_string(size)
                + " does not fit in " + std::to_string(Size()) + " bytes");
        return false;
    }
    m_impl->buffer->Upload(data, size, offset);
    return true;
}

bool GpuBuffer::Download(void* data, std::size_t size, std::size_t offset) const
{
    if (!IsValid()) {
        return false;
    }
    if (!HasUse(m_impl->use, GpuBufferUse::HostRead)) {
        RefuseOnce(*m_impl, "Download needs a HostRead buffer");
        return false;
    }
    if (data == nullptr || !GpuBufferRangeFits(Size(), offset, size)) {
        RefuseOnce(*m_impl, "Download range " + std::to_string(offset) + "+" + std::to_string(size)
                + " does not fit in " + std::to_string(Size()) + " bytes");
        return false;
    }
    return m_impl->buffer->Download(data, size, offset);
}

} // namespace gte::rg
