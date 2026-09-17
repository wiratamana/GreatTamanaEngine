#pragma once

// GTE_REFLECT_FIELD / GTE_REFLECT_ENUM_FIELD - the two macros a component
// registration site (see a later phase's ComponentTypeRegistry registration
// of Transform/Camera/etc.) uses to build one FieldDescriptor per field, fed
// into RegisterComponentType<T>() (ComponentTypeRegistry.h).

#include "ComponentTypeDescriptor.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>

// Builds ONE FieldDescriptor for a plain (non-enum) field `member` of
// component type `ComponentType`, relying on nlohmann::json's own built-in
// to_json/from_json for the field's type (works out of the box for
// float/int/bool/std::string, and for Vec3/Quat ONCE
// ECS/Reflection/MathJsonAdapters.h has been #included ahead of this macro's
// use site - callers MUST include that header first). A missing JSON key is
// treated as "keep the current value" (see FieldDescriptor::readJson's own
// doc comment) - checked via `inFields.contains(#member)` BEFORE attempting
// `.get<...>()`, never relying on a caught exception for the ordinary
// "field omitted" case (exceptions are reserved for genuinely malformed
// input, not a routine, expected, forward-compatible omission).
#define GTE_REFLECT_FIELD(ComponentType, member) \
    gte::FieldDescriptor{ \
        #member, \
        [](const void* c, nlohmann::json& out) { \
            out[#member] = static_cast<const ComponentType*>(c)->member; \
        }, \
        [](void* c, const nlohmann::json& in, std::string& err) -> bool { \
            if (!in.contains(#member)) { return true; } \
            try { \
                static_cast<ComponentType*>(c)->member = in.at(#member).get<decltype(ComponentType::member)>(); \
            } catch (const std::exception& e) { \
                err = std::string("field '" #member "' on component '" #ComponentType "': ") + e.what(); \
                return false; \
            } \
            return true; \
        } \
    }

// Same contract as GTE_REFLECT_FIELD above, for an ENUM field that needs a
// human-readable string representation on disk rather than a raw integer
// (matches this engine's existing "ToString()/TryParseXxxName()" convention
// - e.g. Renderer/Primitives/PrimitiveMeshGenerator.h's
// ToString(PrimitiveType)/TryParsePrimitiveTypeName()). `ToStringFn` must be
// callable as `const char*(EnumType)`; `TryParseFn` must be callable as
// `bool(const std::string&, EnumType&)`.
#define GTE_REFLECT_ENUM_FIELD(ComponentType, member, ToStringFn, TryParseFn) \
    gte::FieldDescriptor{ \
        #member, \
        [](const void* c, nlohmann::json& out) { \
            out[#member] = ToStringFn(static_cast<const ComponentType*>(c)->member); \
        }, \
        [](void* c, const nlohmann::json& in, std::string& err) -> bool { \
            if (!in.contains(#member)) { return true; } \
            if (!in.at(#member).is_string()) { \
                err = "field '" #member "' on component '" #ComponentType "' must be a string"; \
                return false; \
            } \
            decltype(ComponentType::member) parsed{}; \
            if (!TryParseFn(in.at(#member).get<std::string>(), parsed)) { \
                err = "field '" #member "' on component '" #ComponentType "' has an unrecognized value"; \
                return false; \
            } \
            static_cast<ComponentType*>(c)->member = parsed; \
            return true; \
        } \
    }
