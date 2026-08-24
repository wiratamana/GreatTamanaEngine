#pragma once

#include <string>

namespace gte {

// Plain playback-state component - added to a model's ROOT entity (the one
// CreateMeshEntityFromGtaFile() returns - see MeshAssetSource.h above) by
// Game::PlayAnimationOnEntity() to drive that model's own bone-deformed
// rendering from an imported *.gta AssetType::Animation clip (see
// Assets/MotionData.h/VmdLoader.h) every Game::Update() call
// (Game::UpdateSkeletalAnimators(), src/Game/Game.cpp) - this engine's
// first real "animate the model" step (see TODO.md, "Real MMD
// skinning/animation runtime").
//
// `meshGtaPath`/`animationGtaPath` are plain path-string keys back into
// Game's own private, path-keyed caches (m_meshSkinningCache/
// m_animationClipCache/m_resolvedAnimationBindingCache - src/Game/Game.h) -
// the exact same "component stores a stable lookup key, never a live
// pointer into engine-owned cache storage" convention MeshAssetSource.h
// already uses, so this component stays as trivially copyable/Tier-1-
// testable as every other one (see AGENTS.md, "Entity-Component-System").
//
// Deliberately NOT storing the model's own live SkeletonData/MotionData
// (which can be large - hundreds of bones/keyframes) directly - that data
// is shared/cached once per distinct asset path (see Game.h) and
// re-looked-up by these keys every Update(), never duplicated per entity/
// instance. See Game::PlayAnimationOnEntity()'s own doc comment for the one
// documented consequence of this sharing: two SIMULTANEOUSLY-animated
// entities spawned from the exact same model *.gta currently fight over
// the same shared GPU mesh buffers (whichever's pose is computed last each
// frame wins) - a deliberate, documented scope limitation for this first
// pass, not an oversight (see TODO.md).
struct SkeletalAnimator {
    std::string meshGtaPath;
    std::string animationGtaPath;

    // VMD's own fixed 30fps frame grid - kept as a FRACTIONAL frame number
    // (not an integer count) so a `speed` other than an exact multiple of
    // the frame rate still advances smoothly rather than snapping
    // frame-to-frame.
    float frame = 0.0f;
    float speed = 1.0f;
    bool playing = true;
    bool loop = true;
};

} // namespace gte
