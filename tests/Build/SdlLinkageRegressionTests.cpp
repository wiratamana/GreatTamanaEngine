// tests/Build/SdlLinkageRegressionTests.cpp
//
// PHASE1 (task_manager/editor-core-separation-1/
// PHASE1_SDL_DEPENDENCY_REGRESSION_BASELINE.md) of the editor-core-
// separation-1 campaign - a DELIBERATE "before" snapshot, not a normal
// regression test for engine behavior.
//
// Captures today's known-bad fact, already documented as a plain prose
// comment in tests/CMakeLists.txt, as a real, mechanically-checkable CTest
// case: this test BINARY (GreatTamanaEngineTests.exe) still dynamically
// depends on SDL3.dll at process load time - gte_core's Window.cpp
// references real SDL symbols (SDL_CreateWindow et al.), even though not a
// single Tier-1 test in this suite ever calls SDL_Init() or opens a window -
// so the OS loader's own import table for THIS PROCESS'S OWN .exe still
// unconditionally lists SDL3.dll.
//
// --- Mechanism chosen, and why -----------------------------------------
//
// The phase's own strategy document offered three options: (1) `dumpbin
// /dependents` (MSVC-only - this project's actual toolchain is Ninja/MinGW,
// so this isn't reliably available), (2) copy the built exe into an
// SDL3.dll-less directory and see if the OS fails to LAUNCH it at all, or
// (3) parse the PE import directory directly. Option (2) was tried first and
// abandoned after live investigation during this phase's own execution: on
// Windows, a missing hard DLL dependency pops a real, BLOCKING, modal
// "the code execution cannot proceed because SDL3.dll was not found" system
// dialog (confirmed empirically - screenshot evidence during this phase's own
// manual probe) - even with SetErrorMode(SEM_FAILCRITICALERRORS |
// SEM_NOGPFAULTERRORBOX) set on the parent process, plus a freshly-compiled
// throwaway probe .exe tripped this environment's antivirus real-time
// scanning, causing further unpredictable hangs. Spawning child processes
// (with all the OS-dialog/antivirus/hang surface area that implies) is
// exactly the kind of fragile, hard-to-debug mechanism this test suite
// otherwise never relies on - so this file uses option (3) instead: a pure,
// self-contained, Tier-1-testable PE (Portable Executable) import-directory
// parser (ListImportedDllNames() below) that just reads this exe's own bytes
// off disk and walks its COFF/PE header structures by hand (mirroring this
// test suite's own established "hand-parse a binary format" convention -
// see Assets/GtaFileTests.cpp/PmxLoaderTests.cpp) - zero process spawning,
// zero OS dialogs, zero antivirus interaction, 100% deterministic.
//
// Phase 14 (WINDOW_SDL_RELOCATION_TO_EDITOR) is expected to flip this exact
// test's own expectation (or add a sibling test asserting the opposite) once
// Window.cpp/SdlContext move out of gte_core and gte_core drops SDL3::SDL3
// at link time entirely - at that point, SDL3.dll should no longer appear in
// GreatTamanaEngineTests.exe's own import table at all.

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace gte {
namespace {

// --- Minimal, hand-rolled PE/COFF structures ----------------------------
//
// Deliberately NOT reusing <winnt.h>'s IMAGE_* structures (even though this
// project already includes <windows.h> for GetModuleFileNameW below) - these
// are redefined here, packed explicitly, as plain, pure, self-contained POD
// types so ListImportedDllNames() itself stays a genuinely pure function
// over a byte buffer, testable with a hand-built fixture with no OS
// involvement at all, the same "construct the exact binary format by hand"
// discipline as Assets/GtaFileTests.cpp/PmxLoaderTests.cpp. Only the 64-bit
// (PE32+) Optional Header shape is supported - the only variant this
// project's own x86_64 (MinGW-w64) toolchain ever produces.
#pragma pack(push, 1)
struct PeDosHeader
{
    std::uint16_t e_magic;
    std::uint8_t  reserved[58];
    std::int32_t  e_lfanew;
};

struct PeFileHeader
{
    std::uint16_t machine;
    std::uint16_t numberOfSections;
    std::uint32_t timeDateStamp;
    std::uint32_t pointerToSymbolTable;
    std::uint32_t numberOfSymbols;
    std::uint16_t sizeOfOptionalHeader;
    std::uint16_t characteristics;
};

struct PeDataDirectory
{
    std::uint32_t virtualAddress;
    std::uint32_t size;
};

struct PeOptionalHeader64
{
    std::uint16_t magic;
    std::uint8_t  majorLinkerVersion;
    std::uint8_t  minorLinkerVersion;
    std::uint32_t sizeOfCode;
    std::uint32_t sizeOfInitializedData;
    std::uint32_t sizeOfUninitializedData;
    std::uint32_t addressOfEntryPoint;
    std::uint32_t baseOfCode;
    std::uint64_t imageBase;
    std::uint32_t sectionAlignment;
    std::uint32_t fileAlignment;
    std::uint16_t majorOperatingSystemVersion;
    std::uint16_t minorOperatingSystemVersion;
    std::uint16_t majorImageVersion;
    std::uint16_t minorImageVersion;
    std::uint16_t majorSubsystemVersion;
    std::uint16_t minorSubsystemVersion;
    std::uint32_t win32VersionValue;
    std::uint32_t sizeOfImage;
    std::uint32_t sizeOfHeaders;
    std::uint32_t checkSum;
    std::uint16_t subsystem;
    std::uint16_t dllCharacteristics;
    std::uint64_t sizeOfStackReserve;
    std::uint64_t sizeOfStackCommit;
    std::uint64_t sizeOfHeapReserve;
    std::uint64_t sizeOfHeapCommit;
    std::uint32_t loaderFlags;
    std::uint32_t numberOfRvaAndSizes;
    PeDataDirectory dataDirectories[16];
};

struct PeSectionHeader
{
    char          name[8];
    std::uint32_t virtualSize;
    std::uint32_t virtualAddress;
    std::uint32_t sizeOfRawData;
    std::uint32_t pointerToRawData;
    std::uint32_t pointerToRelocations;
    std::uint32_t pointerToLineNumbers;
    std::uint16_t numberOfRelocations;
    std::uint16_t numberOfLineNumbers;
    std::uint32_t characteristics;
};

struct PeImportDescriptor
{
    std::uint32_t originalFirstThunk;
    std::uint32_t timeDateStamp;
    std::uint32_t forwarderChain;
    std::uint32_t name;
    std::uint32_t firstThunk;
};
#pragma pack(pop)

constexpr std::uint16_t kDosSignature = 0x5A4D;      // "MZ"
constexpr std::uint32_t kPeSignature = 0x00004550;   // "PE\0\0"
constexpr std::uint16_t kPe32PlusMagic = 0x20B;
constexpr std::size_t kImportDirectoryIndex = 1;

std::optional<std::uint32_t> RvaToFileOffset(const std::vector<PeSectionHeader>& sections, std::uint32_t rva)
{
    for (const PeSectionHeader& section : sections) {
        const std::uint32_t sectionSize = section.virtualSize != 0 ? section.virtualSize : section.sizeOfRawData;
        if (rva >= section.virtualAddress && rva < section.virtualAddress + sectionSize) {
            return rva - section.virtualAddress + section.pointerToRawData;
        }
    }
    return std::nullopt;
}

// Pure function: walks a raw PE/COFF image's headers (from an in-memory byte
// buffer - never a live loaded module, never touches the OS loader) and
// returns every DLL name listed in its Import Directory Table. Returns an
// empty vector for anything malformed/unrecognized (never throws/asserts) -
// callers must treat an empty result as "could not determine", not as
// "imports nothing".
std::vector<std::string> ListImportedDllNames(const std::vector<std::uint8_t>& bytes)
{
    std::vector<std::string> result;

    if (bytes.size() < sizeof(PeDosHeader)) {
        return result;
    }
    PeDosHeader dos{};
    std::memcpy(&dos, bytes.data(), sizeof(PeDosHeader));
    if (dos.e_magic != kDosSignature || dos.e_lfanew < 0) {
        return result;
    }

    std::size_t offset = static_cast<std::size_t>(dos.e_lfanew);
    if (offset + 4 + sizeof(PeFileHeader) > bytes.size()) {
        return result;
    }

    std::uint32_t peSignature = 0;
    std::memcpy(&peSignature, bytes.data() + offset, 4);
    if (peSignature != kPeSignature) {
        return result;
    }
    offset += 4;

    PeFileHeader fileHeader{};
    std::memcpy(&fileHeader, bytes.data() + offset, sizeof(PeFileHeader));
    offset += sizeof(PeFileHeader);

    if (offset + sizeof(PeOptionalHeader64) > bytes.size()) {
        return result;
    }
    PeOptionalHeader64 optionalHeader{};
    std::memcpy(&optionalHeader, bytes.data() + offset, sizeof(PeOptionalHeader64));
    if (optionalHeader.magic != kPe32PlusMagic) {
        return result; // Only PE32+ (x64) is supported - this project's own toolchain is always x86_64.
    }

    const std::size_t sectionHeadersOffset = offset + fileHeader.sizeOfOptionalHeader;
    const std::size_t sectionsByteSize = static_cast<std::size_t>(fileHeader.numberOfSections) * sizeof(PeSectionHeader);
    if (sectionHeadersOffset + sectionsByteSize > bytes.size()) {
        return result;
    }

    std::vector<PeSectionHeader> sections(fileHeader.numberOfSections);
    if (fileHeader.numberOfSections > 0) {
        std::memcpy(sections.data(), bytes.data() + sectionHeadersOffset, sectionsByteSize);
    }

    const PeDataDirectory& importDirectory = optionalHeader.dataDirectories[kImportDirectoryIndex];
    if (importDirectory.virtualAddress == 0 || importDirectory.size == 0) {
        return result; // No imports at all (or couldn't be resolved) - a valid, if unusual, empty result.
    }

    const std::optional<std::uint32_t> importTableOffset = RvaToFileOffset(sections, importDirectory.virtualAddress);
    if (!importTableOffset) {
        return result;
    }

    std::size_t descriptorOffset = *importTableOffset;
    while (descriptorOffset + sizeof(PeImportDescriptor) <= bytes.size()) {
        PeImportDescriptor descriptor{};
        std::memcpy(&descriptor, bytes.data() + descriptorOffset, sizeof(PeImportDescriptor));
        if (descriptor.name == 0 && descriptor.originalFirstThunk == 0 && descriptor.firstThunk == 0) {
            break; // The all-zero descriptor marks the end of the table.
        }

        const std::optional<std::uint32_t> nameOffset = RvaToFileOffset(sections, descriptor.name);
        if (nameOffset && *nameOffset < bytes.size()) {
            const char* namePtr = reinterpret_cast<const char*>(bytes.data() + *nameOffset);
            const std::size_t maxLen = bytes.size() - *nameOffset;
            const std::size_t len = strnlen(namePtr, maxLen);
            result.emplace_back(namePtr, len);
        }
        descriptorOffset += sizeof(PeImportDescriptor);
    }

    return result;
}

bool ContainsDllNameCaseInsensitive(const std::vector<std::string>& names, std::string_view target)
{
    for (const std::string& name : names) {
        if (name.size() != target.size()) {
            continue;
        }
        bool equal = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(target[i]))) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

// Resolves the path of the CURRENTLY RUNNING process's own .exe on disk -
// this is GreatTamanaEngineTests.exe itself, wherever the build/ctest
// invocation actually placed it. A plain, single Win32 API call - no process
// creation, no OS dialog risk, no antivirus interaction.
std::filesystem::path CurrentExecutablePath()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer);
}

std::vector<std::uint8_t> ReadWholeFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

// --- Pure-function correctness: ListImportedDllNames() itself --------------
//
// A minimal, hand-built PE32+ image (DOS header + NT headers + one section
// containing one import descriptor naming "TEST.dll" + its null terminator
// descriptor) - proves the parser itself is correct against a fixture with
// zero OS/real-file involvement, before trusting it against the real,
// already-built test binary below.
TEST(ListImportedDllNamesTest, FindsANamedDllInAHandBuiltMinimalPe32PlusImage)
{
    constexpr std::uint32_t kSectionRva = 0x1000;
    constexpr std::uint32_t kFileAlignment = 0x200;

    // Section layout inside our fake ".idata"-like section, all RVA-relative:
    //   [0]      import descriptor #0 (points at nameRva below)
    //   [20]     import descriptor #1 (all-zero terminator)
    //   [40]     the null-terminated ASCII string "TEST.dll"
    constexpr std::uint32_t kDescriptorTableRva = kSectionRva;
    constexpr std::uint32_t kNameRva = kSectionRva + 40;

    std::vector<std::uint8_t> bytes;
    bytes.resize(kFileAlignment * 4, 0);

    PeDosHeader dos{};
    dos.e_magic = kDosSignature;
    dos.e_lfanew = static_cast<std::int32_t>(kFileAlignment); // NT headers start at the 2nd "page".
    std::memcpy(bytes.data(), &dos, sizeof(dos));

    std::size_t offset = kFileAlignment;
    std::uint32_t peSig = kPeSignature;
    std::memcpy(bytes.data() + offset, &peSig, sizeof(peSig));
    offset += sizeof(peSig);

    PeFileHeader fileHeader{};
    fileHeader.numberOfSections = 1;
    fileHeader.sizeOfOptionalHeader = sizeof(PeOptionalHeader64);
    std::memcpy(bytes.data() + offset, &fileHeader, sizeof(fileHeader));
    offset += sizeof(fileHeader);

    PeOptionalHeader64 optionalHeader{};
    optionalHeader.magic = kPe32PlusMagic;
    optionalHeader.numberOfRvaAndSizes = 16;
    optionalHeader.dataDirectories[kImportDirectoryIndex].virtualAddress = kDescriptorTableRva;
    optionalHeader.dataDirectories[kImportDirectoryIndex].size = sizeof(PeImportDescriptor) * 2;
    std::memcpy(bytes.data() + offset, &optionalHeader, sizeof(optionalHeader));
    offset += sizeof(optionalHeader);

    PeSectionHeader section{};
    std::memcpy(section.name, ".idata\0\0", 8);
    section.virtualAddress = kSectionRva;
    section.virtualSize = 0x100;
    section.sizeOfRawData = kFileAlignment;
    section.pointerToRawData = kFileAlignment * 2; // 3rd "page" in our fake buffer.
    std::memcpy(bytes.data() + offset, &section, sizeof(section));

    // Real descriptor naming "TEST.dll", followed by the required all-zero
    // terminator descriptor.
    PeImportDescriptor realDescriptor{};
    realDescriptor.name = kNameRva;
    PeImportDescriptor terminatorDescriptor{};

    const std::size_t sectionFileOffset = section.pointerToRawData;
    std::memcpy(bytes.data() + sectionFileOffset, &realDescriptor, sizeof(realDescriptor));
    std::memcpy(bytes.data() + sectionFileOffset + sizeof(PeImportDescriptor), &terminatorDescriptor, sizeof(terminatorDescriptor));

    const char dllName[] = "TEST.dll";
    std::memcpy(bytes.data() + sectionFileOffset + 40, dllName, sizeof(dllName)); // Includes the trailing '\0'.

    const std::vector<std::string> imports = ListImportedDllNames(bytes);
    ASSERT_EQ(imports.size(), 1u);
    EXPECT_EQ(imports[0], "TEST.dll");
}

TEST(ListImportedDllNamesTest, ReturnsEmptyForATooShortBuffer)
{
    const std::vector<std::uint8_t> tooShort{0x4D, 0x5A}; // "MZ" alone - not remotely a full PE image.
    EXPECT_TRUE(ListImportedDllNames(tooShort).empty());
}

TEST(ListImportedDllNamesTest, ReturnsEmptyForAnEmptyBuffer)
{
    EXPECT_TRUE(ListImportedDllNames({}).empty());
}

// --- The real "before" snapshot: today's actual GreatTamanaEngineTests.exe -

// A DELIBERATE "before" snapshot - see this file's own header comment.
// Currently PASSES because SDL3.dll IS genuinely listed in this test
// binary's own import table today. Phase 14 must revisit this exact
// assertion once gte_core drops its SDL3 dependency.
TEST(SdlLinkageRegressionTest, SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll)
{
    const std::filesystem::path exePath = CurrentExecutablePath();
    ASSERT_FALSE(exePath.empty()) << "Could not resolve this test binary's own module path via GetModuleFileNameW().";

    std::error_code existsError;
    ASSERT_TRUE(std::filesystem::exists(exePath, existsError))
        << "Resolved module path does not exist on disk: " << exePath;

    const std::vector<std::uint8_t> exeBytes = ReadWholeFile(exePath);
    ASSERT_FALSE(exeBytes.empty()) << "Failed to read this test binary's own bytes from: " << exePath;

    const std::vector<std::string> imports = ListImportedDllNames(exeBytes);
    ASSERT_FALSE(imports.empty())
        << "PE import table parsing produced zero entries for the real test binary - "
           "the parser itself is likely broken (a real Windows exe always imports at "
           "least kernel32.dll), not that the binary genuinely has zero imports.";

    EXPECT_TRUE(ContainsDllNameCaseInsensitive(imports, "SDL3.dll"))
        << "Expected GreatTamanaEngineTests.exe's own PE import table to list SDL3.dll "
           "(today's known-bad state - gte_core's Window.cpp references real SDL "
           "symbols even though no Tier-1 test ever opens a window), but it was not "
           "found. If gte_core no longer needs SDL3.dll, this is actually GOOD NEWS - "
           "it means Phase 14's fix already landed; update this test's expectation "
           "accordingly (see this file's own header comment).";
}

} // namespace gte
