// Tier 1: signature trimming for GTE_PROFILE_FUNCTION names.
#include "Profiling/FunctionName.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace gte::Profiling {
namespace {

std::string Trim(std::string_view signature)
{
    std::array<char, 256> out{};
    const std::size_t length = TrimFunctionName(signature, out);
    EXPECT_EQ(length, std::string_view(out.data()).size());
    return std::string(out.data(), length);
}

TEST(FunctionNameTest, TrimsCompilerSignatures)
{
    struct Case {
        const char* signature;
        const char* expected;
    };
    const Case cases[] = {
        { "void sample::Widget::Update()", "sample::Widget::Update" },
        { "void sample::Widget::Update() const", "sample::Widget::Update" },
        { "virtual void sample::Widget::Update()", "sample::Widget::Update" },
        { "static int sample::Widget::Count()", "sample::Widget::Count" },
        { "std::vector<int> sample::Make(int)", "sample::Make" },
        { "std::pair<int, int> sample::Pair()", "sample::Pair" },
        { "void sample::Widget::Tmpl(T) [with T = int]", "sample::Widget::Tmpl" },
        { "void sample::Box<T>::Run(T) [with T = int]", "sample::Box<T>::Run" },
        { "sample::Widget::Widget()", "sample::Widget::Widget" },
        { "sample::Widget::~Widget()", "sample::Widget::~Widget" },
        { "bool sample::Widget::operator==(const sample::Widget&) const", "sample::Widget::operator==" },
        { "void sample::Widget::operator()(int) const", "sample::Widget::operator()" },
        { "void {anonymous}::Helper()", "{anonymous}::Helper" },
        { "int main()", "main" },
        { "main()::<lambda()>", "main::<lambda>" },
        { "void sample::Widget::Run()::<lambda(int)>", "sample::Widget::Run::<lambda>" },
        { "sample::Free()::<lambda(int)>", "sample::Free::<lambda>" },
        { "", "" },
    };
    for (const Case& c : cases) {
        EXPECT_EQ(Trim(c.signature), c.expected) << c.signature;
    }
}

TEST(FunctionNameTest, CutsLongNamesAndAlwaysTerminates)
{
    const std::string longName(400, 'a');
    const std::string trimmed = Trim(longName);
    EXPECT_EQ(trimmed.size(), 255u);

    std::array<char, 4> tiny{};
    EXPECT_EQ(TrimFunctionName("void sample::Widget::Update()", tiny), 3u);
    EXPECT_STREQ(tiny.data(), "sam");
    EXPECT_EQ(TrimFunctionName("x", std::span<char>()), 0u);
}

} // namespace
} // namespace gte::Profiling
