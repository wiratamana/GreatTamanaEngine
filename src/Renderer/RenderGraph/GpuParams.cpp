#include "GpuParams.h"

#include "RenderGraphBuilder.h"
#include "../Buffer.h"
#include "../Renderer.h"
#include "../../Core/Logging.h"

#include <cassert>
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

GpuParamsBuffer::~GpuParamsBuffer()
{
    RetireCopies();
}

GpuParamsBuffer::GpuParamsBuffer(GpuParamsBuffer&& other) noexcept
    : m_buffers(std::move(other.m_buffers))
    , m_ledger(other.m_ledger)
    , m_size(std::exchange(other.m_size, 0))
    , m_refusalLogged(other.m_refusalLogged)
    , m_renderer(std::exchange(other.m_renderer, nullptr))
{
}

GpuParamsBuffer& GpuParamsBuffer::operator=(GpuParamsBuffer&& other) noexcept
{
    if (this != &other) {
        RetireCopies();
        m_buffers = std::move(other.m_buffers);
        m_ledger = other.m_ledger;
        m_size = std::exchange(other.m_size, 0);
        m_refusalLogged = other.m_refusalLogged;
        m_renderer = std::exchange(other.m_renderer, nullptr);
    }
    return *this;
}

// A frame in flight may still read a copy, so the renderer frees it later instead of the GPU being drained.
void GpuParamsBuffer::RetireCopies() noexcept
{
    if (m_renderer == nullptr) {
        return;
    }
    for (std::unique_ptr<GpuBuffer>& copy : m_buffers) {
        if (copy != nullptr) {
            m_renderer->Retire(std::move(copy));
        }
    }
}

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
    const OffscreenFrameTicket& ticket = builder.FrameTicket();
    const std::uint32_t copy = RingCopyOf(ticket, kGpuFramesInFlight);
    if (refusal == nullptr && decision == GpuParamsUploadDecision::Overwrite) {
        if (m_buffers[copy] == nullptr) {
            refusal = "the copy of this frame slot does not exist";
        } else if (!m_ledger.Writable(ticket)) {
            refusal = "the copy of this frame slot is still in use by the GPU"; // Never write it.
        }
    }
    if (refusal != nullptr) {
        LogRefusalOnce(std::string("Upload to \"") + (debugName != nullptr ? debugName : "") + "\" refused: " + refusal + ".");
        return false;
    }

    try {
        if (decision == GpuParamsUploadDecision::Create) {
            // Every slot gets its copy up front, so no frame allocates in the middle of recording.
            std::array<std::unique_ptr<GpuBuffer>, kGpuFramesInFlight> created;
            for (std::unique_ptr<GpuBuffer>& slotCopy : created) {
                slotCopy = std::make_unique<GpuBuffer>(builder.OwningRenderer().Device(), size, debugName,
                    GpuBufferUse::Storage | GpuBufferUse::HostWrite);
            }
            if (!created[copy]->Upload(data, size)) {
                return false;
            }
            m_buffers = std::move(created);
            m_renderer = &builder.OwningRenderer();
            m_size = size;
        } else if (!m_buffers[copy]->Upload(data, size)) {
            return false;
        }
        m_ledger.NoteUpload(ticket, builder.FrameIndex());
    } catch (const std::exception& e) {
        LogRefusalOnce(std::string("Upload to \"") + (debugName != nullptr ? debugName : "") + "\" failed: " + e.what());
        return false;
    }
    return true;
}

BufferObjectHandle GpuParamsBuffer::Handle(const RenderGraphBuilder& builder) const noexcept
{
    const OffscreenFrameTicket& ticket = builder.FrameTicket();
    const GpuBuffer* copy = m_buffers[RingCopyOf(ticket, kGpuFramesInFlight)].get();
    if (copy == nullptr) {
        return BufferObjectHandle::Invalid;
    }
    if (!m_ledger.UploadedThisFrame(ticket, builder.FrameIndex())) {
        assert(false && "GpuParamsBuffer::Handle() needs an Upload() in the same frame");
        return BufferObjectHandle::Invalid;
    }
    return copy->Handle();
}

} // namespace gte::rg
