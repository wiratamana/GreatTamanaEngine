#pragma once

// Kit-private: a lazy build that can fail. A failure backs off and retries forever; it never throws out.

#include "../../Renderer/RenderGraph/BuildRetryGate.h"

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace gte::fx::detail {

class GatedBuild {
public:
    enum class Result : std::uint8_t {
        Ready,       // Built (earlier or just now).
        Recovered,   // Built just now, after at least one failure.
        Cooling,     // A failure is backing off.
        FailedFirst, // This attempt failed and it is the first failure since the last success.
        FailedAgain, // This attempt failed again.
    };

    // Runs `build` (it throws on failure) unless it already succeeded or a retry is cooling down.
    template <typename Fn>
    Result Ensure(std::uint64_t frame, Fn&& build)
    {
        if (m_ready) {
            return Result::Ready;
        }
        if (!m_gate.ShouldAttempt(frame)) {
            return Result::Cooling;
        }
        try {
            build();
        } catch (const std::exception& e) {
            m_lastError = e.what();
            return m_gate.OnFailure(frame) == rg::BuildRetryGate::FailureKind::First ? Result::FailedFirst
                                                                                    : Result::FailedAgain;
        }
        m_ready = true;
        return m_gate.OnSuccess() ? Result::Recovered : Result::Ready;
    }

    bool Ready() const noexcept { return m_ready; }
    std::string_view LastError() const noexcept { return m_lastError; }

    // Forgets a success so the next Ensure() builds again.
    void Reset() noexcept { m_ready = false; }

private:
    rg::BuildRetryGate m_gate;
    std::string m_lastError;
    bool m_ready = false;
};

} // namespace gte::fx::detail
