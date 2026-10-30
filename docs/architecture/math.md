# Math
> The conventions of `src/Math/`: axes, matrix layout, angles, depth range. Read before writing any transform or camera code.
> Verified against code: 2026-10-30

## TL;DR
- The math library is written from scratch. The engine code does not use GLM.
- World is left-handed: X right, Y up, Z forward.
- `Mat4` is column-major with column vectors: `M * v`. It uploads to GLSL `mat4` with no transpose.
- Angles are radians inside the engine. Degrees only appear at the edges (Inspector, HTTP, camera `fovYDegrees`).
- Depth range is 0 to 1 (Vulkan), not -1 to 1.

## Diagram
```text
        +Y (up)
         │   +Z (forward)
         │  /
         │ /
         └────── +X (right)

child world = parentWorld * local
(A * B) * v = A * (B * v)        apply B first, then A
```

## Conventions
| Topic | Rule |
|---|---|
| Axes | `Vec3::Right()` = X, `Up()` = Y, `Forward()` = Z. `Cross(Right, Up) == Forward`. |
| Matrix storage | Column-major. `columns[c][r]` is column `c`, row `r`. `Data()` gives 16 floats. |
| Compose | `Mat4::TRS(position, rotation, scale)` is Translate * Rotate * Scale. |
| View | `LookAtLH(eye, target, up)`. View space has +Z in front of the camera. |
| Projection | `PerspectiveFovLH_ZO(fovYRadians, aspect, near, far, flipY)` and `OrthographicLH_ZO`. |
| Y flip | The camera passes `flipY = true`. Do not also use a negative viewport height. Pick one. |
| Euler | `Quat::FromEulerDegrees(pitchX, yawY, rollZ)`. Order: roll, then pitch, then yaw. |
| Default `Mat4()` | All zero. Use `Mat4::Identity()` for identity. |
| Layout | `Vec2` and `Vec3` are tight (`Vec3` is 12 bytes). `Vec4`, `Mat4`, `Quat` are `alignas(16)`. No SIMD code yet. |

## Key files
| File | Role |
|---|---|
| `src/Math/MathTypes.h` | `kPi`, `DegToRad`, `Clamp`, `Lerp`, `ApproximatelyEqual` |
| `src/Math/Vec2.h`, `Vec3.h`, `Vec4.h` | Vectors |
| `src/Math/Mat4.*` | Matrix, projections, inverse |
| `src/Math/Quat.*` | Quaternion, slerp, matrix conversion |
| `src/Math/PbrBrdf.h`, `ToneMapCurves.h` | Shared shading and tone curve math |

## Rules
1. Never store degrees in internal math. Convert at the edge.
2. Do not transpose a `Mat4` before upload. The layout already matches GLSL.
3. Math code is pure. It is safe in jobs. See [Job system](../conventions/job-system.md).

## Gotchas
- `Inverse()` of a singular matrix returns identity (and asserts in debug). Use `TryInverse()` when it can happen.
- Euler angles have gimbal lock at pitch +/-90 degrees. Store rotations as `Quat`.
- GLM exists only inside the PMX and VMD loaders. No GLM type leaves those two files.

## Related
- [ECS](ecs.md) · [Architecture overview](overview.md) · [Rendering](rendering.md)
- [Job system](../conventions/job-system.md) · [Glossary](../glossary.md)
