#include "RecordedPass.h"

#include "RenderGraph.h"

#include <string>
#include <utility>

namespace gte::rg {

std::optional<PassBody> MakeTransferBody(RenderGraphBuilder& builder, const char* passName, const TransferRecord& record)
{
    if (const auto error = ValidateTransferRecord(record)) {
        builder.ReportOnce("TransferPass", std::string("Pass '") + passName + "': " + *error);
        return std::nullopt;
    }
    return PassBody{
        [record](RenderGraphBuilder::PassBuilder& pass) { DeclareTransfer(pass, record); },
        [record](PassContext& ctx) { ExecuteTransfer(ctx, record); },
    };
}

std::optional<PassBody> MakeIndirectDrawBody(RenderGraphBuilder& builder, const char* passName,
    const IndirectDrawRecord& record, std::function<IndirectDrawInputs()> resolveInputs)
{
    if (const auto error = ValidateIndirectDrawRecord(record)) {
        builder.ReportOnce("IndirectDrawPass", std::string("Pass '") + passName + "': " + *error);
        return std::nullopt;
    }
    if (!resolveInputs) {
        builder.ReportOnce("IndirectDrawPass", std::string("Pass '") + passName + "': no input resolver was given");
        return std::nullopt;
    }
    return PassBody{
        [record](RenderGraphBuilder::PassBuilder& pass) { DeclareIndirectDraw(pass, record); },
        [record, resolveInputs = std::move(resolveInputs)](PassContext& ctx) {
            ExecuteIndirectDraw(ctx, record, resolveInputs());
        },
    };
}

} // namespace gte::rg
