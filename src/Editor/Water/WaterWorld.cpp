#include "WaterWorld.h"

#include "../../Water/WaterCarve.h"
#include "../../Water/WaterEdit.h"
#include "../../Water/WaterJson.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace gte {

namespace {

constexpr double kFeatureRetrySeconds = 1.0;
constexpr double kShoreSearchRange = 200.0;
constexpr double kShoreSearchStep = 2.0;

std::string Fmt(double v)
{
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.4f", v);
    return buffer;
}

std::string Color(const float (&c)[3]) { return Fmt(c[0]) + "," + Fmt(c[1]) + "," + Fmt(c[2]); }

water::WaterResult Fail(const std::string& message)
{
    water::WaterResult r;
    r.ok = false;
    r.message = message;
    return r;
}

} // namespace

WaterWorld::WaterWorld(ServicesFn services)
    : m_services(std::move(services))
{
    m_bodies.push_back(water::MakeOceanBody(m_settings.seaLevel));
    m_bodies.back().id = m_nextId++;
    m_set = water::WaterBodySet::Build(m_bodies, m_settings);
    m_bodies = m_set->Bodies();
}

water::WaterBody* WaterWorld::FindBody(std::vector<water::WaterBody>& bodies, std::uint32_t id) const
{
    for (water::WaterBody& b : bodies) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

water::WaterResult WaterWorld::Commit(std::vector<water::WaterBody> bodies, const water::WaterSettings& settings, std::uint32_t id,
    const std::string& message)
{
    const auto next = water::WaterBodySet::Build(std::move(bodies), settings);
    if (id != 0 && next->Find(id) == nullptr) {
        return Fail(next->Issues().empty() ? "the body was refused" : next->Issues().back());
    }
    m_settings = next->Settings();
    m_bodies = next->Bodies();
    m_set = next;
    ++m_revision;
    m_lastEdit = m_now;
    m_featureDirty = true;
    const std::uint64_t applied = m_appliedCarveSet ? m_appliedCarveSet->CarveHash() : 0;
    m_carvePending = m_set->CarveHash() != applied;
    for (const water::WaterBody& b : m_bodies) {
        m_nextId = std::max(m_nextId, b.id + 1);
    }
    m_lastError.clear();
    water::WaterResult ok;
    ok.id = id;
    ok.message = message;
    return ok;
}

water::WaterResult WaterWorld::SetSettings(const water::WaterSettings& settings)
{
    water::WaterSettings clean = water::Sanitize(settings);
    std::vector<water::WaterBody> bodies = m_bodies;
    for (water::WaterBody& b : bodies) {
        if (b.kind == water::WaterBodyKind::Ocean) {
            b.level = clean.seaLevel;
        }
    }
    return Commit(std::move(bodies), clean, 0, "settings applied");
}

water::WaterResult WaterWorld::AddBody(const water::WaterBody& body)
{
    std::vector<water::WaterBody> bodies = m_bodies;
    water::WaterBody added = body;
    added.id = m_nextId;
    if (added.name.empty()) {
        added.name = std::string(water::WaterBodyKindName(added.kind)) + " " + std::to_string(added.id);
    }
    bodies.push_back(std::move(added));
    water::WaterResult r = Commit(std::move(bodies), m_settings, m_nextId, "body added");
    if (!r.ok && static_cast<int>(m_bodies.size()) >= water::kMaxBodies) {
        r.message = "at most 16 water bodies";
    }
    return r;
}

water::WaterResult WaterWorld::EditBody(std::uint32_t id, const std::function<bool(water::WaterBody&, std::string&)>& edit)
{
    std::vector<water::WaterBody> bodies = m_bodies;
    water::WaterBody* body = FindBody(bodies, id);
    if (body == nullptr) {
        return Fail("no water body with id " + std::to_string(id));
    }
    std::string error;
    if (!edit(*body, error)) {
        return Fail(error);
    }
    return Commit(std::move(bodies), m_settings, id, "body changed");
}

water::WaterResult WaterWorld::UpdateBody(const water::WaterBody& body)
{
    return EditBody(body.id, [&body](water::WaterBody& target, std::string&) {
        target = body;
        return true;
    });
}

water::WaterResult WaterWorld::RemoveBody(std::uint32_t id)
{
    std::vector<water::WaterBody> bodies = m_bodies;
    const auto it = std::remove_if(bodies.begin(), bodies.end(), [id](const water::WaterBody& b) { return b.id == id; });
    if (it == bodies.end()) {
        return Fail("no water body with id " + std::to_string(id));
    }
    bodies.erase(it, bodies.end());
    return Commit(std::move(bodies), m_settings, 0, "body removed");
}

water::WaterResult WaterWorld::ClearBodies()
{
    return Commit({}, m_settings, 0, "all bodies removed");
}

water::WaterResult WaterWorld::MovePoint(std::uint32_t id, int index, double x, double z, std::optional<float> surfaceY, std::optional<float> width)
{
    return EditBody(id, [&](water::WaterBody& b, std::string& error) { return water::EditMovePoint(b, index, x, z, surfaceY, width, error); });
}

water::WaterResult WaterWorld::InsertPoint(std::uint32_t id, int index, double x, double z)
{
    return EditBody(id, [&](water::WaterBody& b, std::string& error) { return water::EditInsertPoint(b, index, x, z, error); });
}

water::WaterResult WaterWorld::RemovePoint(std::uint32_t id, int index)
{
    return EditBody(id, [&](water::WaterBody& b, std::string& error) { return water::EditRemovePoint(b, index, error); });
}

water::WaterResult WaterWorld::SaveFile(const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return Fail("cannot write " + path.string());
    }
    file << water::WorldToJson(*m_set);
    return file.good() ? water::WaterResult{ true, "saved " + std::to_string(m_bodies.size()) + " bodies", 0 } : Fail("write failed");
}

water::WaterResult WaterWorld::LoadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Fail("cannot read " + path.string());
    }
    std::stringstream text;
    text << file.rdbuf();
    water::WaterSettings settings = m_settings;
    std::vector<water::WaterBody> bodies;
    std::string error;
    if (!water::ParseWorldJson(text.str(), settings, bodies, error)) {
        return Fail(error);
    }
    const std::string message = "loaded " + std::to_string(bodies.size()) + " bodies";
    return Commit(std::move(bodies), settings, 0, message);
}

std::optional<terrain::TerrainHeightProbe> WaterWorld::TerrainHeight(double x, double z) const
{
    const WaterWorldServices services = m_services();
    return services.terrain != nullptr ? services.terrain->ProbeHeight(x, z) : std::nullopt;
}

water::WaterProbeResult WaterWorld::Probe(double x, double z) const
{
    water::WaterProbeResult out;
    const auto ground = TerrainHeight(x, z);
    out.water = m_set->SampleAt(x, z);
    if (!ground) {
        return out;
    }
    out.terrainKnown = true;
    out.baseHeight = ground->baseHeight;
    out.carvedHeight = ground->carvedHeight;
    const bool wetHere = out.water.hasWater && (out.water.kind != water::WaterBodyKind::Ocean || ground->carvedHeight < out.water.surfaceY);
    out.depth = wetHere ? static_cast<double>(out.water.surfaceY) - ground->carvedHeight : 0.0;

    const auto wetAt = [this](double px, double pz) {
        const water::WaterSample s = m_set->SampleAt(px, pz);
        if (!s.hasWater) {
            return false;
        }
        if (s.kind != water::WaterBodyKind::Ocean) {
            return true;
        }
        const auto h = TerrainHeight(px, pz);
        return h && h->carvedHeight < s.surfaceY;
    };
    const bool start = wetAt(x, z);
    double best = 1.0e30;
    for (int dir = 0; dir < 16; ++dir) {
        const double a = dir * 3.14159265358979323846 / 8.0;
        for (double d = kShoreSearchStep; d <= kShoreSearchRange && d < best; d += kShoreSearchStep) {
            if (wetAt(x + std::cos(a) * d, z + std::sin(a) * d) != start) {
                best = d;
                break;
            }
        }
    }
    if (best < 1.0e29) {
        out.shoreDistance = best;
    }
    return out;
}

water::WaterStats WaterWorld::Stats() const
{
    water::WaterStats s;
    s.revision = m_revision;
    s.bodyCount = static_cast<int>(m_bodies.size());
    s.bodiesHash = m_set->Hash();
    s.carveHash = m_appliedCarveSet ? m_appliedCarveSet->CarveHash() : 0;
    s.carvePending = m_carvePending;
    s.carveApplies = m_carveApplies;
    s.carveDirtyRects = m_carveRects;
    s.featureConnected = m_featureConnected;
    s.featurePushes = m_featurePushes;
    s.oceanQuadHidden = m_quadHidden;
    s.issues = m_set->Issues();
    s.lastError = m_lastError;
    return s;
}

bool WaterWorld::PushToFeature(const WaterWorldServices& services)
{
    fx::IFeatureDebugEndpoint* feature = services.waterFeature;
    if (feature == nullptr) {
        return false;
    }
    try {
        const water::WaterSettings& s = m_settings;
        const std::vector<fx::FeatureDebugArg> look = {
            { "seaLevel", Fmt(s.seaLevel) },
            { "waveCount", std::to_string(s.waveCount) },
            { "waveHeight", Fmt(s.waveHeight) },
            { "choppiness", Fmt(s.choppiness) },
            { "waveSpeed", Fmt(s.waveSpeed) },
            { "windDirectionDeg", Fmt(s.windDirectionDeg) },
            { "baseWavelength", Fmt(s.waveLength) },
            { "shallowColor", Color(s.shallowColor) },
            { "deepColor", Color(s.deepColor) },
            { "foamColor", Color(s.foamColor) },
            { "clarity", Fmt(s.clarity) },
            { "crestFoam", Fmt(s.foamAmount) },
            { "shoreFoam", Fmt(s.foamAmount) },
            { "ssr", s.ssrEnabled ? "true" : "false" },
            { "wetBand", Fmt(std::max(0.1f, s.wetBand)) },
            { "wetStrength", Fmt(s.wetDarken) },
        };
        const fx::FeatureDebugResult set = feature->Set(look);
        if (!set.ok) {
            m_lastError = "Water feature refused " + set.badKey + ": " + set.message;
            return false;
        }
        feature->Invoke("clear_bodies", {});
        for (const water::WaterBody& b : m_bodies) {
            std::vector<fx::FeatureDebugArg> args;
            args.push_back({ "waveScale", Fmt(std::clamp(b.look.waveScale, 0.0f, 2.0f)) });
            const char* action = "add_ocean";
            if (b.kind == water::WaterBodyKind::Ocean) {
                args.push_back({ "level", Fmt(s.seaLevel) });
            } else if (b.kind == water::WaterBodyKind::Lake) {
                action = "add_lake";
                args.push_back({ "level", Fmt(b.level) });
                std::string points;
                for (const water::Point2& p : b.polygon) {
                    points += (points.empty() ? "" : ";") + Fmt(p.x) + "," + Fmt(p.z);
                }
                args.push_back({ "points", points });
            } else {
                action = "add_river";
                std::string points;
                for (const water::RiverPoint& p : b.river) {
                    points += (points.empty() ? "" : ";") + Fmt(p.x) + "," + Fmt(p.z) + "," + Fmt(p.surfaceY) + "," + Fmt(0.5 * p.width);
                }
                const int perSegment = std::clamp(63 / std::max(1, static_cast<int>(b.river.size()) - 1), 1, 32);
                args.push_back({ "points", points });
                args.push_back({ "spline", "1" });
                args.push_back({ "samples", std::to_string(perSegment) });
            }
            const fx::FeatureDebugResult added = feature->Invoke(action, args);
            if (!added.ok) {
                m_lastError = "Water feature refused " + b.name + ": " + added.message;
                return false;
            }
        }
        ++m_featurePushes;
        return true;
    } catch (const std::exception& e) {
        m_lastError = e.what();
        return false;
    }
}

void WaterWorld::PushCarve(const WaterWorldServices& services)
{
    std::vector<terrain::Rect2> dirty;
    water::WaterBodySet::DirtyRects(m_appliedCarveSet.get(), *m_set, dirty);
    m_carveRects = static_cast<int>(dirty.size());
    if (services.terrain != nullptr) {
        const terrain::TerrainResult result = services.terrain->SetCarve(water::WaterCarve::Build(*m_set), dirty);
        if (!result.ok) {
            m_lastError = "terrain refused the carve: " + result.message;
            return;
        }
    }
    m_appliedCarveSet = m_set;
    m_carvePending = false;
    ++m_carveApplies;
}

void WaterWorld::Tick(double nowSeconds)
{
    m_now = nowSeconds;
    const WaterWorldServices services = m_services();

    if (m_carvePending && (m_flushRequested || m_now - m_lastEdit >= m_debounce)) {
        PushCarve(services);
    }
    m_flushRequested = false;

    if (m_featureDirty) {
        if (services.waterFeature == nullptr) {
            m_featureConnected = false;
        } else if (m_now - m_lastFeatureTry >= 0.0) {
            m_featureConnected = PushToFeature(services);
            m_featureDirty = !m_featureConnected;
            m_lastFeatureTry = m_now + (m_featureConnected ? 0.0 : kFeatureRetrySeconds);
        }
    }

    if (services.terrain != nullptr) {
        const bool hide = m_featureConnected && m_settings.hideOceanQuad;
        if (hide != m_quadHidden) {
            services.terrain->SetOceanQuadVisible(!hide);
            m_quadHidden = hide;
        }
    }
}

} // namespace gte
