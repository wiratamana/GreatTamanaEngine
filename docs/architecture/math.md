# Math

_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._

`src/Math/` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) is a from-scratch math
library — no GLM dependency, the same "own the core data model" philosophy
as the hand-rolled ECS (see [Entity-Component-System](ecs.md)). `Mat4` is column-major/column-vector (matches
GLSL's `mat4` layout exactly, so `Mat4::Data()` uploads to a push
constant/uniform with zero transpose) and the engine's coordinate system is
left-handed, Y-up, Z-forward.
