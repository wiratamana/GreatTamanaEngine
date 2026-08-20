#pragma once

#include "Renderer/Vertex.h"

#include <vector>

namespace gte {

// Which built-in primitive shape to generate - this engine's equivalent of
// Unity's UnityEngine.PrimitiveType / GameObject.CreatePrimitive(). Plain
// enum, no behavior of its own (same "plain data" spirit as every ECS
// component - see AGENTS.md, "Entity-Component-System"); PrimitiveMeshGenerator
// below is the only thing that interprets it. Deliberately a RUNTIME type
// (not under src/Editor/) - spawning a primitive is a Game/gameplay-level
// operation (see Game::CreatePrimitiveEntity(), src/Game/Game.h), exactly
// like Unity's own GameObject.CreatePrimitive() is a runtime API, not an
// editor-only one. The Editor's "Hierarchy" right-click menu
// (src/Editor/Panels/HierarchyPanel.cpp) is just ONE caller of it.
enum class PrimitiveType {
    Cube,
    Sphere,
    Capsule,
    Cone,
    Plane,
};

// Human-readable name for a PrimitiveType ("Cube", "Sphere", ...) - used by
// the Editor's "Create 3D Object" menu and as a debug name handed to
// Renderer::CreateMesh(). Pure, dependency-free, safe to call from anywhere
// including Tier 1 tests - see tests/Renderer/PrimitiveMeshGeneratorTests.cpp.
const char* ToString(PrimitiveType type) noexcept;

// Pure CPU-side geometry generation for the engine's built-in primitive
// shapes - genuinely Tier-1-testable (see AGENTS.md, "Testability &
// Regression Safety"): no GPU device, Renderer, ECS, or Vulkan handle of any
// kind is touched here, only plain Vertex/Vec3 math. Whoever actually wants
// a drawable entity from this (see Game::CreatePrimitiveEntity(), src/Game/
// Game.h/.cpp) is responsible for handing the returned vertices to
// Renderer::CreateMesh() and wiring up a MeshHandle/PipelineHandle - this
// class never touches either.
//
// Every generated mesh is a plain, NON-INDEXED triangle list - this engine's
// Mesh (see Mesh.h) has no index buffer support yet (see TODO.md), so
// vertices are duplicated across triangles/faces as needed rather than
// shared. Each vertex's color is not a placeholder flat gray: it bakes a
// simple, fixed-direction "faux-lit" shade (a constant light direction
// dotted against either the FACE normal - Cube/Cone/Plane, i.e. hard-edged
// flat shading - or the true per-vertex normal - Sphere/Capsule, i.e. smooth
// shading) into the vertex color at generation time, entirely on the CPU.
// This is a deliberately-scoped stand-in for a real lighting pass/pipeline
// (which does not exist yet - the engine's one shader pair,
// Shaders/Triangle.vert/.frag, is a flat unlit vertex-color pass-through) -
// it makes a freshly-created primitive actually readable as a 3D shape in
// the Scene/Game view instead of a flat silhouette, at zero cost to the
// existing renderer/pipeline beyond Vertex::position growing from vec2 to
// vec3 (see Vertex.h).
//
// Every shape is centered on the origin, at the same "unit" size Unity
// itself uses for each built-in primitive (a 1x1x1 Cube, a Sphere/Capsule of
// radius 0.5, a 1x1 Plane, ...) - Transform::scale is how a caller resizes
// the result, exactly like Unity's own primitives.
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
