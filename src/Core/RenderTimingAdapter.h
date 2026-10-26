#pragma once

// Turns renderer timing callbacks into profiler scopes. Only the profiler's main thread records.

#include "../Profiling/ScopeEvent.h"
#include "../Profiling/ScopeLog.h"
#include "../Renderer/RenderTimingObserver.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace gte {

// Writes "GpuWait(Site)" or "GpuWait(Site:detail)" into `buffer` (truncated to fit) and returns the text.
std::string_view FormatWaitName(std::span<char> buffer, BlockSite site, const char* detail) noexcept;

// Writes "<prefix>.<suffix>" into `buffer` (truncated to fit) and returns the text.
std::string_view FormatScopeName(std::span<char> buffer, const char* prefix, const char* suffix) noexcept;

class RenderTimingAdapter final : public IRenderTimingObserver {
public:
    // Names the graph being executed ("Offscreen", "Present"). A string literal. Main thread only.
    void SetGraphLabel(const char* label) noexcept { m_graphLabel = label != nullptr ? label : ""; }

    void OnBlockBegin(BlockSite site, const char* detail) noexcept override;
    void OnBlockEnd(BlockSite site, BlockOutcome outcome) noexcept override;
    void OnPhaseBegin(RenderPhase phase) noexcept override;
    void OnPhaseEnd(RenderPhase phase) noexcept override;
    void OnDeclareBegin(const char* provider) noexcept override;
    void OnDeclareEnd() noexcept override;
    void OnPassRecordBegin(const char* pass) noexcept override;
    void OnPassRecordEnd() noexcept override;
    void OnMarker(const char* name) noexcept override;

private:
    struct Open {
        Profiling::ScopeStart start;
        Profiling::NameId name = Profiling::kInvalidNameId;
        Profiling::ScopeKind kind = Profiling::ScopeKind::Work;
    };

    void OpenScope(std::string_view name, Profiling::ScopeKind kind) noexcept;
    void CloseScope() noexcept;

    static constexpr std::size_t kMaxOpen = 32;

    // Main thread only. Scopes past kMaxOpen are counted but not recorded.
    std::array<Open, kMaxOpen> m_open{};
    std::size_t m_depth = 0;
    const char* m_graphLabel = "";
};

} // namespace gte
