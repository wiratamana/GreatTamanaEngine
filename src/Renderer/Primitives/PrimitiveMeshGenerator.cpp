#include "Renderer/Primitives/PrimitiveMeshGenerator.h"

#include "Math/MathTypes.h"
#include "Math/Vec3.h"

#include <cmath>
#include <cstddef>

namespace gte {

namespace {

// A fixed, arbitrary "key light" direction plus an ambient floor - baked
// into each generated vertex's color at generation time instead of a real
// lighting pass. See the class comment in PrimitiveMeshGenerator.h for the
// full rationale. The ambient floor keeps a face pointed away from the
// light a dim, visible gray instead of pure black.
const Vec3 kLightDir = Normalize(Vec3{ 0.4f, 0.8f, -0.4f });
constexpr float kAmbient = 0.25f;
constexpr Vec3 kBaseColor{ 0.70f, 0.70f, 0.75f };

Vec3 Shade(const Vec3& normal) noexcept
{
    const float ndotl = Dot(normal, kLightDir);
    const float intensity = kAmbient + (1.0f - kAmbient) * (ndotl > 0.0f ? ndotl : 0.0f);
    return kBaseColor * intensity;
}

Vertex MakeVertex(const Vec3& position, const Vec3& color) noexcept
{
    return Vertex{ { position.x, position.y, position.z }, { color.x, color.y, color.z } };
}

// Appends one flat-shaded (hard-edged) triangle: all three vertices get the
// SAME color, baked from the triangle's own face normal - used for Cube/
// Cone/Plane, where an abrupt shade change from one face to the next is
// exactly what a real edge should look like.
void AddFlatTriangle(std::vector<Vertex>& vertices, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 normal = Normalize(Cross(b - a, c - a));
    const Vec3 color = Shade(normal);
    vertices.push_back(MakeVertex(a, color));
    vertices.push_back(MakeVertex(b, color));
    vertices.push_back(MakeVertex(c, color));
}

// Appends one quad (a,b,c,d, in order around its perimeter) as two flat-
// shaded triangles sharing that same face normal/color - the face normal is
// computed from (a,b,c) alone, so callers must list corners so THAT triangle
// alone already has the correct outward winding (see GenerateCube()'s own
// comment for the worked-out per-face winding).
void AddFlatQuad(std::vector<Vertex>& vertices, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d)
{
    AddFlatTriangle(vertices, a, b, c);
    AddFlatTriangle(vertices, a, c, d);
}

// Appends one smooth-shaded triangle: each vertex gets its OWN color, baked
// from its OWN (already-computed) normal - used for Sphere/Capsule, where
// the surface should look continuously curved rather than faceted.
void AddSmoothTriangle(std::vector<Vertex>& vertices, const Vec3& posA, const Vec3& normalA, const Vec3& posB,
    const Vec3& normalB, const Vec3& posC, const Vec3& normalC)
{
    vertices.push_back(MakeVertex(posA, Shade(normalA)));
    vertices.push_back(MakeVertex(posB, Shade(normalB)));
    vertices.push_back(MakeVertex(posC, Shade(normalC)));
}

// Shared by GenerateSphere() and GenerateCapsule()'s two end caps: a UV
// hemisphere of `radius` centered at `center`. Its pole sits at
// center + (0, apexSign * radius, 0) and its equator (a full circle of the
// given radius) at center's own height - `apexSign` is +1 for a cap whose
// pole points up (a capsule's top cap, or a full sphere's top half) or -1
// for one whose pole points down (a capsule's bottom cap, or a full
// sphere's bottom half). Every vertex's normal is simply
// Normalize(position - center) - always geometrically correct for a
// sphere/hemisphere regardless of triangle winding, which is why this
// (unlike GenerateCube()/GenerateCone()) never has to reason about winding
// order at all.
void AddHemisphere(std::vector<Vertex>& vertices, const Vec3& center, float radius, float apexSign,
    int latitudeSegments, int longitudeSegments)
{
    auto ringPoint = [&](int lat, int lon) -> Vec3 {
        const float theta = kHalfPi * (static_cast<float>(lat) / static_cast<float>(latitudeSegments)); // 0..pi/2
        const float phi = kTwoPi * (static_cast<float>(lon) / static_cast<float>(longitudeSegments));
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        const Vec3 offset{ radius * sinTheta * std::cos(phi), apexSign * radius * cosTheta,
            radius * sinTheta * std::sin(phi) };
        return center + offset;
    };

    for (int lat = 0; lat < latitudeSegments; ++lat) {
        for (int lon = 0; lon < longitudeSegments; ++lon) {
            const Vec3 p00 = ringPoint(lat, lon);
            const Vec3 p01 = ringPoint(lat, lon + 1);
            const Vec3 p10 = ringPoint(lat + 1, lon);
            const Vec3 p11 = ringPoint(lat + 1, lon + 1);

            const Vec3 n00 = Normalize(p00 - center);
            const Vec3 n01 = Normalize(p01 - center);
            const Vec3 n10 = Normalize(p10 - center);
            const Vec3 n11 = Normalize(p11 - center);

            if (lat == 0) {
                // Degenerate top ring - it IS the pole, so p00 == p01
                // regardless of `lon` - just one triangle per longitude
                // slice here, not a quad.
                AddSmoothTriangle(vertices, p00, n00, p10, n10, p11, n11);
            } else {
                AddSmoothTriangle(vertices, p00, n00, p10, n10, p11, n11);
                AddSmoothTriangle(vertices, p00, n00, p11, n11, p01, n01);
            }
        }
    }
}

} // namespace

const char* ToString(PrimitiveType type) noexcept
{
    switch (type) {
        case PrimitiveType::Cube: return "Cube";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Capsule: return "Capsule";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::Plane: return "Plane";
    }
    return "Unknown";
}

std::vector<Vertex> PrimitiveMeshGenerator::Generate(PrimitiveType type)
{
    switch (type) {
        case PrimitiveType::Cube: return GenerateCube();
        case PrimitiveType::Sphere: return GenerateSphere();
        case PrimitiveType::Capsule: return GenerateCapsule();
        case PrimitiveType::Cone: return GenerateCone();
        case PrimitiveType::Plane: return GeneratePlane();
    }
    return {};
}

std::vector<Vertex> PrimitiveMeshGenerator::GenerateCube()
{
    constexpr float h = 0.5f;
    const Vec3 p000{ -h, -h, -h };
    const Vec3 p001{ -h, -h, h };
    const Vec3 p010{ -h, h, -h };
    const Vec3 p011{ -h, h, h };
    const Vec3 p100{ h, -h, -h };
    const Vec3 p101{ h, -h, h };
    const Vec3 p110{ h, h, -h };
    const Vec3 p111{ h, h, h };

    std::vector<Vertex> vertices;
    vertices.reserve(36);

    // Each quad's 4 corners are listed so Cross(b-a, c-a) of its first
    // triangle already points straight out of that face - hand-verified for
    // all six faces (see tests/Renderer/PrimitiveMeshGeneratorTests.cpp).
    AddFlatQuad(vertices, p100, p110, p111, p101); // +X
    AddFlatQuad(vertices, p001, p011, p010, p000); // -X
    AddFlatQuad(vertices, p010, p011, p111, p110); // +Y
    AddFlatQuad(vertices, p000, p100, p101, p001); // -Y
    AddFlatQuad(vertices, p101, p111, p011, p001); // +Z
    AddFlatQuad(vertices, p100, p000, p010, p110); // -Z

    return vertices;
}

std::vector<Vertex> PrimitiveMeshGenerator::GenerateSphere()
{
    constexpr float radius = 0.5f;
    constexpr int latitudeSegments = 16;  // top-to-bottom rings, split evenly across both hemispheres
    constexpr int longitudeSegments = 16; // around the equator

    std::vector<Vertex> vertices;
    // A full sphere is just two hemispheres glued at the equator - reuse
    // AddHemisphere() for both halves, with matching radius/longitude so the
    // seam at y=0 lines up exactly.
    AddHemisphere(vertices, Vec3::Zero(), radius, 1.0f, latitudeSegments / 2, longitudeSegments);
    AddHemisphere(vertices, Vec3::Zero(), radius, -1.0f, latitudeSegments / 2, longitudeSegments);
    return vertices;
}

std::vector<Vertex> PrimitiveMeshGenerator::GenerateCapsule()
{
    constexpr float radius = 0.5f;
    constexpr float totalHeight = 2.0f; // Unity's own default capsule proportions (radius 0.5, height 2).
    constexpr float cylinderHalfHeight = totalHeight * 0.5f - radius; // 0.5 with the constants above.
    constexpr int radialSegments = 16;
    constexpr int capLatitudeSegments = 8;

    std::vector<Vertex> vertices;

    // Cylindrical body: one band from y=-cylinderHalfHeight to
    // y=+cylinderHalfHeight. Every vertex's normal is the pure radial
    // direction (cos(phi), 0, sin(phi)) - smoothly shaded around the
    // circumference, and exactly matching the hemisphere caps' own equator
    // normals at y = +-cylinderHalfHeight, so there's no visible seam.
    for (int i = 0; i < radialSegments; ++i) {
        const float phi0 = kTwoPi * (static_cast<float>(i) / static_cast<float>(radialSegments));
        const float phi1 = kTwoPi * (static_cast<float>(i + 1) / static_cast<float>(radialSegments));

        const Vec3 n0{ std::cos(phi0), 0.0f, std::sin(phi0) };
        const Vec3 n1{ std::cos(phi1), 0.0f, std::sin(phi1) };

        const Vec3 bottom0{ radius * n0.x, -cylinderHalfHeight, radius * n0.z };
        const Vec3 bottom1{ radius * n1.x, -cylinderHalfHeight, radius * n1.z };
        const Vec3 top0{ radius * n0.x, cylinderHalfHeight, radius * n0.z };
        const Vec3 top1{ radius * n1.x, cylinderHalfHeight, radius * n1.z };

        AddSmoothTriangle(vertices, bottom0, n0, bottom1, n1, top1, n1);
        AddSmoothTriangle(vertices, bottom0, n0, top1, n1, top0, n0);
    }

    // Top/bottom hemisphere caps, centered on the cylinder's own end rings -
    // see AddHemisphere()'s comment for why winding never has to be reasoned
    // about here.
    AddHemisphere(vertices, Vec3{ 0.0f, cylinderHalfHeight, 0.0f }, radius, 1.0f, capLatitudeSegments, radialSegments);
    AddHemisphere(
        vertices, Vec3{ 0.0f, -cylinderHalfHeight, 0.0f }, radius, -1.0f, capLatitudeSegments, radialSegments);

    return vertices;
}

std::vector<Vertex> PrimitiveMeshGenerator::GenerateCone()
{
    constexpr float radius = 0.5f;
    constexpr float height = 1.0f;
    constexpr int radialSegments = 24;

    const Vec3 apex{ 0.0f, height * 0.5f, 0.0f };
    const Vec3 baseCenter{ 0.0f, -height * 0.5f, 0.0f };

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(radialSegments) * 6);

    for (int i = 0; i < radialSegments; ++i) {
        const float a0 = kTwoPi * (static_cast<float>(i) / static_cast<float>(radialSegments));
        const float a1 = kTwoPi * (static_cast<float>(i + 1) / static_cast<float>(radialSegments));

        const Vec3 base0{ radius * std::cos(a0), -height * 0.5f, radius * std::sin(a0) };
        const Vec3 base1{ radius * std::cos(a1), -height * 0.5f, radius * std::sin(a1) };

        // Lateral face - flat-shaded per facet (a faceted "low-poly" cone
        // rather than a smoothly-curved one - see the class comment).
        // Order (apex, base1, base0) is what makes the computed face normal
        // point outward-and-up, matching the cone's true slope (see
        // tests/Renderer/PrimitiveMeshGeneratorTests.cpp for the derivation).
        AddFlatTriangle(vertices, apex, base1, base0);

        // Base cap - a flat disk facing straight down (0,-1,0);
        // (center, base0, base1) already yields exactly that normal.
        AddFlatTriangle(vertices, baseCenter, base0, base1);
    }

    return vertices;
}

std::vector<Vertex> PrimitiveMeshGenerator::GeneratePlane()
{
    constexpr float h = 0.5f;
    const Vec3 a{ -h, 0.0f, -h };
    const Vec3 b{ -h, 0.0f, h };
    const Vec3 c{ h, 0.0f, h };
    const Vec3 d{ h, 0.0f, -h };

    std::vector<Vertex> vertices;
    AddFlatQuad(vertices, a, b, c, d); // outward normal is +Y - see AddFlatQuad()'s comment.
    return vertices;
}

} // namespace gte
