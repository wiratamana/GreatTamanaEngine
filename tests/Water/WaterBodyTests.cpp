// Tier 1: water settings, geometry, body set, JSON.

#include "Water/WaterBodySet.h"
#include "Water/WaterJson.h"
#include "Water/WaterMath.h"
#include "Water/WaterSettings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte::water {

namespace {

WaterBody Lake(double cx, double cz, double sx, double sz, float level = 10.0f)
{
    WaterBody b;
    b.name = "Lake";
    b.kind = WaterBodyKind::Lake;
    b.level = level;
    b.polygon = MakeBoxPolygon(cx, cz, sx, sz);
    return b;
}

WaterBody River(float y0 = 20.0f)
{
    WaterBody b;
    b.name = "River";
    b.kind = WaterBodyKind::River;
    b.river = { { 0.0, 0.0, y0, 10.0f }, { 100.0, 40.0, y0 - 2.0f, 12.0f }, { 200.0, 0.0, y0 - 4.0f, 14.0f }, { 300.0, -40.0, y0 - 6.0f, 14.0f } };
    return b;
}

} // namespace

TEST(WaterSettingsTests, SanitizeFixesNanAndRanges)
{
    WaterSettings s;
    s.waveHeight = std::numeric_limits<float>::quiet_NaN();
    s.choppiness = 5.0f;
    s.waveCount = 99;
    s.shallowColor[1] = -3.0f;
    const WaterSettings c = Sanitize(s);
    EXPECT_EQ(c.waveHeight, WaterSettings{}.waveHeight);
    EXPECT_EQ(c.choppiness, 1.0f);
    EXPECT_EQ(c.waveCount, 8);
    EXPECT_EQ(c.shallowColor[1], 0.0f);
}

TEST(WaterSettingsTests, HashAndEqualityTrackEveryChange)
{
    const WaterSettings a;
    WaterSettings b = a;
    EXPECT_TRUE(a == b);
    EXPECT_EQ(Hash(a), Hash(b));
    b.windDirectionDeg += 1.0f;
    EXPECT_FALSE(a == b);
    EXPECT_NE(Hash(a), Hash(b));
    WaterSettings c = a;
    c.deepColor[2] = 0.9f;
    EXPECT_NE(Hash(a), Hash(c));
    WaterSettings d = a;
    d.ssrEnabled = !d.ssrEnabled;
    EXPECT_NE(Hash(a), Hash(d));
}

TEST(WaterSettingsTests, QualityPresetChangesWaveCountAndSsr)
{
    const WaterSettings low = ApplyQuality({}, WaterQuality::Low);
    const WaterSettings high = ApplyQuality({}, WaterQuality::High);
    EXPECT_LT(low.waveCount, high.waveCount);
    EXPECT_FALSE(low.ssrEnabled);
    EXPECT_TRUE(high.ssrEnabled);
}

TEST(WaterSettingsTests, JsonRoundTripAndKeyNames)
{
    WaterSettings s;
    s.waveHeight = 3.5f;
    s.quality = WaterQuality::High;
    s.foamColor[0] = 0.25f;
    s.ssrEnabled = true;
    WaterSettings back;
    std::string error;
    ASSERT_TRUE(ParseSettingsJson(SettingsToJson(s), back, error)) << error;
    EXPECT_TRUE(s == back);
    EXPECT_GE(SettingsKeyNames().size(), 15u);

    WaterSettings t;
    EXPECT_TRUE(SetSettingByName(t, "waveHeight", "2.5", error));
    EXPECT_EQ(t.waveHeight, 2.5f);
    EXPECT_TRUE(SetSettingByName(t, "shallowColor", "0.1,0.2,0.3", error));
    EXPECT_EQ(t.shallowColor[2], 0.3f);
    EXPECT_TRUE(SetSettingByName(t, "quality", "low", error));
    EXPECT_FALSE(SetSettingByName(t, "nope", "1", error));
    EXPECT_FALSE(SetSettingByName(t, "waveHeight", "abc", error));
    EXPECT_FALSE(ParseSettingsJson("{\"waveHeight\":\"x\"}", t, error));
    EXPECT_FALSE(ParseSettingsJson("not json", t, error));
}

TEST(WaterSettingsTests, SeabedSkipAndHalfResAreSavedHashedAndFollowThePreset)
{
    const WaterSettings d;
    EXPECT_TRUE(d.cullDeepSeabed);
    EXPECT_FALSE(d.halfRes);
    EXPECT_TRUE(ApplyQuality(d, WaterQuality::Low).halfRes);
    EXPECT_FALSE(ApplyQuality(d, WaterQuality::Medium).halfRes);

    WaterSettings s = d;
    s.cullDeepSeabed = false;
    s.halfRes = true;
    s.seabedCullDepth = 90.0f;
    EXPECT_NE(Hash(d), Hash(s));
    EXPECT_FALSE(d == s);
    WaterSettings back;
    std::string error;
    ASSERT_TRUE(ParseSettingsJson(SettingsToJson(s), back, error)) << error;
    EXPECT_TRUE(s == back);
    EXPECT_TRUE(SetSettingByName(back, "halfRes", "false", error));
    EXPECT_FALSE(back.halfRes);

    WaterSettings wild = d;
    wild.seabedCullDepth = 1.0e9f;
    EXPECT_EQ(Sanitize(wild).seabedCullDepth, 500.0f);
}

TEST(WaterPolygonTests, BoxSignedDistanceAndConcaveShape)
{
    const auto box = MakeBoxPolygon(0.0, 0.0, 100.0, 60.0);
    EXPECT_NEAR(PolygonSignedDistance(box, 0.0, 0.0), -30.0, 1e-9);
    EXPECT_NEAR(PolygonSignedDistance(box, 60.0, 0.0), 10.0, 1e-9);
    EXPECT_NEAR(PolygonSignedDistance(box, 50.0, 0.0), 0.0, 1e-9);
    EXPECT_TRUE(PointInPolygon(box, 49.0, 29.0));
    EXPECT_FALSE(PointInPolygon(box, 51.0, 0.0));

    // L shape: the notch is outside.
    const std::vector<Point2> l = { { 0, 0 }, { 100, 0 }, { 100, 40 }, { 40, 40 }, { 40, 100 }, { 0, 100 } };
    EXPECT_TRUE(PointInPolygon(l, 20.0, 80.0));
    EXPECT_FALSE(PointInPolygon(l, 80.0, 80.0));
    EXPECT_GT(PolygonSignedDistance(l, 80.0, 80.0), 0.0);
    EXPECT_NEAR(PolygonSignedDistance(l, 80.0, 80.0), 40.0, 1e-9);
    EXPECT_FALSE(PolygonSelfIntersects(l));
    EXPECT_NEAR(std::abs(PolygonArea(l)), 6400.0, 1e-9);
}

TEST(WaterPolygonTests, BowTieIsSelfIntersecting)
{
    const std::vector<Point2> bow = { { 0, 0 }, { 100, 100 }, { 100, 0 }, { 0, 100 } };
    EXPECT_TRUE(PolygonSelfIntersects(bow));
}

TEST(WaterSplineTests, FlattenPassesThroughControlPointsAndIsContinuous)
{
    const std::vector<RiverPoint> pts = River().river;
    const auto segs = FlattenRiver(pts, 12.0);
    ASSERT_GT(segs.size(), 20u);
    EXPECT_DOUBLE_EQ(segs.front().ax, pts.front().x);
    EXPECT_DOUBLE_EQ(segs.back().bx, pts.back().x);
    for (std::size_t i = 1; i < segs.size(); ++i) {
        EXPECT_DOUBLE_EQ(segs[i].ax, segs[i - 1].bx);
        EXPECT_DOUBLE_EQ(segs[i].az, segs[i - 1].bz);
        EXPECT_LE(std::hypot(segs[i].bx - segs[i].ax, segs[i].bz - segs[i].az), 16.0);
        EXPECT_LE(segs[i].ya, segs[i - 1].ya + 1e-6f);
    }
    for (const RiverPoint& p : pts) {
        const RiverHit hit = NearestOnRiver(segs, p.x, p.z);
        EXPECT_LT(hit.distance, 1e-6);
        EXPECT_NEAR(hit.surfaceY, p.surfaceY, 1e-3f);
        EXPECT_NEAR(hit.width, p.width, 1e-3f);
    }
}

TEST(WaterSplineTests, EnforceDescentNeverRises)
{
    std::vector<RiverPoint> pts = { { 0, 0, 5.0f, 5.0f }, { 1, 0, 9.0f, 5.0f }, { 2, 0, 3.0f, 5.0f }, { 3, 0, 4.0f, 5.0f } };
    EnforceDescent(pts);
    for (std::size_t i = 1; i < pts.size(); ++i) {
        EXPECT_LE(pts[i].surfaceY, pts[i - 1].surfaceY);
    }
    EXPECT_FLOAT_EQ(pts[1].surfaceY, 5.0f);
    EXPECT_FLOAT_EQ(pts[3].surfaceY, 3.0f);
}

TEST(WaterSplineTests, NearestOnStraightRiver)
{
    const std::vector<RiverPoint> pts = { { 0, 0, 10.0f, 8.0f }, { 100, 0, 10.0f, 8.0f } };
    const auto segs = FlattenRiver(pts, 10.0);
    const RiverHit hit = NearestOnRiver(segs, 50.0, 7.0);
    EXPECT_NEAR(hit.distance, 7.0, 1e-9);
    EXPECT_NEAR(hit.along, 50.0f, 1e-3f);
    EXPECT_EQ(NearestOnRiver({}, 0, 0).segment, -1);
}

TEST(WaterBodySetTests, CapsIdsAndInvalidBodies)
{
    std::vector<WaterBody> bodies;
    for (int i = 0; i < 20; ++i) {
        bodies.push_back(Lake(i * 500.0, 0.0, 100.0, 100.0));
    }
    const auto set = WaterBodySet::Build(bodies, {});
    EXPECT_EQ(static_cast<int>(set->Bodies().size()), kMaxBodies);
    EXPECT_FALSE(set->Issues().empty());
    std::vector<std::uint32_t> ids;
    for (const WaterBody& b : set->Bodies()) {
        ids.push_back(b.id);
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    EXPECT_NE(ids.front(), 0u);

    WaterBody bad;
    bad.kind = WaterBodyKind::Lake;
    bad.polygon = { { 0, 0 }, { 1, 1 } };
    WaterBody bow;
    bow.kind = WaterBodyKind::Lake;
    bow.polygon = { { 0, 0 }, { 100, 100 }, { 100, 0 }, { 0, 100 } };
    WaterBody oceanA;
    oceanA.kind = WaterBodyKind::Ocean;
    WaterBody oceanB = oceanA;
    const auto s2 = WaterBodySet::Build({ bad, bow, oceanA, oceanB }, {});
    ASSERT_EQ(s2->Bodies().size(), 1u);
    EXPECT_EQ(s2->Bodies()[0].kind, WaterBodyKind::Ocean);
    EXPECT_EQ(s2->Issues().size(), 3u);
}

TEST(WaterBodySetTests, DuplicateIdsAreRepaired)
{
    WaterBody a = Lake(0, 0, 50, 50);
    WaterBody b = Lake(200, 0, 50, 50);
    a.id = 7;
    b.id = 7;
    const auto set = WaterBodySet::Build({ a, b }, {});
    ASSERT_EQ(set->Bodies().size(), 2u);
    EXPECT_NE(set->Bodies()[0].id, set->Bodies()[1].id);
}

TEST(WaterBodySetTests, OceanFollowsSeaLevelAndSampleFindsHighestWater)
{
    WaterSettings settings;
    settings.seaLevel = 2.0f;
    WaterBody ocean;
    ocean.kind = WaterBodyKind::Ocean;
    const auto set = WaterBodySet::Build({ ocean, Lake(0, 0, 100, 100, 50.0f), River(30.0f) }, settings);
    const WaterSample far = set->SampleAt(5000.0, 5000.0);
    EXPECT_TRUE(far.hasWater);
    EXPECT_EQ(far.kind, WaterBodyKind::Ocean);
    EXPECT_FLOAT_EQ(far.surfaceY, 2.0f);
    const WaterSample lake = set->SampleAt(10.0, 10.0);
    EXPECT_EQ(lake.kind, WaterBodyKind::Lake);
    EXPECT_FLOAT_EQ(lake.surfaceY, 50.0f);
    EXPECT_LT(lake.edgeDistance, 0.0);
    const WaterSample river = set->SampleAt(200.0, 0.0);
    EXPECT_EQ(river.kind, WaterBodyKind::River);
    EXPECT_NEAR(river.surfaceY, 26.0f, 0.5f);
    const WaterSample none = WaterBodySet::Build({ Lake(0, 0, 10, 10) }, {})->SampleAt(500.0, 500.0);
    EXPECT_FALSE(none.hasWater);
}

TEST(WaterBodySetTests, HashesTrackWhatMatters)
{
    WaterBody a = Lake(0, 0, 100, 100);
    const auto base = WaterBodySet::Build({ a }, {});
    EXPECT_EQ(base->Hash(), WaterBodySet::Build({ a }, {})->Hash());
    EXPECT_NE(base->CarveHash(), 0u);

    WaterBody renamed = a;
    renamed.name = "Other";
    const auto r = WaterBodySet::Build({ renamed }, {});
    EXPECT_NE(base->Hash(), r->Hash());
    EXPECT_EQ(base->CarveHash(), r->CarveHash());

    WaterBody deeper = a;
    deeper.depth += 1.0f;
    EXPECT_NE(base->CarveHash(), WaterBodySet::Build({ deeper }, {})->CarveHash());

    WaterBody noCarve = a;
    noCarve.carveTerrain = false;
    EXPECT_EQ(WaterBodySet::Build({ noCarve }, {})->CarveHash(), 0u);
}

TEST(WaterBodySetTests, DirtyRectsCoverOldAndNewAndSkipLookEdits)
{
    WaterBody a = Lake(0, 0, 100, 100);
    a.id = 1;
    const auto before = WaterBodySet::Build({ a }, {});

    std::vector<Rect2> rects;
    WaterBodySet::DirtyRects(nullptr, *before, rects);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_LE(rects[0].minX, -50.0);

    WaterBody moved = a;
    moved.polygon = MakeBoxPolygon(2000.0, 0.0, 100.0, 100.0);
    const auto after = WaterBodySet::Build({ moved }, {});
    rects.clear();
    WaterBodySet::DirtyRects(before.get(), *after, rects);
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_TRUE(rects[0].Overlaps({ -10, -10, 10, 10 }) != rects[1].Overlaps({ -10, -10, 10, 10 }));
    EXPECT_TRUE(rects[0].Overlaps({ 1990, -10, 2010, 10 }) != rects[1].Overlaps({ 1990, -10, 2010, 10 }));

    WaterBody recolored = a;
    recolored.look.overrideColors = true;
    rects.clear();
    WaterBodySet::DirtyRects(before.get(), *WaterBodySet::Build({ recolored }, {}), rects);
    EXPECT_TRUE(rects.empty());

    rects.clear();
    WaterBodySet::DirtyRects(before.get(), *WaterBodySet::Build({}, {}), rects);
    EXPECT_EQ(rects.size(), 1u);
}

TEST(WaterBodySetTests, WorldJsonRoundTripKeepsHash)
{
    WaterSettings settings;
    settings.waveHeight = 2.0f;
    WaterBody ocean;
    ocean.kind = WaterBodyKind::Ocean;
    const auto set = WaterBodySet::Build({ ocean, Lake(10, 20, 80, 60, 12.0f), River() }, settings);
    WaterSettings parsedSettings;
    std::vector<WaterBody> parsedBodies;
    std::string error;
    ASSERT_TRUE(ParseWorldJson(WorldToJson(*set), parsedSettings, parsedBodies, error)) << error;
    const auto again = WaterBodySet::Build(parsedBodies, parsedSettings);
    EXPECT_EQ(set->Hash(), again->Hash());
    EXPECT_EQ(set->CarveHash(), again->CarveHash());
    EXPECT_FALSE(ParseWorldJson("{\"bodies\":[{\"kind\":\"swamp\"}]}", parsedSettings, parsedBodies, error));
    EXPECT_FALSE(ParseWorldJson("{\"bodies\":[{\"river\":[[1,2,3]]}]}", parsedSettings, parsedBodies, error));
}

} // namespace gte::water
