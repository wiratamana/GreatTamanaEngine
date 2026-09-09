#pragma once

#include "Entity.h"

#include <string>

namespace gte {

class Registry;

// First entity (in ComponentStorage<Name>'s current dense iteration order -
// NOT guaranteed stable across a Remove() on ANY entity's Name component,
// per ComponentStorage<T>'s own swap-with-last removal - see ComponentStorage.h)
// whose Name::value is EXACTLY equal to `name` (case-sensitive, byte-for-byte
// comparison - no normalization of any kind). Returns kInvalidEntity if no
// live entity currently has that exact name, or if `name` is empty (an empty
// name is never a valid lookup key - MakeUniqueEntityName() below never
// produces one, and this function treats an empty `name` as "not found" by
// definition rather than silently matching every entity with an empty Name,
// which would otherwise be a real footgun).
Entity FindEntityByName(Registry& registry, const std::string& name);

// True if ANY live entity currently has a Name component whose value equals
// `name` exactly - a thin wrapper over FindEntityByName() returning != kInvalidEntity,
// kept as its own named function purely for call-site readability
// (`if (IsEntityNameInUse(...))` reads better than a raw comparison at every
// call site - mirrors this codebase's general preference for named boolean
// helpers over inline comparisons, e.g. Entity::IsValid()).
bool IsEntityNameInUse(Registry& registry, const std::string& name);

// Unity's own "GameObject", "GameObject (1)", "GameObject (2)", ... auto-
// de-duplication convention, mirroring the already-established precedent in
// src/Editor/ProjectPanelData.h's MakeUniqueDestinationPath() (see that
// function's own implementation for the loop/probe shape this mirrors). If
// `baseName` is not currently in use (IsEntityNameInUse() == false), returns
// it verbatim, unmodified - the common case, and what keeps a single spawn
// with no naming collision looking exactly like the name the caller asked
// for. Otherwise probes "<baseName> (1)", "<baseName> (2)", "<baseName> (3)",
// ... in order, returning the first one NOT currently in use. `baseName` must
// be non-empty (callers - Phase 3's Game::InstantiatePrimitive() - are
// responsible for substituting a sensible default, e.g. the shape's own
// ToString(), before calling this when the caller supplied no name at all;
// this function itself does not know about PrimitiveType and must not gain a
// dependency on it).
//
// NOTE (deliberate, do not "fix" speculatively): unlike MakeUniqueDestinationPath()'s
// filesystem probing (bounded in practice by how many files a human could
// plausibly create by hand), this loop has no engine-enforced upper bound.
// This is accepted as-is, mirroring the filesystem precedent's own unbounded
// loop - do not add an artificial cap unless a future regression actually
// demonstrates a problem. Also note this function does NOT attempt to detect
// or increment an already-"(N)"-shaped base name smartly - a base name that
// is itself already in the "X (1)" shape and already in use simply probes
// onward from "X (1) (1)", exactly mirroring MakeUniqueDestinationPath()'s own
// simplicity.
std::string MakeUniqueEntityName(Registry& registry, const std::string& baseName);

} // namespace gte
