#include "Frame.h"

#include "GpuReadback.h"
#include "VerbSupport.h"
#include "../../Renderer/RenderGraph/PassKindDefaults.h"
#include "../../Renderer/RenderGraph/RecordedPass.h"

#include <utility>

namespace gte::fx {

namespace {

bool DeclareTransferPass(const detail::FrameContext& context, rg::StaticName passName, const rg::TransferRecord& record)
{
    std::optional<rg::PassBody> body = rg::MakeTransferBody(*context.builder, passName.value, record);
    if (!body) {
        return false;
    }
    context.builder->AddRenderPass(passName.value, rg::DefaultPassKind(rg::DeclarativePassType::Transfer), context.scope,
        rg::RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
        rg::DefaultDrawKind(rg::DeclarativePassType::Transfer), context.event);
    return true;
}

} // namespace

bool Frame::ClearReadbackCore(rg::StaticName passName, detail::ReadbackCore& core, std::uint32_t fillValue)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("ClearReadback() needs a frame that belongs to a render callback");
        return false;
    }
    if (!detail::PassEnabled(*m_context, passName)) {
        return false;
    }
    const std::optional<detail::ReadbackCore::Binding> binding = core.Acquire(*this);
    return binding && DeclareTransferPass(*m_context, passName, rg::TransferRecord::Fill(binding->device, fillValue, 0, binding->size));
}

bool Frame::CopyReadbackCore(rg::StaticName passName, detail::ReadbackCore& core)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("CopyReadbackToHost() needs a frame that belongs to a render callback");
        return false;
    }
    if (!detail::PassEnabled(*m_context, passName)) {
        return false;
    }
    const std::optional<detail::ReadbackCore::Binding> binding = core.Acquire(*this);
    if (!binding) {
        return false;
    }
    m_context->builder->KeepBufferOutput(binding->host);
    if (!DeclareTransferPass(*m_context, passName, rg::TransferRecord::Readback(binding->device, binding->host, binding->size))) {
        return false;
    }
    core.NoteCopied(*this);
    return true;
}

bool Frame::FillBuffer(rg::StaticName passName, GpuBufferRef buffer, std::uint32_t value)
{
    if (m_context == nullptr || m_context->builder == nullptr) {
        ReportOnce("FillBuffer() needs a frame that belongs to a render callback");
        return false;
    }
    if (!detail::PassEnabled(*m_context, passName)) {
        return false;
    }
    if (!detail::CheckOwned(*this, buffer, passName, "the buffer")) {
        return false;
    }
    // Device-local buffers get TransferDst automatically; a host-visible buffer must ask for it.
    const bool hostVisible = rg::HasUse(buffer.use, BufferUse::HostWrite) || rg::HasUse(buffer.use, BufferUse::HostRead);
    if (hostVisible && !rg::HasUse(buffer.use, BufferUse::TransferDst)) {
        ReportOnce(std::string("Pass '") + passName.value + "': the buffer was not created with BufferUse::TransferDst");
        return false;
    }
    return DeclareTransferPass(*m_context, passName, rg::TransferRecord::Fill(buffer.handle, value, 0, FillByteCount(buffer.byteSize)));
}

} // namespace gte::fx
