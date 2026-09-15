#include "Time.h"

namespace gte {

void Time::Advance(double realDeltaSeconds, bool isPaused, bool isSteppedThisFrame, double fixedStepSeconds) noexcept
{
    m_unscaledDeltaSeconds = realDeltaSeconds;
    m_timeSinceStartupSeconds += realDeltaSeconds;
    ++m_frameCount;

    m_isPaused = isPaused;
    // A Step request is only ever meaningful while paused - see the
    // header's own doc comment on this parameter.
    m_isSteppedThisFrame = isPaused && isSteppedThisFrame;

    if (m_isSteppedThisFrame) {
        m_deltaSeconds = fixedStepSeconds;
    } else if (m_isPaused) {
        m_deltaSeconds = 0.0;
    } else if (m_wasPausedLastCall) {
        // Locked Design Decision #11 - just resumed from a pause of
        // arbitrary real-world duration. Nothing was simulated while
        // frozen, so don't replay that entire elapsed wall-clock gap as
        // one giant catch-up step (which could tunnel a fast-moving
        // physics object clean through a thin collider, or make an
        // animation jump forward by however long the user stepped away
        // for) - resume with a single ordinary-sized step instead, exactly
        // as if merely one normal frame had elapsed.
        m_deltaSeconds = fixedStepSeconds;
    } else {
        m_deltaSeconds = realDeltaSeconds;
    }

    m_simulatedTimeSeconds += m_deltaSeconds;
    m_wasPausedLastCall = m_isPaused;
}

} // namespace gte
