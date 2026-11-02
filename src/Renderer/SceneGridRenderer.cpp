#include "SceneGridRenderer.h"

#include "Pipeline.h"
#include "Renderer.h"
#include "ShaderFile.h"
#include "RenderGraph/CommandBuffer.h"
#include "../Core/Logging.h"
#include "../Core/PipelineDesc.h"

#include <array>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Fragment-only push constants of SceneGrid.frag. Every vec3 is padded to a vec4.
// For a pixel at NDC (x, y): ray origin = origin + x * originDx + y * originDy, ray direction = dir + x * dirDx + y * dirDy.
struct GridPush {
    float origin[4];
    float originDx[4];
    float originDy[4];
    float dir[4];
    float dirDx[4];
    float dirDy[4];
    float depthRowZ[4]; // Row 2 of view * projection.
    float depthRowW[4]; // Row 3 of view * projection.
};
static_assert(sizeof(GridPush) == 128, "layout differs from SceneGrid.frag");

using Vec3d = std::array<double, 3>;

// Inverts a 4x4 matrix in double precision (Gauss-Jordan, partial pivoting). `m` is row-major.
bool InvertDouble(const double (&m)[4][4], double (&out)[4][4]) noexcept
{
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[r][c];
            a[r][4 + c] = (r == c) ? 1.0 : 0.0;
        }
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (!(std::fabs(a[pivot][col]) > 1.0e-300)) {
            return false;
        }
        for (int c = 0; c < 8; ++c) {
            std::swap(a[col][c], a[pivot][c]);
        }
        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) {
            a[col][c] *= inv;
        }
        for (int r = 0; r < 4; ++r) {
            if (r == col) {
                continue;
            }
            const double k = a[r][col];
            for (int c = 0; c < 8; ++c) {
                a[r][c] -= k * a[col][c];
            }
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[r][c] = a[r][4 + c];
        }
    }
    return true;
}

// World point of NDC (x, y, z) through the inverse matrix, in double precision.
bool UnprojectDouble(const double (&inv)[4][4], double x, double y, double z, Vec3d& out) noexcept
{
    const double in[4] = { x, y, z, 1.0 };
    double v[4];
    for (int r = 0; r < 4; ++r) {
        v[r] = inv[r][0] * in[0] + inv[r][1] * in[1] + inv[r][2] * in[2] + inv[r][3] * in[3];
    }
    if (!(std::fabs(v[3]) > 1.0e-12)) {
        return false;
    }
    out = { v[0] / v[3], v[1] / v[3], v[2] / v[3] };
    return true;
}

void Store(float (&dst)[4], const Vec3d& v) noexcept
{
    dst[0] = static_cast<float>(v[0]);
    dst[1] = static_cast<float>(v[1]);
    dst[2] = static_cast<float>(v[2]);
    dst[3] = 0.0f;
}

// Near and far plane points are affine in NDC xy, so the ray of any pixel follows from three samples. Doing the
// inversion and the differences in double keeps the ground hit exact when the eye is thousands of units away.
bool BuildGridPush(const Mat4& viewProjection, GridPush& push) noexcept
{
    double m[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            m[r][c] = static_cast<double>(viewProjection(r, c));
        }
    }
    double inv[4][4];
    if (!InvertDouble(m, inv)) {
        return false;
    }

    Vec3d near00, near10, near01, far00, far10, far01;
    if (!UnprojectDouble(inv, 0.0, 0.0, 0.0, near00) || !UnprojectDouble(inv, 1.0, 0.0, 0.0, near10)
        || !UnprojectDouble(inv, 0.0, 1.0, 0.0, near01) || !UnprojectDouble(inv, 0.0, 0.0, 1.0, far00)
        || !UnprojectDouble(inv, 1.0, 0.0, 1.0, far10) || !UnprojectDouble(inv, 0.0, 1.0, 1.0, far01)) {
        return false;
    }

    Vec3d originDx, originDy, dir, dirDx, dirDy;
    for (int i = 0; i < 3; ++i) {
        originDx[i] = near10[i] - near00[i];
        originDy[i] = near01[i] - near00[i];
        dir[i] = far00[i] - near00[i];
        dirDx[i] = (far10[i] - near10[i]) - dir[i];
        dirDy[i] = (far01[i] - near01[i]) - dir[i];
    }

    Store(push.origin, near00);
    Store(push.originDx, originDx);
    Store(push.originDy, originDy);
    Store(push.dir, dir);
    Store(push.dirDx, dirDx);
    Store(push.dirDy, dirDy);
    for (int c = 0; c < 4; ++c) {
        push.depthRowZ[c] = viewProjection(2, c);
        push.depthRowW[c] = viewProjection(3, c);
    }
    return true;
}

} // namespace

SceneGridRenderer::~SceneGridRenderer()
{
    if (m_pipeline) {
        m_renderer.IdleForTeardown("scene grid");
    }
}

bool SceneGridRenderer::EnsurePipeline()
{
    if (m_pipeline) {
        return true;
    }
    if (m_buildFailed) {
        return false;
    }

    // Fullscreen triangle from gl_VertexIndex. Depth test on, depth write off: a translucent overlay must not touch depth.
    PipelineDesc desc = PipelinePreset::AlphaBlend(m_renderer.ScenePixelFormat(), m_renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    desc.layout.pushConstantByteSize = sizeof(GridPush);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    try {
        const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/SceneGrid.vert.spv");
        const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/SceneGrid.frag.spv");
        m_pipeline = std::make_unique<Pipeline>(m_renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
    } catch (const std::exception& e) {
        m_buildFailed = true;
        GTE_LOG_ERROR("SceneGrid", std::string("Grid pipeline could not be built: ") + e.what());
        return false;
    }
    return true;
}

void SceneGridRenderer::Draw(const rg::RecordingContext& ctx, const Mat4& sceneViewProjection)
{
    GridPush push;
    if (!BuildGridPush(sceneViewProjection, push)) {
        return; // Degenerate camera matrix this frame.
    }
    if (!EnsurePipeline()) {
        return;
    }

    rg::CommandBuffer cmd = ctx.Commands();
    cmd.BindPipeline(*m_pipeline);
    cmd.PushConstants(push, ShaderStageMask::Fragment);
    cmd.Draw(3);
}

} // namespace gte
