#pragma once

// editor-core-separation-7 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md) - the real
// "Export DOT" implementation the original Render Graph campaign's own
// RenderGraphPanel.cpp button tooltip has promised since Phase 8
// ("Planned for Phase 9, once this panel's own ImGui-list data model has
// proven itself"). Pure function of an already-built rg::RenderGraphMetadata -
// no live RenderGraph/file I/O inside this function itself (see
// ExportRenderGraphDotToFile() below for the one thin, file-writing wrapper
// that DOES do I/O, kept deliberately separate so this function stays
// Tier-1-testable on its own text output).
//
// Lives under src/Editor/ (gte_editor), NOT src/Renderer/RenderGraph/
// (gte_core) - PHASE0_MASTER_STRATEGY.md's Locked Design Decision #14:
// this file has exactly ONE consumer, forever, RenderGraphPanel::Build()'s
// own "Export DOT" button, mirroring src/Editor/ComputeBlurValidation.h/.cpp,
// src/Editor/GBufferValidation.h/.cpp,
// src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp, and
// src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp's own
// identical "Editor-only debug/export tool consuming rg::/Renderer types"
// precedent - all four live here too, not under Renderer/RenderGraph/.

#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <string>

namespace gte {

// Produces a complete, valid Graphviz DOT document text (a "digraph"
// wrapping one visually-distinct subgraph cluster per regime -
// "cluster_offscreen"/"cluster_present" - each pass a node, each declared
// read/write an edge from/to a small resource node, a culled pass rendered
// with a dashed/grey node style so a human opening this in any Graphviz
// viewer can immediately see what ran vs. what got culled, mirroring this
// panel's own existing ImGui "TextDisabled for culled" convention). Pure -
// no file I/O, directly Tier-1-testable by asserting substring content
// (e.g. every pass name appears, a culled pass's node carries the expected
// style attribute).
std::string BuildRenderGraphDot(const rg::RenderGraphMetadata& metadata);

// The ONE thin, file-writing wrapper - PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #13: always writes to a fixed, working-directory-relative
// path, "render_graph_export.dot" (overwritten every call, never a save
// dialog/path picker - this repo has none). Returns the resolved ABSOLUTE
// path actually written (useful for the caller to GTE_LOG_INFO) on success;
// returns an empty string on a genuine write failure - never throws,
// mirroring Editor/SceneIO.h's own SaveScene()'s "return false/empty on I/O
// failure, never throw" convention.
std::string ExportRenderGraphDotToFile(const rg::RenderGraphMetadata& metadata);

} // namespace gte
