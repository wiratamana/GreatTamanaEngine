#pragma once

#include "Renderer/Vertex.h"

#include <string>
#include <vector>

namespace gte {

// Built-in primitive shape (like Unity's PrimitiveType). A runtime type, not editor-only: the Hierarchy
// menu is just one caller of Game::CreatePrimitiveEntity(). PrimitiveMeshGenerator interprets it.
enum class PrimitiveType {
    Cube,
    Sphere,
    Capsule,
    Cone,
    Plane,
};

// Human-readable shape name ("Cube", "Sphere", ...), also used as the mesh debug name. Pure.
const char* ToString(PrimitiveType type) noexcept;

// Inverse of ToString(): case-insensitive parse of a shape name. Returns false and leaves `outType`
// untouched for anything else, including an empty string.
bool TryParsePrimitiveTypeName(const std::string& name, PrimitiveType& outType) noexcept;

// CPU-side unit-size geometry centered on the origin: non-indexed triangles with a normal and a neutral color per vertex.
class PrimitiveMeshGenerator {
public:
    static std::vector<Vertex> Generate(PrimitiveType type);

private:
    static std::vector<Vertex> GenerateCube();
    static std::vector<Vertex> GenerateSphere();
    static std::vector<Vertex> GenerateCapsule();
    static std::vector<Vertex> GenerateCone();
    static std::vector<Vertex> GeneratePlane();
};

} // namespace gte
