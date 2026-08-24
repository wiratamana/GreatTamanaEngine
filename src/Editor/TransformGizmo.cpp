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
    float rectY, float rectWidth, float rectHeight, Transform& transform, const Mat4& parentWorld)
{
    // Appends to the CURRENT window's ImDrawList (see this function's own
    // header comment) - ScenePanel.cpp calls this while "Scene" is still
    // the current ImGui window, right after its ImGui::Image() call.
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetRect(rectX, rectY, rectWidth, rectHeight);

    // ImGuizmo's `matrix` parameter is both input (where to draw/manipulate
    // the gizmo from) and output (the manipulated result) - it needs a
    // plain, mutable float[16], never Mat4::Data() directly (that's
    // `const float*`). Column-major layout is bit-identical to gte::Mat4's
    // own (see Mat4.h), so a straight memcpy is all that's needed - no
    // transpose, no repacking. Manipulated in WORLD space (parentWorld *
    // transform's own local matrix - see this function's own header
    // comment) rather than `transform`'s local matrix directly, so a
    // parented entity's gizmo actually lines up with where it's really
    // drawn in "Scene" (RenderSystem draws it via
    // ECS/TransformHierarchy.h's ComputeWorldMatrix(), the exact same
    // parentWorld * local composition).
    const Mat4 worldMatrix = parentWorld * transform.LocalToWorldMatrix();
    float matrix[16];
    std::memcpy(matrix, worldMatrix.Data(), sizeof(matrix));

    const bool manipulated = ImGuizmo::Manipulate(
        view.Data(), projection.Data(), ToImGuizmoOperation(operation), ImGuizmo::LOCAL, matrix);

    if (manipulated) {
        // Deliberately NOT ImGuizmo::DecomposeMatrixToComponents() here -
        // its own header comment flags "numerical stability issues", and
        // more importantly its rotation output is Euler degrees in
        // ImGuizmo's OWN composition order, which is not guaranteed to
        // match gte::Quat::FromEulerDegrees()'s Yaw*Pitch*Roll convention
        // (see Quat.h). InspectorPanel.cpp gets away with that round trip
        // (ToEulerDegrees() -> DragFloat3 -> FromEulerDegrees()) because
        // it's this engine's OWN convention on both ends; mixing in a
        // second library's Euler convention here would silently fight the
        // mouse every frame of a rotate drag instead. Reading the
        // manipulated matrix's raw columns and going through
        // Quat::FromMat4() instead sidesteps Euler order entirely - plain
        // linear algebra, exact (up to float error) regardless of either
        // library's Euler convention.
        //
        // `matrix` is the manipulated WORLD matrix - convert back to
        // `transform`'s own LOCAL space first (parentWorld's inverse *
        // world), THEN decompose that into position/rotation/scale, so a
        // parented entity's Transform fields stay correctly relative to
        // its parent (for a root entity, parentWorld == Identity() and
        // this is a no-op, exactly the previous behavior):
        //   - translation is column 3, verbatim.
        //   - Transform::LocalToWorldMatrix() builds T * R * S (see
        //     Mat4::TRS()), so columns 0/1/2 are the rotation's own
        //     right/up/forward basis vectors, each additionally scaled by
        //     scale.x/y/z respectively - each column's own length IS that
        //     axis's scale, and dividing it back out recovers the pure
        //     rotation basis.
        Mat4 manipulatedWorld;
        std::memcpy(manipulatedWorld.columns, matrix, sizeof(matrix));

        Mat4 parentInverse;
        if (!parentWorld.TryInverse(parentInverse)) {
            parentInverse = Mat4::Identity();
        }
        const Mat4 local = parentInverse * manipulatedWorld;

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

} // namespace gte
