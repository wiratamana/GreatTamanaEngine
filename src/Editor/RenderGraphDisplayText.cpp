#include "RenderGraphDisplayText.h"

#include "../Application/GpuTimingText.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <algorithm>
#include <map>
#include <utility>

namespace gte {

namespace {

constexpr const char* kSharedLabel = "Shared";
constexpr const char* kPrimaryLabel = "Game";
constexpr const char* kSecondaryLabel = "Scene";

// Fixed order when combining view labels, so the output is deterministic.
int LabelPriority(const std::string& label)
{
    if (label == kPrimaryLabel) {
        return 0;
    }
    if (label == kSecondaryLabel) {
        return 1;
    }
    if (label == kSharedLabel) {
        return 2;
    }
    return 3;
}

// View badge from the contributing instances: distinct labels by priority joined with "+"; a lone Game or Scene gets " only".
std::string BuildViewLabel(const std::vector<const rg::RenderGraphPassMetadata*>& contributors)
{
    std::vector<std::string> distinct;
    for (const rg::RenderGraphPassMetadata* instance : contributors) {
        const std::string label = ViewScopeLabel(instance->viewScope);
        if (std::find(distinct.begin(), distinct.end(), label) == distinct.end()) {
            distinct.push_back(label);
        }
    }
    std::sort(distinct.begin(), distinct.end(),
        [](const std::string& a, const std::string& b) { return LabelPriority(a) < LabelPriority(b); });

    if (distinct.empty()) {
        return "(no instance)";
    }
    if (distinct.size() == 1) {
        return (distinct[0] == kPrimaryLabel || distinct[0] == kSecondaryLabel) ? (distinct[0] + " only") : distinct[0];
    }
    std::string joined;
    for (const std::string& label : distinct) {
        if (!joined.empty()) {
            joined += "+";
        }
        joined += label;
    }
    return joined;
}

// Per-view timing text, not summed. A single instance shows its own text; equal views keep their order.
std::string BuildGroupedGpuTimingText(const std::vector<rg::RenderGraphPassMetadata>& instances)
{
    if (instances.size() == 1) {
        return FormatGpuTiming(instances.front());
    }
    std::vector<const rg::RenderGraphPassMetadata*> sorted;
    sorted.reserve(instances.size());
    for (const rg::RenderGraphPassMetadata& instance : instances) {
        sorted.push_back(&instance);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const rg::RenderGraphPassMetadata* a, const rg::RenderGraphPassMetadata* b) {
        return LabelPriority(ViewScopeLabel(a->viewScope)) < LabelPriority(ViewScopeLabel(b->viewScope));
    });
    std::string joined;
    for (const rg::RenderGraphPassMetadata* instance : sorted) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += std::string(ViewScopeLabel(instance->viewScope)) + ": " + FormatGpuTiming(*instance);
    }
    return joined;
}

// Names of every instance's reads (or writes when !reads), first-seen order, no duplicates.
std::vector<std::string> CombineResourceRefNames(const std::vector<rg::RenderGraphPassMetadata>& instances, bool reads)
{
    std::vector<std::string> combined;
    for (const rg::RenderGraphPassMetadata& instance : instances) {
        const std::vector<rg::RenderGraphResourceRefMetadata>& refs = reads ? instance.reads : instance.writes;
        for (const rg::RenderGraphResourceRefMetadata& ref : refs) {
            if (std::find(combined.begin(), combined.end(), ref.name) == combined.end()) {
                combined.push_back(ref.name);
            }
        }
    }
    return combined;
}

} // namespace

const char* ViewScopeLabel(const std::string& viewScope)
{
    if (viewScope == rg::ToString(rg::ViewScope::Primary)) {
        return kPrimaryLabel;
    }
    if (viewScope == rg::ToString(rg::ViewScope::Secondary)) {
        return kSecondaryLabel;
    }
    return kSharedLabel;
}

std::string FormatGpuTiming(const GpuPassRecord& record)
{
    const double milliseconds = static_cast<double>(record.barrierMs) + static_cast<double>(record.workMs);
    return FormatGpuTimingText(rg::ToString(record.state), milliseconds);
}

std::string FormatGpuTiming(const rg::RenderGraphPassMetadata& pass)
{
    return FormatGpuTimingText(pass.gpuTimingStatus, pass.gpuRecordedMilliseconds);
}

// std::map iteration is sorted by name, so row order does not depend on input order.
std::vector<RenderGraphGroupedPass> GroupPassMetadataByName(const std::vector<rg::RenderGraphPassMetadata>& ungrouped)
{
    std::map<std::string, std::vector<rg::RenderGraphPassMetadata>> instancesByName;
    for (const rg::RenderGraphPassMetadata& pass : ungrouped) {
        instancesByName[pass.name].push_back(pass);
    }

    std::vector<RenderGraphGroupedPass> grouped;
    grouped.reserve(instancesByName.size());
    for (auto& [name, instances] : instancesByName) {
        RenderGraphGroupedPass group;
        group.name = name;

        bool allCulled = true;
        std::vector<const rg::RenderGraphPassMetadata*> nonCulled;
        std::vector<const rg::RenderGraphPassMetadata*> allInstances;
        for (const rg::RenderGraphPassMetadata& instance : instances) {
            group.drawCallCount += instance.drawCallCount;
            group.triangleCount += instance.triangleCount;
            if (!instance.isCulled) {
                allCulled = false;
                nonCulled.push_back(&instance);
            }
            allInstances.push_back(&instance);
        }
        group.isCulled = allCulled;
        // The badge reflects only views that contributed a non-culled instance.
        group.viewLabel = BuildViewLabel(nonCulled.empty() ? allInstances : nonCulled);

        group.gpuTimingText = BuildGroupedGpuTimingText(instances);
        group.reads = CombineResourceRefNames(instances, /*reads=*/true);
        group.writes = CombineResourceRefNames(instances, /*reads=*/false);
        group.instances = std::move(instances);

        grouped.push_back(std::move(group));
    }
    return grouped;
}

} // namespace gte
