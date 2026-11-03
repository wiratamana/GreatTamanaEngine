#include "ToonMaterialClass.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace gte {

namespace {

enum class ToonRamp : std::uint8_t { None, Skin, Hair, Other };

bool EqualsNoCase(std::string_view a, std::string_view b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// File name without folders and extension.
std::string_view StemOf(std::string_view path) noexcept
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    const std::size_t dot = path.find_last_of('.');
    return dot != std::string_view::npos ? path.substr(0, dot) : path;
}

ToonRamp RampOf(const Material& material, std::span<const MaterialTextureRef> textures) noexcept
{
    if (material.useSharedToon) {
        return ToonRamp::Other;
    }
    if (material.toonTextureIndex < 0 || static_cast<std::size_t>(material.toonTextureIndex) >= textures.size()) {
        return ToonRamp::None;
    }
    const std::string_view stem = StemOf(textures[static_cast<std::size_t>(material.toonTextureIndex)].sourcePath);
    if (EqualsNoCase(stem, "skin")) {
        return ToonRamp::Skin;
    }
    if (EqualsNoCase(stem, "hair")) {
        return ToonRamp::Hair;
    }
    return ToonRamp::Other;
}

} // namespace

ToonClass ClassifyToonMaterial(const Material& material, std::span<const MaterialTextureRef> textures) noexcept
{
    const ToonRamp ramp = RampOf(material, textures);
    const bool plus = material.name.ends_with('+');
    const bool additiveSphere = material.sphereMode == SphereTextureMode::Add;
    const bool face = material.englishName == "Mat_Face";
    const bool hairMaterial = material.englishName == "Mat_Hair" || material.englishName == "Mat_EffectHair";

    if (!material.useSharedToon && material.toonTextureIndex < 0) {
        return ToonClass::Eye;
    }
    if (plus) {
        return additiveSphere ? ToonClass::HairOverlay : ToonClass::ClothBack;
    }
    if (additiveSphere) {
        return ToonClass::Gem;
    }
    if (face) {
        return ramp == ToonRamp::Hair ? ToonClass::FaceLine : ToonClass::FaceSkin;
    }
    if (hairMaterial && ramp == ToonRamp::Hair) {
        return ToonClass::Hair;
    }
    if (ramp == ToonRamp::Skin) {
        return ToonClass::Skin;
    }
    return ToonClass::Cloth;
}

bool ToonClassDrawsEdge(ToonClass toonClass, bool drawEdgeFlag) noexcept
{
    switch (toonClass) {
    case ToonClass::Eye:
    case ToonClass::FaceLine:
    case ToonClass::HairOverlay:
    case ToonClass::ClothBack:
        return false;
    default:
        return drawEdgeFlag;
    }
}

MaterialParams MakeToonParamsFor(const Material& material, std::span<const MaterialTextureRef> textures) noexcept
{
    const ToonClass toonClass = ClassifyToonMaterial(material, textures);
    return MakeToonMaterialParams(static_cast<std::uint32_t>(toonClass), material.edgeSize / kToonReferenceEdgeSize,
        ToonClassDrawsEdge(toonClass, material.drawEdge));
}

} // namespace gte
