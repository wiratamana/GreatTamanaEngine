#pragma once

#include "IndirectDrawPass.h"
#include "RenderGraphBuilder.h"
#include "TransferPass.h"

#include <functional>
#include <optional>

namespace gte::rg {

// The setup and execute functions of one declarative pass, ready for RenderPassDesc or AddRenderPass.
struct PassBody {
    std::function<void(RenderGraphBuilder::PassBuilder&)> setup;
    std::function<void(PassContext&)> execute;
};

// Validates `record` and returns its body. An invalid record logs one error (per graph) and returns nothing.
std::optional<PassBody> MakeTransferBody(RenderGraphBuilder& builder, const char* passName, const TransferRecord& record);

// `resolveInputs` runs every frame at execute time (pipeline and mesh can change between frames).
std::optional<PassBody> MakeIndirectDrawBody(RenderGraphBuilder& builder, const char* passName,
    const IndirectDrawRecord& record, std::function<IndirectDrawInputs()> resolveInputs);

} // namespace gte::rg
