#pragma once

#include "RenderTimingObserver.h"

#include <stdexcept>
#include <string>

namespace gte {

// Thrown when a bounded GPU wait did not complete. Carries the outcome so callers can tell busy from lost.
class GpuWaitFailure : public std::runtime_error {
public:
    GpuWaitFailure(const std::string& what, BlockOutcome outcome) : std::runtime_error(what), m_outcome(outcome) {}

    BlockOutcome Outcome() const noexcept { return m_outcome; }

private:
    BlockOutcome m_outcome;
};

} // namespace gte
