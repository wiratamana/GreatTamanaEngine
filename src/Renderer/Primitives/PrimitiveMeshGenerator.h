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

// Pure CPU-side geometry for the built-in primitives (no GPU, ECS or Vulkan). Every shape is centered on
// the origin at Unity's unit size; Transform::scale resizes it. Meshes are plain non-indexed triangle
// lists with a real normal per vertex (flat per face for Cube/Cone/Plane, smooth for Sphere/Capsule)
// plus a flat neutral color; Triangle.frag does the lighting.
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
