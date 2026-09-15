#include "FrameDebuggerData.h"

namespace gte {

FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot()
{
    // Deliberately empty - see FrameDebuggerData.h's own top-of-file
    // comment and PHASE0's Locked Design Decision #2. Returning a
    // default-constructed FrameDebuggerSnapshot{} is intentional, not a
    // stub left unfinished - this IS the finished behavior for this
    // campaign.
    return FrameDebuggerSnapshot{};
}

std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return "0 of 0";
    }
    // 1-based display, matching the reference screenshot's own
    // "2117 of 2117" convention - currentEventIndex is a 0-based index
    // internally (see ClampSelectedEventIndex()'s own contract), so +1
    // here, once, is the single place that conversion happens.
    const int displayIndex = currentEventIndex < 0 ? 0 : (currentEventIndex + 1);
    return std::to_string(displayIndex) + " of " + std::to_string(totalEventCount);
}

int ClampSelectedEventIndex(int requested, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return -1;
    }
    if (requested < 0) {
        return 0;
    }
    if (requested >= totalEventCount) {
        return totalEventCount - 1;
    }
    return requested;
}

namespace {

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndexRecursive(
    const std::vector<FrameDebuggerEventNode>& nodes, int eventIndex)
{
    for (const FrameDebuggerEventNode& node : nodes) {
        if (node.isDrawCall && node.eventIndex == eventIndex) {
            return node.details;
        }
        std::optional<FrameDebuggerEventDetails> found
            = FindEventDetailsByIndexRecursive(node.children, eventIndex);
        if (found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex)
{
    if (eventIndex < 0) {
        return std::nullopt;
    }
    return FindEventDetailsByIndexRecursive(snapshot.rootNodes, eventIndex);
}

} // namespace gte
