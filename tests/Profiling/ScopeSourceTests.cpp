// Tier 1: call-site origin tags of interned scope names.
#include "Profiling/NameTable.h"
#include "Profiling/ScopeSource.h"

#include <gtest/gtest.h>

namespace gte::Profiling {
namespace {

TEST(ScopeSourceTest, OriginComesFromThePath)
{
    const NameId project = InternNameAt("ScopeSourceTest.Project", "C:/x/Projects/Foo/Assets/a.cpp", 12);
    const NameId engine = InternNameAt("ScopeSourceTest.Engine", "C:/x/src/Core/Core.cpp", 34);
    const NameId unknown = InternNameAt("ScopeSourceTest.Unknown", "a.cpp", 5);
    const NameId backslash = InternNameAt("ScopeSourceTest.Backslash", "C:\\x\\Projects\\Foo\\b.cpp", 6);

    EXPECT_EQ(SourceOf(project).origin, ScopeOrigin::Project);
    EXPECT_EQ(SourceOf(project).line, 12u);
    EXPECT_STREQ(ResolveName(SourceOf(project).fileName), "a.cpp");
    EXPECT_EQ(SourceOf(engine).origin, ScopeOrigin::Engine);
    EXPECT_STREQ(ResolveName(SourceOf(engine).fileName), "Core.cpp");
    EXPECT_EQ(SourceOf(unknown).origin, ScopeOrigin::Unknown);
    EXPECT_EQ(SourceOf(backslash).origin, ScopeOrigin::Project);
    EXPECT_STREQ(ResolveName(SourceOf(backslash).fileName), "b.cpp");
}

TEST(ScopeSourceTest, ProjectPathBeatsEnginePath)
{
    const NameId id = InternNameAt("ScopeSourceTest.Both", "C:/x/src/Projects/Foo/a.cpp", 1);
    EXPECT_EQ(SourceOf(id).origin, ScopeOrigin::Project);
}

TEST(ScopeSourceTest, FirstSiteWins)
{
    const NameId first = InternNameAt("ScopeSourceTest.Twice", "C:/x/src/A.cpp", 10);
    const NameId second = InternNameAt("ScopeSourceTest.Twice", "C:/x/Projects/B.cpp", 20);

    EXPECT_EQ(first, second);
    EXPECT_EQ(SourceOf(first).line, 10u);
    EXPECT_EQ(SourceOf(first).origin, ScopeOrigin::Engine);
}

TEST(ScopeSourceTest, UnknownIdsHaveNoSource)
{
    for (const NameId id : { kInvalidNameId, kOverflowNameId, NameId{ 4000000 } }) {
        const ScopeSource source = SourceOf(id);
        EXPECT_EQ(source.fileName, kInvalidNameId);
        EXPECT_EQ(source.line, 0u);
        EXPECT_EQ(source.origin, ScopeOrigin::Unknown);
    }
}

TEST(ScopeSourceTest, FunctionNamesAreTrimmed)
{
    const NameId id = InternFunctionName("void sample::Widget::Update() const", "C:/x/src/W.cpp", 7);
    EXPECT_STREQ(ResolveName(id), "sample::Widget::Update");
    EXPECT_EQ(SourceOf(id).line, 7u);

    const NameId empty = InternFunctionName("", "C:/x/src/W.cpp", 8);
    EXPECT_STREQ(ResolveName(empty), "<unknown function>");
}

} // namespace
} // namespace gte::Profiling
