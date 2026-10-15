#include "GpuParams.h"

#include "RenderGraphBuilder.h"
#include "../Buffer.h"
#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <exception>
#include <string>
#include <utility>

namespace gte::rg {

void GpuParamsBuffer::LogRefusalOnce(const std::string& message)
{
    if (!m_refusalLogged) {
        m_refusalLogged = true;
        GTE_LOG_ERROR("GpuParams", message);
    }
}

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
        LogRefusalOnce(std::string("Upload to \"") + (debugName != nullptr ? debugName : "") + "\" refused: " + refusal + ".");
        return false;
    }

    try {
        if (decision == GpuParamsUploadDecision::Create) {
            auto buffer = std::make_unique<GpuBuffer>(builder.OwningRenderer().Device(), size, debugName,
                GpuBufferUse::Storage | GpuBufferUse::HostWrite);
            if (!buffer->Upload(data, size)) {
                return false;
            }
            m_buffer = std::move(buffer);
            m_size = size;
        } else if (!m_buffer->Upload(data, size)) {
            return false;
        }
    } catch (const std::exception& e) {
        LogRefusalOnce(std::string("Upload to \"") + (debugName != nullptr ? debugName : "") + "\" failed: " + e.what());
        return false;
    }
    return true;
}

BufferObjectHandle GpuParamsBuffer::Handle() const noexcept
{
    return m_buffer != nullptr ? m_buffer->Handle() : BufferObjectHandle::Invalid;
}

} // namespace gte::rg
