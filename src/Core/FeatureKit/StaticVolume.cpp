#include "StaticVolume.h"

#include "FrameContext.h"
#include "PersistentTarget.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Assets/GtvFile.h"
#include "../../Renderer/GraphicsHandleRegistry.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/StaticImage.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/ViewScopeUtil.h"

#include <array>
#include <exception>
#include <optional>
#include <string>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "FeatureKit";

PixelFormat PixelFormatOf(GtvFormat format) noexcept
{
    return format == GtvFormat::R8Unorm ? PixelFormat::R8_UNorm : PixelFormat::RGBA8_UNorm;
}

// The file, the GPU image and the per-view graph names behind StaticVolume and StaticTexture.
class StaticImageSlot {
public:
    explicit StaticImageSlot(std::string_view assetFile)
        : m_file(assetFile)
    {
    }

    ~StaticImageSlot()
    {
        if (m_sampler.handle != SamplerHandle::Invalid) {
            UnregisterSampler(m_sampler.handle);
        }
    }

    StaticImageSlot(const StaticImageSlot&) = delete;
    StaticImageSlot& operator=(const StaticImageSlot&) = delete;

    const StaticImage* Image() const noexcept { return m_image.has_value() ? &*m_image : nullptr; }

    // The image, loaded now if needed; null while the load is failing or cooling down.
    const StaticImage* Ensure(Frame& frame, detail::FrameContext& context, bool wantVolume)
    {
        if (m_image.has_value()) {
            return &*m_image;
        }
        const std::uint64_t frameIndex = context.builder->FrameIndex();
        if (!m_gate.ShouldAttempt(frameIndex)) {
            return nullptr;
        }
        if (const std::string error = Load(context, wantVolume); !error.empty()) {
            m_gate.OnFailure(frameIndex);
            frame.ReportOnce("Static image '" + m_file + "' could not be loaded: " + error);
            return nullptr;
        }
        if (m_gate.OnSuccess()) {
            GTE_LOG_INFO(kLogCategory, "Static image '" + m_file + "' loaded after an earlier failure.");
        }
        return &*m_image;
    }

    Texture ImportVolume(Frame& frame, detail::FrameContext& context)
    {
        const StaticImage& image = *m_image;
        const rg::TextureHandle handle = context.builder->ImportVolumeTexture(
            NameFor(context), image.VolumeTargetOf(), ImageLayout::ShaderReadOnlyOptimal, SamplerOf(image));
        return frame.Stamp(handle);
    }

    Texture ImportTexture2D(Frame& frame, detail::FrameContext& context)
    {
        const StaticImage& image = *m_image;
        const rg::TextureHandle handle = context.builder->ImportTexture(
            NameFor(context), image.Target2D(), ImageLayout::ShaderReadOnlyOptimal, SamplerOf(image));
        return frame.Stamp(handle);
    }

private:
    // Empty on success, else the reason.
    std::string Load(detail::FrameContext& context, bool wantVolume)
    {
        const GtvParseResult parsed = ReadGtvFile(EngineAssetPath(m_file));
        if (!parsed.Ok()) {
            return std::string(GtvErrorName(parsed.error)) + " (" + EngineAssetPath(m_file) + ")";
        }
        const GtvImage& file = parsed.image;
        if (file.IsVolume() != wantVolume) {
            return wantVolume ? "the file is a 2D texture, a 3D volume was expected"
                              : "the file is a 3D volume, a 2D texture was expected";
        }
        const StaticImageDesc desc{ .texels = file.texels.data(),
            .width = file.width,
            .height = file.height,
            .depth = file.depth,
            .format = PixelFormatOf(file.format),
            .addressMode = file.Tileable() ? TextureAddressMode::Repeat : TextureAddressMode::ClampToEdge,
            .debugName = m_file.c_str() };
        try {
            m_image.emplace(context.core->GetRenderer().CreateStaticImage(desc));
        } catch (const std::exception& e) {
            return std::string("GPU upload failed: ") + e.what();
        }
        return {};
    }

    // Stable per view: the graph keeps the pointer for the frame.
    const char* NameFor(const detail::FrameContext& context)
    {
        std::string& name = m_names[rg::ViewScopeIndex(context.scope)];
        if (name.empty()) {
            name = PersistentName("Static." + m_file, context);
        }
        return name.c_str();
    }

    SamplerHandle SamplerOf(const StaticImage& image) noexcept { return m_sampler.Resolve(image.Sampler()); }

    std::string m_file;
    std::optional<StaticImage> m_image;
    CachedSamplerHandle m_sampler;
    rg::BuildRetryGate m_gate;
    std::array<std::string, rg::kViewScopeCount> m_names;
};

detail::FrameContext* UsableContext(Frame& frame)
{
    detail::FrameContext* context = frame.Context();
    if (context == nullptr || context->builder == nullptr || context->core == nullptr) {
        frame.ReportOnce("A static image needs a frame that belongs to a render callback");
        return nullptr;
    }
    return context;
}

} // namespace

struct StaticVolume::Impl {
    explicit Impl(std::string_view file)
        : slot(file)
    {
    }
    StaticImageSlot slot;
};

StaticVolume::StaticVolume(std::string_view assetFile)
    : m_impl(std::make_unique<Impl>(assetFile))
{
}

StaticVolume::~StaticVolume() = default;

Texture StaticVolume::Acquire(Frame& frame)
{
    detail::FrameContext* context = UsableContext(frame);
    if (context == nullptr || m_impl->slot.Ensure(frame, *context, true) == nullptr) {
        return Texture{};
    }
    return m_impl->slot.ImportVolume(frame, *context);
}

VolumeSize StaticVolume::Size() const noexcept
{
    const StaticImage* image = m_impl->slot.Image();
    return image == nullptr ? VolumeSize{}
                            : VolumeSize{ static_cast<std::uint32_t>(image->Width()),
                                  static_cast<std::uint32_t>(image->Height()),
                                  static_cast<std::uint32_t>(image->Depth()) };
}

struct StaticTexture::Impl {
    explicit Impl(std::string_view file)
        : slot(file)
    {
    }
    StaticImageSlot slot;
};

StaticTexture::StaticTexture(std::string_view assetFile)
    : m_impl(std::make_unique<Impl>(assetFile))
{
}

StaticTexture::~StaticTexture() = default;

Texture StaticTexture::Acquire(Frame& frame)
{
    detail::FrameContext* context = UsableContext(frame);
    if (context == nullptr || m_impl->slot.Ensure(frame, *context, false) == nullptr) {
        return Texture{};
    }
    return m_impl->slot.ImportTexture2D(frame, *context);
}

Extent2D StaticTexture::Size() const noexcept
{
    const StaticImage* image = m_impl->slot.Image();
    return image == nullptr ? Extent2D{}
                            : Extent2D{ static_cast<std::uint32_t>(image->Width()),
                                  static_cast<std::uint32_t>(image->Height()) };
}

} // namespace gte::fx
