#include "Core/Plugins/ProjectAssemblyNameValidation.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

bool IsValid(const std::string& name)
{
    std::string ignored;
    return IsValidProjectAssemblyIdentifierName(name, ignored);
}

} // namespace

TEST(ProjectAssemblyNameValidationTest, RejectsEmptyString) { EXPECT_FALSE(IsValid("")); }
TEST(ProjectAssemblyNameValidationTest, RejectsLeadingDigit) { EXPECT_FALSE(IsValid("1Project")); }
TEST(ProjectAssemblyNameValidationTest, RejectsEmbeddedSpace) { EXPECT_FALSE(IsValid("My Project")); }
TEST(ProjectAssemblyNameValidationTest, RejectsDotDot) { EXPECT_FALSE(IsValid("../evil")); }
TEST(ProjectAssemblyNameValidationTest, RejectsForwardSlash) { EXPECT_FALSE(IsValid("a/b")); }
TEST(ProjectAssemblyNameValidationTest, RejectsBackslash) { EXPECT_FALSE(IsValid("a\\b")); }

TEST(ProjectAssemblyNameValidationTest, RejectsReservedDeviceNamesExactCase)
{
    EXPECT_FALSE(IsValid("CON"));
    EXPECT_FALSE(IsValid("PRN"));
    EXPECT_FALSE(IsValid("AUX"));
    EXPECT_FALSE(IsValid("NUL"));
    EXPECT_FALSE(IsValid("COM1"));
    EXPECT_FALSE(IsValid("COM9"));
    EXPECT_FALSE(IsValid("LPT1"));
    EXPECT_FALSE(IsValid("LPT9"));
}

TEST(ProjectAssemblyNameValidationTest, RejectsReservedDeviceNamesMixedCase)
{
    EXPECT_FALSE(IsValid("con"));
    EXPECT_FALSE(IsValid("CoN"));
    EXPECT_FALSE(IsValid("com3"));
}

TEST(ProjectAssemblyNameValidationTest, AcceptsGenuinelyValidNames)
{
    EXPECT_TRUE(IsValid("MyGame"));
    EXPECT_TRUE(IsValid("_LeadingUnderscore"));
    EXPECT_TRUE(IsValid("_"));
    EXPECT_TRUE(IsValid("Project_Assembly_2"));
    EXPECT_TRUE(IsValid("ProjectAssemblyProbe")); // the one real, existing project name - must never regress.
}

TEST(ProjectAssemblyNameValidationTest, ErrorMessageIsPopulatedOnlyOnFailure)
{
    std::string message = "sentinel-must-be-cleared-by-caller-not-this-function";
    EXPECT_TRUE(IsValidProjectAssemblyIdentifierName("ValidName", message));
    EXPECT_EQ(message, "sentinel-must-be-cleared-by-caller-not-this-function"); // untouched on success.

    std::string failureMessage;
    EXPECT_FALSE(IsValidProjectAssemblyIdentifierName("", failureMessage));
    EXPECT_FALSE(failureMessage.empty());
}

} // namespace gte
