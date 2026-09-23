#include "RenderPassGroupRegistry.h"

#include "../../Editor/Logger.h" // PHASE2 - GTE_LOG_WARNING for the "tag re-registered under a different heading" soft diagnostic (see this file's own header comment). Safe to include unconditionally regardless of GTE_ENABLE_EDITOR - see RenderGraph.cpp's own identical precedent.

#include <cstring>
#include <vector>

namespace gte::rg {

namespace {

struct Entry {
    RenderPassTag tag;
    const char* uiHeading = nullptr;
};

// A single function-local static std::vector<Entry> - NOT #ifndef NDEBUG-gated (unlike
// RenderPipeline.cpp's own PassIdDebugNameRegistry()), since this facility drives real,
// always-visible Editor UI in both debug and release builds. Deliberately a flat, linearly-
// scanned vector rather than a hashed container - the realistic number of live registered
// tags is single digits to low tens (mirrors RenderPassBlackboard's own identical reasoning).
std::vector<Entry>& Registry()
{
    static std::vector<Entry> entries;
    return entries;
}

// Pointer-OR-content match, mirroring RenderPipeline::Unregister()'s own established
// "reuse by identical pointer AND by equal-content-different-pointer" convention.
bool HeadingsMatch(const char* a, const char* b)
{
    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    return std::strcmp(a, b) == 0;
}

} // namespace

void RegisterPassGroupLabel(RenderPassTag tag, const char* uiHeading) noexcept
{
    std::vector<Entry>& entries = Registry();

    for (Entry& entry : entries) {
        if (entry.tag.bit == tag.bit) {
            if (!HeadingsMatch(entry.uiHeading, uiHeading)) {
                GTE_LOG_WARNING("RenderPassGroupRegistry",
                    "A pass-group tag (bit 0x" + std::to_string(tag.bit)
                        + ") was re-registered under a different Frame Debugger heading - old: \""
                        + std::string(entry.uiHeading != nullptr ? entry.uiHeading : "<null>")
                        + "\", new: \"" + std::string(uiHeading != nullptr ? uiHeading : "<null>")
                        + "\". The new heading wins; this is a cosmetic mislabeling risk, not a "
                          "correctness hazard.");
                entry.uiHeading = uiHeading;
            }
            return;
        }
    }

    entries.push_back(Entry{ tag, uiHeading });
}

std::size_t PassGroupLabelCount() noexcept
{
    return Registry().size();
}

RenderPassTag PassGroupLabelTagAt(std::size_t index) noexcept
{
    return Registry()[index].tag;
}

const char* PassGroupLabelUiHeadingAt(std::size_t index) noexcept
{
    return Registry()[index].uiHeading;
}

std::optional<std::size_t> FindPassGroupIndexForTags(RenderPassTagMask tags) noexcept
{
    const std::vector<Entry>& entries = Registry();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if ((entries[i].tag.bit & tags) != 0) {
            return i;
        }
    }
    return std::nullopt;
}

void ResetPassGroupRegistryForTesting() noexcept
{
    Registry().clear();
}

} // namespace gte::rg
