#pragma once

#include <cstdint>
#include <vector>

namespace gte {

// Builds the inclusive, ascending-sorted index range spanning `anchorIndex`
// and `clickedIndex` - the pure logic behind a genuine Windows Explorer/
// Unity-style Shift-click "range select" (as opposed to Ctrl-click's own
// single-item TOGGLE, see Selection.h's ToggleModelPartInSelection()): every
// index from whichever of the two is smaller up to whichever is larger,
// inclusive of both endpoints. Order-independent -
// BuildInclusiveIndexRange(2, 5) and BuildInclusiveIndexRange(5, 2) both
// return { 2, 3, 4, 5 }. Deliberately decoupled from BoneViewerWindow/
// RigidBodyEntry/JointEntry (plain int32_t in, plain int32_t vector out) -
// this is pure index arithmetic with no notion of "rigid body" or "joint"
// at all, exactly like RigidBodyGroupSelection.h's own functions are
// deliberately decoupled from RigidBody/Joint (see that header's own "What
// We Will NOT Do" reasoning) - reusable by ANY future flat, linearly-ordered
// list this Editor ever grows (not just Rigid Body/Joint rows).
//
// If `anchorIndex` is negative (no anchor recorded yet - see
// BoneViewerWindow.h's own m_flatSelectionAnchorIndex doc comment for every
// point it gets reset to -1: a fresh window, a genuine data reload, or a
// view-mode switch), there is no meaningful "distance" to span at all, so
// the result is just `{ clickedIndex }` alone - a Shift-click with no prior
// plain/Ctrl-click anchor degrades to selecting just the row that was
// actually clicked, never a crash or an empty result. `clickedIndex` itself
// is assumed non-negative by every real caller (BoneViewerWindow.cpp never
// calls this with a negative clickedIndex - see RenderFlatPartRow()/Build()'s
// own bounds-checked callers) but is not itself defensively re-validated
// here, since this function has no array to index out of bounds against -
// it only ever produces a list of plain integers for the caller to hand to
// Selection::SelectModelParts(), which is itself already defensive.
std::vector<std::int32_t> BuildInclusiveIndexRange(std::int32_t anchorIndex, std::int32_t clickedIndex);

} // namespace gte
