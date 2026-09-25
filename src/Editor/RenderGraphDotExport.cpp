#include "RenderGraphDotExport.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Graphviz double-quoted string literal escaping - a raw pass/resource name
// is not guaranteed to be free of '"'/'\\' (this engine already has names
// with spaces/'#'/etc, all fine unescaped inside a quoted label; only these
// two characters are actually unsafe to embed verbatim).
std::string EscapeForDot(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

// Appends one regime's worth of DOT text as its own subgraph cluster.
// `idPrefix` seeds every node ID this regime declares (e.g. "offscreen_pass_0",
// "offscreen_res_0") - kept SEPARATE from each node's own human-readable
// `label="..."` attribute, since a raw pass/resource name is not guaranteed
// to be a valid bare DOT identifier (spaces, '#', etc are common in real
// pass/resource names in this engine).
void AppendRegimeDot(std::string& out, const char* idPrefix, const std::string& regimeLabel,
    const rg::RenderGraphRegimeMetadata& regime)
{
    out += "  subgraph cluster_";
    out += idPrefix;
    out += " {\n";
    out += "    label=\"";
    out += EscapeForDot(regimeLabel);
    out += "\";\n";

    // Pass nodes - one per entry in regime.passes, in the SAME order
    // (surviving passes first, then culled - matches
    // RenderGraphSnapshot::passesInExecutionOrder's own order, see
    // RenderGraphMetadata.h).
    std::vector<std::string> passNodeIds;
    passNodeIds.reserve(regime.passes.size());
    for (std::size_t i = 0; i < regime.passes.size(); ++i) {
        const rg::RenderGraphPassMetadata& pass = regime.passes[i];
        std::string nodeId = std::string(idPrefix) + "_pass_" + std::to_string(i);

        out += "    " + nodeId + " [label=\"" + EscapeForDot(pass.name.empty() ? "(unnamed)" : pass.name) + "\"";
        if (pass.isCulled) {
            // Dashed/grey - mirrors this panel's own existing ImGui
            // "TextDisabled for a culled pass" convention.
            out += ", style=\"dashed\", color=\"grey60\", fontcolor=\"grey40\"";
        } else {
            out += ", style=\"filled\", fillcolor=\"lightblue\"";
        }
        out += "];\n";

        passNodeIds.push_back(std::move(nodeId));
    }

    // Resource nodes - one per distinct resource NAME actually referenced in
    // this regime (declared resources first, matching regime.resources'
    // own order; any name only ever seen as a pass read/write - should not
    // normally happen, but handled defensively rather than dropped - is
    // appended on first reference below).
    std::unordered_map<std::string, std::string> resourceNodeIdByName;
    std::size_t nextResourceIndex = 0;
    auto resourceNodeIdFor = [&](const std::string& name) -> const std::string& {
        const auto it = resourceNodeIdByName.find(name);
        if (it != resourceNodeIdByName.end()) {
            return it->second;
        }
        std::string nodeId = std::string(idPrefix) + "_res_" + std::to_string(nextResourceIndex++);
        out += "    " + nodeId + " [label=\"" + EscapeForDot(name.empty() ? "(unnamed)" : name)
            + "\", shape=box, style=\"filled\", fillcolor=\"khaki\"];\n";
        return resourceNodeIdByName.emplace(name, std::move(nodeId)).first->second;
    };

    for (const rg::RenderGraphResourceMetadata& resource : regime.resources) {
        resourceNodeIdFor(resource.name);
    }

    // Edges - resource -> pass for every read, pass -> resource for every
    // write, mirroring how a Render Graph's own dependency direction
    // actually flows.
    for (std::size_t i = 0; i < regime.passes.size(); ++i) {
        const rg::RenderGraphPassMetadata& pass = regime.passes[i];
        const std::string& passNodeId = passNodeIds[i];

        for (const rg::RenderGraphResourceRefMetadata& read : pass.reads) {
            out += "    " + resourceNodeIdFor(read.name) + " -> " + passNodeId + ";\n";
        }
        for (const rg::RenderGraphResourceRefMetadata& write : pass.writes) {
            out += "    " + passNodeId + " -> " + resourceNodeIdFor(write.name) + ";\n";
        }
    }

    out += "  }\n";
}

} // namespace

std::string BuildRenderGraphDot(const rg::RenderGraphMetadata& metadata)
{
    std::string out;
    out += "digraph RenderGraph {\n";
    out += "  rankdir=LR;\n";
    out += "  node [fontname=\"Helvetica\"];\n";

    AppendRegimeDot(out, "offscreen", "Offscreen Regime (" + metadata.offscreenRegime.regimeName + ")",
        metadata.offscreenRegime);
    AppendRegimeDot(
        out, "present", "Present Regime (" + metadata.presentRegime.regimeName + ")", metadata.presentRegime);

    out += "}\n";
    return out;
}

std::string ExportRenderGraphDotToFile(const rg::RenderGraphMetadata& metadata)
{
    const std::string dotText = BuildRenderGraphDot(metadata);

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #13 - a fixed,
    // working-directory-relative path, always overwritten, never a save
    // dialog (this repo has none).
    const std::filesystem::path outputPath = "render_graph_export.dot";

    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return {};
    }
    file.write(dotText.data(), static_cast<std::streamsize>(dotText.size()));
    if (!file.good()) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path resolved = std::filesystem::absolute(outputPath, ec);
    return ec ? outputPath.string() : resolved.string();
}

} // namespace gte
