#include "Collider.h"

#include "BoxCollider.h"
#include "CapsuleCollider.h"
#include "SphereCollider.h"

namespace gte {

void SolveCollision(VerletParticle& particle, const Collider& collider) noexcept
{
    switch (collider.shape) {
    case ColliderShape::Sphere:
        SolveSphereCollision(particle, SphereCollider{ collider.center, collider.size.x });
        return;
    case ColliderShape::Box:
        SolveBoxCollision(particle, BoxCollider{ collider.center, collider.rotation, collider.size });
        return;
    case ColliderShape::Capsule:
        SolveCapsuleCollision(
            particle, CapsuleCollider{ collider.center, collider.rotation, collider.size.x, collider.size.y });
        return;
    }
}

} // namespace gte
