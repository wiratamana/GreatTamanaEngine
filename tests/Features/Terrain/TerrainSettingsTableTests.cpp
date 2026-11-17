#include "Features/Terrain/TerrainApplyPolicy.h"
#include "Features/Terrain/TerrainSettingsKeys.h"
#include "Features/Terrain/TerrainSettingsTable.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>

namespace gte::terrain {

TEST(TerrainSettingsTableTest, EveryFieldIsListedExactlyOnce)
{
    std::set<std::string> table;
    for (const FieldDesc& field : SettingFields()) {
        ASSERT_NE(field.name, nullptr);
        EXPECT_TRUE(table.insert(field.name).second) << "duplicate " << field.name;
    }
    std::set<std::string> names;
    for (const std::string_view name : SettingNames()) {
        names.emplace(name);
    }
    EXPECT_EQ(table, names);
}

TEST(TerrainSettingsTableTest, EveryFieldHasAMemberLabelGroupAndRange)
{
    for (const FieldDesc& field : SettingFields()) {
        SCOPED_TRACE(field.name);
        EXPECT_NE(field.label, nullptr);
        EXPECT_GT(std::strlen(field.label), 0u);
        EXPECT_LE(field.min, field.max);
        bool grouped = false;
        for (const char* group : SettingGroups()) {
            grouped = grouped || std::strcmp(group, field.group) == 0;
        }
        EXPECT_TRUE(grouped);
        const int members = (field.floatMember != nullptr) + (field.intMember != nullptr) + (field.boolMember != nullptr)
            + (field.seedMember != nullptr);
        EXPECT_EQ(members, 1);
    }
}

TEST(TerrainSettingsTableTest, DefaultsLieInsideTheSliderRange)
{
    const TerrainSettings defaults = Sanitize(TerrainSettings{});
    for (const FieldDesc& field : SettingFields()) {
        SCOPED_TRACE(field.name);
        const double value = GetFieldValue(defaults, field);
        EXPECT_GE(value, field.min);
        EXPECT_LE(value, field.max);
    }
}

TEST(TerrainSettingsTableTest, SliderEndsSurviveSanitize)
{
    // The one dependent field: rock slope is raised to the grass slope.
    for (const FieldDesc& field : SettingFields()) {
        if (field.kind == FieldKind::Bool || field.kind == FieldKind::Seed || field.kind == FieldKind::Choice) {
            continue;
        }
        for (const double end : { field.min, field.max }) {
            SCOPED_TRACE(std::string(field.name) + " " + std::to_string(end));
            TerrainSettings s;
            SetFieldValue(s, field, end);
            const TerrainSettings clean = Sanitize(s);
            const double got = GetFieldValue(clean, field);
            if (std::strcmp(field.name, "rockMinSlopeDeg") == 0) {
                EXPECT_GE(got, end);
                continue;
            }
            EXPECT_NEAR(got, end, std::max(1e-4, std::abs(end) * 1e-4));
        }
    }
}

TEST(TerrainSettingsTableTest, ChunkQuadChoicesAreStable)
{
    for (const std::int32_t choice : ChunkQuadChoices()) {
        TerrainSettings s;
        s.chunkQuads = choice;
        EXPECT_EQ(Sanitize(s).chunkQuads, choice);
    }
}

TEST(TerrainSettingsTableTest, GetAndSetRoundTripEveryKind)
{
    TerrainSettings s;
    for (const FieldDesc& field : SettingFields()) {
        SCOPED_TRACE(field.name);
        const double mid = field.kind == FieldKind::Bool ? 1.0 : (field.kind == FieldKind::Choice ? 32.0 : 0.5 * (field.min + field.max));
        SetFieldValue(s, field, mid);
        const double back = GetFieldValue(s, field);
        if (field.kind == FieldKind::Float) {
            EXPECT_NEAR(back, mid, std::abs(mid) * 1e-6 + 1e-9);
        } else if (field.kind == FieldKind::Seed || field.kind == FieldKind::Int || field.kind == FieldKind::Choice) {
            EXPECT_NEAR(back, std::round(mid), 0.51);
        } else {
            EXPECT_EQ(back, 1.0);
        }
    }
    SetFieldValue(s, SettingFields()[0], std::nan(""));
    EXPECT_NE(GetFieldValue(s, SettingFields()[0]), std::numeric_limits<double>::infinity());
}

TEST(TerrainSettingsTableTest, EveryGroupHasFields)
{
    for (const char* group : SettingGroups()) {
        int count = 0;
        for (const FieldDesc& field : SettingFields()) {
            count += std::strcmp(field.group, group) == 0 ? 1 : 0;
        }
        EXPECT_GT(count, 0) << group;
    }
}

// --- Apply policy -------------------------------------------------------------------------------------------

namespace {

EditState State(SettingsChange pending, bool dragging, bool preview, double idle)
{
    EditState s;
    s.pending = pending;
    s.dragging = dragging;
    s.livePreview = preview;
    s.secondsSinceEdit = idle;
    return s;
}

} // namespace

TEST(TerrainApplyPolicyTest, NothingPendingNeverApplies)
{
    EXPECT_FALSE(ShouldApplyNow(State(SettingsChange::None, false, false, 10.0)));
}

TEST(TerrainApplyPolicyTest, CheapChangesApplyWhileDragging)
{
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Material, true, false, 0.0)));
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Optimization, true, false, 0.0)));
}

TEST(TerrainApplyPolicyTest, HeightChangeUpdatesThePreviewEveryFrame)
{
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Height, true, true, 0.0)));
}

TEST(TerrainApplyPolicyTest, HeightChangeWaitsForReleaseOrRestWithoutPreview)
{
    EXPECT_FALSE(ShouldApplyNow(State(SettingsChange::Height, true, false, 0.0)));
    EXPECT_FALSE(ShouldApplyNow(State(SettingsChange::Height, true, false, 0.2)));
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Height, true, false, 0.5)));
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Height, false, false, 0.0)));
}

TEST(TerrainApplyPolicyTest, GeometryChangeOnlyOnRelease)
{
    const SettingsChange both = SettingsChange::Geometry | SettingsChange::Height;
    EXPECT_FALSE(ShouldApplyNow(State(both, true, false, 5.0)));
    EXPECT_FALSE(ShouldApplyNow(State(both, true, true, 5.0)));
    EXPECT_TRUE(ShouldApplyNow(State(both, false, false, 0.0)));
}

TEST(TerrainApplyPolicyTest, MixedChangesFollowTheMostExpensivePart)
{
    EXPECT_FALSE(ShouldApplyNow(State(SettingsChange::Height | SettingsChange::Material, true, false, 0.0)));
    EXPECT_TRUE(ShouldApplyNow(State(SettingsChange::Height | SettingsChange::Material, true, true, 0.0)));
}

} // namespace gte::terrain
