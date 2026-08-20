#include "EditorCamera.h"

namespace gte {

void EditorCamera::Update(Vec2 mouseDelta, float scrollDelta, bool middleMouseDown, bool rightMouseDown) noexcept
{
    // Rotate first, so a frame that (unusually) both rotates AND
    // pans/dollies moves along the JUST-updated orientation, not last
    // frame's - see the field comment on m_yawDegrees/m_pitchDegrees for why
    // these accumulate independently rather than being decomposed back out
    // of m_transform.rotation every frame.
    if (rightMouseDown) {
        // Screen-space dx>0 (mouse moved right) turns the view right: this
        // engine's yaw convention already rotates Forward() TOWARD Right()
        // for a positive angle (see Quat.h), so this falls out directly
        // with no sign flip needed.
        m_yawDegrees += mouseDelta.x * kDegreesPerPixel;

        // Screen-space dy>0 (mouse moved down) tilts the view down: a
        // positive pitch (rotation around local Right()) tilts Forward()'s
        // Y component negative (see EditorCamera.h's derivation notes /
        // tests/Editor/EditorCameraTests.cpp), i.e. downward - so this too
        // falls out directly, no sign flip needed.
        m_pitchDegrees = Clamp(m_pitchDegrees + mouseDelta.y * kDegreesPerPixel, -kMaxPitchDegrees, kMaxPitchDegrees);

        m_transform.rotation = Quat::FromEulerDegrees(m_pitchDegrees, m_yawDegrees, 0.0f);
    }

    const Vec3 right = m_transform.rotation.RotateVector(Vec3::Right());
    const Vec3 up = m_transform.rotation.RotateVector(Vec3::Up());
    const Vec3 forward = m_transform.rotation.RotateVector(Vec3::Forward());

    if (middleMouseDown) {
        // "Grab and drag" feel (matches Unity): dragging right should make
        // the SCENE appear to follow the cursor to the right, which means
        // the CAMERA itself moves left (and vice versa for up/down) -
        // hence the negated X term. Y is screen-space (down-positive), so
        // it is negated again relative to world-space Up() to get the same
        // "content follows the drag" feel vertically.
        m_transform.position += right * (-mouseDelta.x * kPanUnitsPerPixel) + up * (mouseDelta.y * kPanUnitsPerPixel);
    }

    if (scrollDelta != 0.0f) {
        m_transform.position += forward * (scrollDelta * kZoomUnitsPerWheelNotch);
    }
}

} // namespace gte
