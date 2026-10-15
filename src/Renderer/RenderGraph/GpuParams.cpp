#include "GpuParams.h"

#include "RenderGraphBuilder.h"
#include "../Buffer.h"
#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <string>

namespace gte::rg {

GpuParamsUploadDecision DecideGpuParamsUpload(
    std::size_t existingSize, std::size_t newSize, bool pipelinedRegime) noexcept
{
    if (newSize == 0) {
        return GpuParamsUploadDecision::RefuseEmpty;
    }
    if (pipelinedRegime) {
        return GpuParamsUploadDecision::RefusePipelined;
    }
    if (existingSize == 0) {
        return GpuParamsUploadDecision::Create;
    }
    return existingSize == newSize ? GpuParamsUploadDecision::Overwrite : GpuParamsUploadDecision::RefuseSizeMismatch;
}

GpuParamsBuffer::GpuParamsBuffer() noexcept = default;
GpuParamsBuffer::~GpuParamsBuffer() = default;
GpuParamsBuffer::GpuParamsBuffer(GpuParamsBuffer&&) noexcept = default;
GpuParamsBuffer& GpuParamsBuffer::operator=(GpuParamsBuffer&&) noexcept = default;

bool GpuParamsBuffer::Upload(RenderGraphBuilder& builder, const void* data, std::size_t size, const char* debugName)
{
    const GpuParamsUploadDecision decision = DecideGpuParamsUpload(m_size, size, builder.IsPersistentRegimePipelined());

    const char* refusal = nullptr;
    switch (decision) {
    case GpuParamsUploadDecision::Create:
    case GpuParamsUploadDecision::Overwrite:
        break;
    case GpuParamsUploadDecision::RefuseEmpty:
        refusal = "the upload is empty";
        break;
    case GpuParamsUploadDecision::RefuseSizeMismatch:
        refusal = "the size differs from the first upload";
        break;
    case GpuParamsUploadDecision::RefusePipelined:
        refusal = "frames in flight would read a buffer the CPU is overwriting";
        break;
    }
    if (refusal == nullptr && data == nullptr) {
        refusal = "the data pointer is null";
    }
    if (refusal == nullptr && decision == GpuParamsUploadDecision::Create && builder.RendererOrNull() == nullptr) {
        refusal = "the builder has no renderer";
    }
    if (refusal != nullptr) {
        if (!m_refusalLogged) {
            m_refusalLogged = true;
            GTE_LOG_ERROR("GpuParams",
                std::string("Upload to \"") + (debugName != nullptr ? debugName : "") + "\" refused: " + refusal + ".");
        }
        return false;
    }

    if (decision == GpuParamsUploadDecision::Create) {
        m_buffer = std::make_unique<Buffer>(builder.OwningRenderer().CreateBuffer(
            size, BufferUsage::StorageBuffer, BufferMemoryUsage::CpuToGpu, debugName));
        m_size = size;
    }
    m_buffer->Upload(data, size);
    return true;
}

BufferObjectHandle GpuParamsBuffer::Handle() const noexcept
{
    return m_buffer != nullptr ? m_buffer->ObjectHandle() : BufferObjectHandle::Invalid;
}

} // namespace gte::rg
