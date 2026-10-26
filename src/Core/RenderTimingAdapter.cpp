#include "RenderTimingAdapter.h"

#include "../Profiling/FrameProfiler.h"
#include "../Profiling/NameTable.h"

#include <cstring>

namespace gte {

namespace {

// Appends `text` to `buffer` at `used`, truncating at the end. Returns the new length.
std::size_t Append(std::span<char> buffer, std::size_t used, std::string_view text) noexcept
{
    const std::size_t room = used < buffer.size() ? buffer.size() - used : 0;
    const std::size_t count = text.size() < room ? text.size() : room;
    if (count > 0) {
        std::memcpy(buffer.data() + used, text.data(), count);
    }
    return used + count;
}

std::string_view View(const char* text) noexcept
{
    return text != nullptr ? std::string_view(text) : std::string_view();
}

} // namespace

std::string_view FormatWaitName(std::span<char> buffer, BlockSite site, const char* detail) noexcept
{
    std::size_t used = Append(buffer, 0, "GpuWait(");
    used = Append(buffer, used, ToString(site));
    const std::string_view extra = View(detail);
    if (!extra.empty()) {
        used = Append(buffer, used, ":");
        used = Append(buffer, used, extra);
    }
    used = Append(buffer, used, ")");
    return std::string_view(buffer.data(), used);
}

std::string_view FormatScopeName(std::span<char> buffer, const char* prefix, const char* suffix) noexcept
{
    std::size_t used = Append(buffer, 0, View(prefix));
    used = Append(buffer, used, ".");
    used = Append(buffer, used, View(suffix));
    return std::string_view(buffer.data(), used);
}

void RenderTimingAdapter::OpenScope(std::string_view name, Profiling::ScopeKind kind) noexcept
{
    if (!Profiling::FrameGate::IsMainThread()) {
        return;
    }
    if (m_depth < kMaxOpen) {
        Open& slot = m_open[m_depth];
        slot.start = Profiling::FrameProfiler::Instance().BeginScope();
        slot.kind = kind;
        slot.name = slot.start.token != 0 ? Profiling::GlobalNameTable().Intern(name) : Profiling::kInvalidNameId;
    }
    ++m_depth;
}

void RenderTimingAdapter::CloseScope() noexcept
{
    if (!Profiling::FrameGate::IsMainThread() || m_depth == 0) {
        return;
    }
    --m_depth;
    if (m_depth < kMaxOpen) {
        const Open& slot = m_open[m_depth];
        Profiling::FrameProfiler::Instance().EndScope(slot.start, slot.name, slot.kind, Profiling::ViewSlot::Shared);
    }
}

void RenderTimingAdapter::OnBlockBegin(BlockSite site, const char* detail) noexcept
{
    // Every thread publishes its wait; only the main thread also records a scope.
    Profiling::FrameProfiler::Instance().LiveBlocks().Begin(ToString(site), detail, Profiling::NowMicroseconds());
    std::array<char, 96> text{};
    OpenScope(FormatWaitName(text, site, detail), Profiling::ScopeKind::Blocked);
}

void RenderTimingAdapter::OnBlockEnd(BlockSite, BlockOutcome) noexcept
{
    Profiling::FrameProfiler::Instance().LiveBlocks().End();
    CloseScope();
}

void RenderTimingAdapter::OnPhaseBegin(RenderPhase phase) noexcept
{
    std::array<char, 64> text{};
    const char* label = m_graphLabel[0] != '\0' ? m_graphLabel : "Graph";
    OpenScope(FormatScopeName(text, label, ToString(phase)), Profiling::ScopeKind::Work);
}

void RenderTimingAdapter::OnPhaseEnd(RenderPhase) noexcept
{
    CloseScope();
}

void RenderTimingAdapter::OnDeclareBegin(const char* provider) noexcept
{
    std::array<char, 96> text{};
    OpenScope(FormatScopeName(text, "Declare", provider), Profiling::ScopeKind::Work);
}

void RenderTimingAdapter::OnDeclareEnd() noexcept
{
    CloseScope();
}

void RenderTimingAdapter::OnPassRecordBegin(const char* pass) noexcept
{
    std::array<char, 96> text{};
    OpenScope(FormatScopeName(text, "Pass", pass), Profiling::ScopeKind::Work);
}

void RenderTimingAdapter::OnPassRecordEnd() noexcept
{
    CloseScope();
}

} // namespace gte
