#pragma once

// Free to_json/from_json ADL adapters for this engine's own math types
// (gte::Vec3/gte::Quat), so nlohmann::json can convert them automatically
// anywhere a FieldDescriptor (see ComponentTypeDescriptor.h)/GTE_REFLECT_FIELD
// (see ReflectFieldMacros.h) touches a Vec3/Quat field. These functions MUST
// live inside `namespace gte` (the same namespace as Vec3/Quat themselves) -
// nlohmann::json finds them via argument-dependent lookup (ADL), not via any
// registration call.
//
// Represented as a plain 3- or 4-element JSON ARRAY (`[x,y,z]`/`[x,y,z,w]`),
// not a JSON object with named keys - shorter on disk, and matches this
// engine's own prior CSV-positional convention (scene-serialization-1's
// SceneTextFormat.cpp `position=%.6f,%.6f,%.6f`).
//
// `from_json` uses `.at(i)` (throws nlohmann::json::out_of_range if the array
// is too short) and `.get<float>()` (throws nlohmann::json::type_error if an
// element isn't numeric) - both exceptions are expected to propagate up to
// whatever calls from_json indirectly (GTE_REFLECT_FIELD's generated
// readJson lambda, see ReflectFieldMacros.h), which MUST catch
// `std::exception` at that boundary and turn it into a non-throwing `bool`
// failure - a malformed scene file must never crash the engine.

#include "../../Math/Quat.h"
#include "../../Math/Vec3.h"

#include <nlohmann/json.hpp>

namespace gte {

inline void to_json(nlohmann::json& j, const Vec3& v)
{
    j = { v.x, v.y, v.z };
}

inline void from_json(const nlohmann::json& j, Vec3& v)
{
    v.x = j.at(0).get<float>();
    v.y = j.at(1).get<float>();
    v.z = j.at(2).get<float>();
}

inline void to_json(nlohmann::json& j, const Quat& q)
{
    j = { q.x, q.y, q.z, q.w };
}

inline void from_json(const nlohmann::json& j, Quat& q)
{
    q.x = j.at(0).get<float>();
    q.y = j.at(1).get<float>();
    q.z = j.at(2).get<float>();
    q.w = j.at(3).get<float>();
}

} // namespace gte
