// Tier 1: call-site origin tags of interned scope names.
#include "Profiling/NameTable.h"
#include "Profiling/ScopeSource.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

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

TEST(ScopeSourceTest, EngineRootDecidesInsideTheCheckout)
{
    // A checkout that itself sits in a "Projects" folder must not turn engine code into project code.
    constexpr std::string_view root = "C:/Users/x/Projects/GreatTamanaEngine/";
    EXPECT_EQ(OriginOfPath("C:/Users/x/Projects/GreatTamanaEngine/src/Core/Core.cpp", root), ScopeOrigin::Engine);
    EXPECT_EQ(OriginOfPath("C:\\Users\\x\\Projects\\GreatTamanaEngine\\src\\Core\\Core.cpp", root), ScopeOrigin::Engine);
    EXPECT_EQ(OriginOfPath("C:/Users/x/Projects/GreatTamanaEngine/Projects/Foo/a.cpp", root), ScopeOrigin::Project);
    EXPECT_EQ(OriginOfPath("C:/Users/x/Projects/GreatTamanaEngine/third_party/b.cpp", root), ScopeOrigin::Unknown);
    EXPECT_EQ(OriginOfPath("D:/other/Projects/Foo/a.cpp", root), ScopeOrigin::Project);
    EXPECT_EQ(OriginOfPath("D:/other/src/a.cpp", root), ScopeOrigin::Engine);
    EXPECT_EQ(OriginOfPath("C:/Users/x/Projects/Foo/src/a.cpp", {}), ScopeOrigin::Project);
}

TEST(ScopeSourceTest, RealEngineRootTagsEngineSources)
{
    const std::string_view root = EngineRoot();
    if (root.empty()) {
        GTEST_SKIP() << "engine root is not derivable from this build's source paths";
    }
    const std::string engine = std::string(root) + "src/Core/Core.cpp";
    const std::string project = std::string(root) + "Projects/Foo/a.cpp";
    EXPECT_EQ(OriginOfPath(engine, root), ScopeOrigin::Engine);
    EXPECT_EQ(OriginOfPath(project, root), ScopeOrigin::Project);

    const NameId id = InternNameAt("ScopeSourceTest.RealRoot", engine.c_str(), 3);
    EXPECT_EQ(SourceOf(id).origin, ScopeOrigin::Engine);
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
