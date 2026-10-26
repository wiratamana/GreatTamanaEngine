#include "GpuReadback.h"

#include "Frame.h"
#include "FrameContext.h"
#include "PersistentTarget.h"
#include "../Logging.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/GpuBuffer.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <exception>
#include <string>

namespace gte::fx::detail {

namespace {

constexpr const char* kLogCategory = "FeatureKit";
constexpr std::size_t kCells = 3;
constexpr std::uint64_t kSampleLagFrames = 2;

} // namespace

struct ReadbackCore::Impl {
    struct Cell {
        rg::GpuBuffer device;
        rg::GpuBuffer host;
        std::uint64_t frame = kNoFrame; // Frame whose copy was declared into `host`.
    };

    // `names` come first: the buffers and the graph imports keep pointers into them.
    struct Slot {
        std::array<std::string, kCells * 2> names;
        std::array<Cell, kCells> cells;
        bool created = false;
        rg::BuildRetryGate gate;
        std::uint64_t importedSerial = kNoFrame;
        Binding binding;
    };

    Impl(std::string_view baseName, std::size_t byteSize)
        : base(baseName)
        , size(byteSize)
    {
    }

    // Names the slot's buffers for the frame's view on first use.
    void NameSlot(Slot& slot, const FrameContext& context) const
    {
        if (!slot.names[0].empty()) {
            return;
        }
        const std::string slotBase = PersistentName(base, context);
        for (std::size_t c = 0; c < kCells; ++c) {
            slot.names[c * 2] = slotBase + ".Device" + std::to_string(c);
            slot.names[c * 2 + 1] = slotBase + ".Host" + std::to_string(c);
        }
    }

    bool Create(Slot& slot, const Frame& frame, rg::RenderGraphBuilder& builder);

    std::string base;
    std::size_t size;
    std::array<Slot, rg::kViewScopeCount> slots;
};

bool ReadbackCore::Impl::Create(Slot& slot, const Frame& frame, rg::RenderGraphBuilder& builder)
{
    try {
        const rg::DeviceRef device = builder.OwningRenderer().Device();
        for (std::size_t c = 0; c < kCells; ++c) {
            Cell& cell = slot.cells[c];
            cell.device = rg::GpuBuffer(device, size, slot.names[c * 2].c_str(),
                rg::GpuBufferUse::Storage | rg::GpuBufferUse::TransferSrc | rg::GpuBufferUse::TransferDst);
            cell.host = rg::GpuBuffer(
                device, size, slot.names[c * 2 + 1].c_str(), rg::GpuBufferUse::HostRead | rg::GpuBufferUse::TransferDst);
            cell.frame = kNoFrame;
        }
    } catch (const std::exception& e) {
        for (Cell& cell : slot.cells) {
            cell = Cell{};
        }
        if (slot.gate.OnFailure(frame.FrameIndex()) == rg::BuildRetryGate::FailureKind::First) {
            frame.ReportOnce("GpuReadback '" + slot.names[0] + "' could not allocate its buffers: " + e.what());
        }
        return false;
    }
    if (slot.gate.OnSuccess()) {
        GTE_LOG_INFO(kLogCategory, "GpuReadback '" + slot.names[0] + "' recovered after an earlier failure.");
    }
    slot.created = true;
    return true;
}

ReadbackCore::ReadbackCore(std::string_view baseName, std::size_t byteSize)
    : m_impl(std::make_unique<Impl>(baseName, byteSize))
{
}

ReadbackCore::~ReadbackCore() = default;
ReadbackCore::ReadbackCore(ReadbackCore&&) noexcept = default;
ReadbackCore& ReadbackCore::operator=(ReadbackCore&&) noexcept = default;

std::optional<ReadbackCore::Binding> ReadbackCore::Acquire(const Frame& frame)
{
    FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr) {
        frame.ReportOnce("GpuReadback needs a frame that belongs to a render callback");
        return std::nullopt;
    }
    rg::RenderGraphBuilder& builder = *context->builder;
    if (builder.RendererOrNull() == nullptr) {
        frame.ReportOnce("GpuReadback needs a graph with a renderer");
        return std::nullopt;
    }

    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.importedSerial == frame.Serial()) {
        return slot.binding;
    }
    m_impl->NameSlot(slot, *context);
    if (!slot.created && (!slot.gate.ShouldAttempt(frame.FrameIndex()) || !m_impl->Create(slot, frame, builder))) {
        return std::nullopt;
    }

    const Impl::Cell& cell = slot.cells[frame.FrameIndex() % kCells];
    slot.binding = Binding{ builder.Import(cell.device), builder.Import(cell.host), m_impl->size };
    slot.importedSerial = frame.Serial();
    return slot.binding;
}

void ReadbackCore::NoteCopied(const Frame& frame)
{
    FrameContext* context = frame.Context();
    if (context == nullptr) {
        return;
    }
    Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(context->scope)];
    if (slot.created) {
        slot.cells[frame.FrameIndex() % kCells].frame = frame.FrameIndex();
    }
}

bool ReadbackCore::Latest(const Frame& frame, void* out, std::uint64_t& recordedFrame) const
{
    const std::uint64_t index = frame.FrameIndex();
    if (index < kSampleLagFrames) {
        return false;
    }
    const Impl::Slot& slot = m_impl->slots[rg::ViewScopeIndex(frame.Scope())];
    if (!slot.created) {
        return false;
    }
    // The cell after this frame's own is the one written two frames ago.
    const Impl::Cell& cell = slot.cells[(index + 1) % kCells];
    if (cell.frame != index - kSampleLagFrames || !cell.host.Download(out, m_impl->size)) {
        return false;
    }
    recordedFrame = cell.frame;
    return true;
}

} // namespace gte::fx::detail
