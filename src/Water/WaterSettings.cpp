#include "WaterSettings.h"

#include "WaterHash.h"

#include <algorithm>
#include <cmath>

namespace gte::water {

namespace {

float ClampF(float v, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

void ClampColor(float (&c)[3], const float (&fallback)[3]) noexcept
{
    for (int i = 0; i < 3; ++i) {
        c[i] = ClampF(c[i], 0.0f, 1.0f, fallback[i]);
    }
}

bool SameColor(const float (&a)[3], const float (&b)[3]) noexcept
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

} // namespace

const char* WaterBodyKindName(WaterBodyKind kind) noexcept
{
    switch (kind) {
    case WaterBodyKind::Ocean: return "ocean";
    case WaterBodyKind::Lake: return "lake";
    default: return "river";
    }
}

bool TryParseWaterBodyKind(const std::string& name, WaterBodyKind& out) noexcept
{
    if (name == "ocean") { out = WaterBodyKind::Ocean; return true; }
    if (name == "lake") { out = WaterBodyKind::Lake; return true; }
    if (name == "river") { out = WaterBodyKind::River; return true; }
    return false;
}

const char* WaterQualityName(WaterQuality q) noexcept
{
    switch (q) {
    case WaterQuality::Low: return "low";
    case WaterQuality::Medium: return "medium";
    default: return "high";
    }
}

bool TryParseWaterQuality(const std::string& name, WaterQuality& out) noexcept
{
    if (name == "low") { out = WaterQuality::Low; return true; }
    if (name == "medium") { out = WaterQuality::Medium; return true; }
    if (name == "high") { out = WaterQuality::High; return true; }
    return false;
}

std::vector<Point2> MakeBoxPolygon(double cx, double cz, double sx, double sz)
{
    const double hx = std::abs(sx) * 0.5;
    const double hz = std::abs(sz) * 0.5;
    return { { cx - hx, cz - hz }, { cx + hx, cz - hz }, { cx + hx, cz + hz }, { cx - hx, cz + hz } };
}

WaterSettings Sanitize(const WaterSettings& in) noexcept
{
    const WaterSettings d{};
    WaterSettings s = in;
    s.seaLevel = ClampF(s.seaLevel, -500.0f, 500.0f, d.seaLevel);
    s.waveHeight = ClampF(s.waveHeight, 0.0f, 12.0f, d.waveHeight);
    s.choppiness = ClampF(s.choppiness, 0.0f, 1.0f, d.choppiness);
    s.waveSpeed = ClampF(s.waveSpeed, 0.0f, 8.0f, d.waveSpeed);
    s.windDirectionDeg = ClampF(s.windDirectionDeg, -720.0f, 720.0f, d.windDirectionDeg);
    s.waveLength = ClampF(s.waveLength, 2.0f, 400.0f, d.waveLength);
    ClampColor(s.shallowColor, d.shallowColor);
    ClampColor(s.deepColor, d.deepColor);
    ClampColor(s.foamColor, d.foamColor);
    s.clarity = ClampF(s.clarity, 0.1f, 200.0f, d.clarity);
    s.foamAmount = ClampF(s.foamAmount, 0.0f, 2.0f, d.foamAmount);
    s.wetBand = ClampF(s.wetBand, 0.0f, 20.0f, d.wetBand);
    s.wetDarken = ClampF(s.wetDarken, 0.0f, 1.0f, d.wetDarken);
    s.waveCount = std::clamp(s.waveCount, 1, 8);
    if (static_cast<int>(s.quality) > static_cast<int>(WaterQuality::High)) {
        s.quality = d.quality;
    }
    return s;
}

std::uint64_t Hash(const WaterSettings& s) noexcept
{
    Fnv f;
    f.Add(s.seaLevel); f.Add(s.waveHeight); f.Add(s.choppiness); f.Add(s.waveSpeed);
    f.Add(s.windDirectionDeg); f.Add(s.waveLength);
    for (int i = 0; i < 3; ++i) {
        f.Add(s.shallowColor[i]); f.Add(s.deepColor[i]); f.Add(s.foamColor[i]);
    }
    f.Add(s.clarity); f.Add(s.foamAmount); f.Add(s.wetBand); f.Add(s.wetDarken);
    f.Add(s.ssrEnabled); f.Add(s.waveCount); f.Add(static_cast<std::uint8_t>(s.quality));
    f.Add(s.carveTerrain); f.Add(s.hideOceanQuad);
    return f.h;
}

bool operator==(const WaterSettings& a, const WaterSettings& b) noexcept
{
    return a.seaLevel == b.seaLevel && a.waveHeight == b.waveHeight && a.choppiness == b.choppiness
        && a.waveSpeed == b.waveSpeed && a.windDirectionDeg == b.windDirectionDeg && a.waveLength == b.waveLength
        && SameColor(a.shallowColor, b.shallowColor) && SameColor(a.deepColor, b.deepColor)
        && SameColor(a.foamColor, b.foamColor) && a.clarity == b.clarity && a.foamAmount == b.foamAmount
        && a.wetBand == b.wetBand && a.wetDarken == b.wetDarken && a.ssrEnabled == b.ssrEnabled
        && a.waveCount == b.waveCount && a.quality == b.quality && a.carveTerrain == b.carveTerrain
        && a.hideOceanQuad == b.hideOceanQuad;
}

WaterSettings ApplyQuality(WaterSettings s, WaterQuality quality) noexcept
{
    s.quality = quality;
    switch (quality) {
    case WaterQuality::Low: s.waveCount = 2; s.ssrEnabled = false; break;
    case WaterQuality::Medium: s.waveCount = 4; s.ssrEnabled = false; break;
    default: s.waveCount = 8; s.ssrEnabled = true; break;
    }
    return s;
}

} // namespace gte::water
