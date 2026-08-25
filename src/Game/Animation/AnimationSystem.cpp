#include "AnimationSystem.h"

#include "../../Animation/AnimationPoseEvaluator.h"
#include "../../Animation/VertexSkinning.h"
#include "../../ECS/Components/MeshAssetSource.h"
#include "../../ECS/Components/SkeletalAnimator.h"
#include "../../Profiling/ScopeTimer.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/MeshVertex.h"
#include "../Instantiation/MeshInstantiationSystem.h"
#include "../Instantiation/MeshVertexPacking.h"
#include "../RenderSystem.h"

#include <cmath>

namespace gte {

namespace {

// VMD's own fixed frame grid - see Assets/MotionData.h's own file comment.
constexpr float kVmdFramesPerSecond = 30.0f;

} // namespace

bool AnimationSystem::Play(Registry& registry, Entity targetEntity, const std::string& absoluteAnimationGtaPath)
{
    if (!registry.IsAlive(targetEntity)) {
        return false;
    }

    const MeshAssetSource* source = registry.TryGetComponent<MeshAssetSource>(targetEntity);
    if (source == nullptr) {
        return false; // Not a model root spawned by MeshInstantiationSystem::SpawnMeshAsset().
    }

    const SkinnedMeshData* skinData = m_rigCache.TryGet(source->gtaPath);
    if (skinData == nullptr || skinData->skeleton.bones.empty()) {
        return false; // A boneless/riggless model - nothing to animate.
    }

    if (m_clipCache.GetOrLoad(absoluteAnimationGtaPath) == nullptr) {
        return false;
    }

    SkeletalAnimator& animator = registry.AddComponent<SkeletalAnimator>(targetEntity);
    animator.meshGtaPath = source->gtaPath;
    animator.animationGtaPath = absoluteAnimationGtaPath;
    animator.frame = 0.0f;
    animator.speed = 1.0f;
    animator.playing = true;
    animator.loop = true;
    return true;
}

void AnimationSystem::Update(Registry& registry, double deltaSeconds)
{
    GTE_PROFILE_SCOPE("AnimationSystem::Update");

    ComponentStorage<SkeletalAnimator>& animators = registry.Storage<SkeletalAnimator>();

    for (std::size_t i = 0; i < animators.Size(); ++i) {
        SkeletalAnimator& animator = animators.ComponentAt(i);
        if (!animator.playing || animator.animationGtaPath.empty()) {
            continue;
        }

        const SkinnedMeshData* skinData = m_rigCache.TryGet(animator.meshGtaPath);
        if (skinData == nullptr) {
            continue; // Its model's own skinning data isn't (or is no longer) cached - nothing to do.
        }

        const MotionData* motion = m_clipCache.TryGet(animator.animationGtaPath);
        if (motion == nullptr) {
            continue; // Its clip isn't (or is no longer) cached.
        }

        // Resolved once per distinct (mesh, animation) pair, then reused
        // every frame afterwards - see ResolvedAnimationBindingCache.h.
        const AnimationBindingKey bindingKey{ animator.meshGtaPath, animator.animationGtaPath };
        const ResolvedAnimationBinding& binding = m_bindingCache.GetOrCompute(bindingKey, skinData->skeleton, *motion);

        animator.frame += static_cast<float>(deltaSeconds) * kVmdFramesPerSecond * animator.speed;
        if (binding.lastFrame > 0) {
            const float loopLength = static_cast<float>(binding.lastFrame) + 1.0f;
            if (animator.loop) {
                animator.frame = std::fmod(animator.frame, loopLength);
                if (animator.frame < 0.0f) {
                    animator.frame += loopLength;
                }
            } else if (animator.frame > static_cast<float>(binding.lastFrame)) {
                animator.frame = static_cast<float>(binding.lastFrame);
                animator.playing = false;
            }
        }

        // Sample -> IK-solve -> append/grant-inherit -> forward-kinematics,
        // in that exact, correctness-critical fixed order - see
        // Animation/AnimationPoseEvaluator.h's own file comment. This pure
        // math module is NOT touched by this refactor at all.
        const std::vector<Mat4> skinningMatrices =
            EvaluateAnimatedSkinningPose(skinData->skeleton, binding, animator.frame);

        std::vector<Vec3> skinnedPositions;
        std::vector<Vec3> skinnedNormals;
        SkinVertices(skinData->bindPositions, skinData->bindNormals, skinData->skinWeights, skinningMatrices,
            skinnedPositions, skinnedNormals);

        const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(animator.meshGtaPath);
        if (parts == nullptr) {
            continue;
        }

        // Re-upload EVERY one of this model's mesh parts - each part's own
        // GPU vertex buffer holds a full copy of the whole model's vertex
        // data, so all of them need the same freshly-skinned positions/
        // normals, just reformatted per part's own vertex layout via the
        // SHARED MeshVertexPacking helpers (the exact same functions
        // MeshAssetGpuCatalog used at initial load time) instead of the two
        // duplicated inline loops this used to be.
        for (const MeshAssetPart& part : *parts) {
            Mesh* gpuMesh = m_renderSystem.TryGetMesh(part.mesh);
            if (gpuMesh == nullptr) {
                continue;
            }

            if (part.texture.IsValid()) {
                const std::vector<MeshVertexUv> vertices =
                    PackMeshVertexUvs(skinnedPositions, skinnedNormals, skinData->uvs);
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertexUv));
            } else {
                const std::vector<MeshVertex> vertices = PackMeshVertices(skinnedPositions, skinnedNormals);
                gpuMesh->UpdateVertexData(vertices.data(), vertices.size() * sizeof(MeshVertex));
            }
        }
    }
}

} // namespace gte
