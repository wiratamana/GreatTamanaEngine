#include "TransformGizmo.h"

#include "../Math/Quat.h"
#include "../Math/Vec3.h"
#include "../Math/Vec4.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <cstddef>
#include <cstring>

namespace gte {

namespace {

ImGuizmo::OPERATION ToImGuizmoOperation(GizmoOperation operation) noexcept
{
    switch (operation) {
        case GizmoOperation::Translate:
            return ImGuizmo::TRANSLATE;
        case GizmoOperation::Rotate:
            return ImGuizmo::ROTATE;
        case GizmoOperation::Scale:
            return ImGuizmo::SCALE;
    }
    return ImGuizmo::TRANSLATE; // unreachable - silences a "not all control paths return" warning on MSVC
}

} // namespace

void BeginGizmoFrame()
{
    ImGuizmo::BeginFrame();
}

void DrawGizmoOperationSwitcher(GizmoOperation& operation)
{
    struct Entry {
        const char* label;
        GizmoOperation operation;
    };
    static constexpr Entry kEntries[] = {
        { "Move", GizmoOperation::Translate },
        { "Rotate", GizmoOperation::Rotate },
        { "Scale", GizmoOperation::Scale },
    };

    ImGui::BeginGroup();
    for (std::size_t i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }

        const bool isActive = (operation == kEntries[i].operation);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }
        if (ImGui::Button(kEntries[i].label)) {
            operation = kEntries[i].operation;
        }
        if (isActive) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();
}

bool ManipulateTransformGizmo(GizmoOperation operation, const Mat4& view, const Mat4& projection, float rectX,
    float rectY, float rectWidth, float rectHeight, Transform& transform, const Mat4& parentWorld, bool orthographic)
{
    // Draws into the current window's draw list: call it while the Scene window is current.
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetOrthographic(orthographic);
    ImGuizmo::SetRect(rectX, rectY, rectWidth, rectHeight);

    // Manipulate in world space (parent world * local). ImGuizmo edits the float[16] in place.
    const Mat4 worldMatrix = parentWorld * transform.LocalToWorldMatrix();
    float matrix[16];
    std::memcpy(matrix, worldMatrix.Data(), sizeof(matrix));

    const bool manipulated = ImGuizmo::Manipulate(
        view.Data(), projection.Data(), ToImGuizmoOperation(operation), ImGuizmo::LOCAL, matrix);

    // Rebuild position, rotation and scale from the matrix columns.
    // Not ImGuizmo's Euler helper: its Euler order differs from Quat::FromEulerDegrees.
    if (manipulated) {
        // Back to the entity's local space.
        Mat4 manipulatedWorld;
        std::memcpy(manipulatedWorld.columns, matrix, sizeof(matrix));

        Mat4 parentInverse;
        if (!parentWorld.TryInverse(parentInverse)) {
            parentInverse = Mat4::Identity();
        }
        const Mat4 local = parentInverse * manipulatedWorld;

        // Column lengths are the scales. Dividing them out leaves the rotation.
        const Vec3 col0{ local(0, 0), local(1, 0), local(2, 0) };
        const Vec3 col1{ local(0, 1), local(1, 1), local(2, 1) };
        const Vec3 col2{ local(0, 2), local(1, 2), local(2, 2) };

        const Vec3 scale{ Length(col0), Length(col1), Length(col2) };

        Mat4 rotationOnly = Mat4::Identity();
        rotationOnly.columns[0] = Vec4(scale.x > kEpsilon ? col0 / scale.x : Vec3::Right(), 0.0f);
        rotationOnly.columns[1] = Vec4(scale.y > kEpsilon ? col1 / scale.y : Vec3::Up(), 0.0f);
        rotationOnly.columns[2] = Vec4(scale.z > kEpsilon ? col2 / scale.z : Vec3::Forward(), 0.0f);

        transform.position = Vec3{ local(0, 3), local(1, 3), local(2, 3) };
        transform.rotation = Quat::FromMat4(rotationOnly);
        transform.scale = scale;
    }

    return manipulated;
}

GizmoIdScope::GizmoIdScope(const char* id)
{
    ImGuizmo::PushID(id);
}

GizmoIdScope::~GizmoIdScope()
{
    ImGuizmo::PopID();
}

bool IsGizmoOver()
{
    return ImGuizmo::IsOver();
}

bool IsGizmoUsing()
{
    return ImGuizmo::IsUsing();
}

void CancelGizmoInteraction()
{
    ImGuizmo::Enable(false);
    ImGuizmo::Enable(true);
}

} // namespace gte
