#include "AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

namespace gte {

namespace {

// Same UTF-8 <-> std::filesystem::path convention ProjectPanelData.h uses
// (see its own PathToUtf8()/Utf8ToPath() comment) - duplicated locally
// rather than depending on it, since this module (src/Assets/) must stay
// available with GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL both OFF (it's
// an engine-level concept, not an Editor one).
std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string ToLowerAscii(const std::string& s)
{
    std::string result = s;
    std::transform(
        result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // namespace

void AssetDatabase::Clear()
{
    m_assets.clear();
    m_guidToIndex.clear();
    m_pathToIndex.clear();
}

void AssetDatabase::UpsertRecord(AssetRecord record)
{
    const auto guidIt = m_guidToIndex.find(record.guid);
    if (guidIt != m_guidToIndex.end()) {
        const std::size_t index = guidIt->second;
        const std::string oldPath = m_assets[index].gtaPath;
        if (oldPath != record.gtaPath) {
            m_pathToIndex.erase(oldPath);
            m_pathToIndex[record.gtaPath] = index;
        }
        m_assets[index] = std::move(record);
        return;
    }

    const std::size_t index = m_assets.size();
    m_pathToIndex[record.gtaPath] = index;
    m_guidToIndex.emplace(record.guid, index);
    m_assets.push_back(std::move(record));
}

std::size_t AssetDatabase::RefreshFromDirectory(const std::filesystem::path& rootDirectory)
{
    Clear();

    std::error_code rootEc;
    if (!std::filesystem::is_directory(rootDirectory, rootEc) || rootEc) {
        return 0;
    }

    std::error_code iterEc;
    std::filesystem::recursive_directory_iterator it(
        rootDirectory, std::filesystem::directory_options::skip_permission_denied, iterEc);
    if (iterEc) {
        return 0;
    }

    const std::filesystem::recursive_directory_iterator end;
    while (it != end) {
        std::error_code entryEc;
        const std::filesystem::directory_entry entry = *it;

        const bool isFile = entry.is_regular_file(entryEc);
        if (!entryEc && isFile && ToLowerAscii(PathToUtf8(entry.path().extension())) == ".gta") {
            const std::optional<GtaHeader> header = ReadGtaHeader(entry.path());
            if (header.has_value() && m_guidToIndex.find(header->Id()) == m_guidToIndex.end()) {
                AssetRecord record;
                record.guid = header->Id();
                record.type = header->Type();
                record.flags = header->Flags();
                record.version = header->version;

                std::error_code absEc;
                record.gtaPath = PathToUtf8(std::filesystem::absolute(entry.path(), absEc));

                std::error_code sizeEc;
                const std::uintmax_t size = entry.file_size(sizeEc);
                record.fileSizeBytes = sizeEc ? 0 : size;

                UpsertRecord(std::move(record));
            }
            // A *.gta with a bad/unreadable header is silently skipped -
            // not fatal to the rest of the scan.
        }

        it.increment(entryEc);
        if (entryEc) {
            // The tree was modified out from under this iteration - stop
            // here and return whatever was already collected, same
            // tolerant behavior as ProjectPanelData::ScanChildren().
            break;
        }
    }

    return m_assets.size();
}

std::optional<Guid> AssetDatabase::ImportAsset(const std::filesystem::path& destinationGtaPath, AssetType type,
    const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload, AssetFlags flags)
{
    Guid guid = Guid::Generate();

    // Reuse an existing asset's Guid when overwriting it in place, so any
    // existing scene cross-reference to it survives a re-import.
    if (const std::optional<GtaHeader> existing = ReadGtaHeader(destinationGtaPath); existing.has_value()) {
        guid = existing->Id();
    }

    if (!WriteGtaFile(destinationGtaPath, type, guid, flags, metadata, payload)) {
        return std::nullopt;
    }

    AssetRecord record;
    record.guid = guid;
    record.type = type;
    record.flags = flags;
    record.version = kGtaCurrentVersion;

    std::error_code absEc;
    record.gtaPath = PathToUtf8(std::filesystem::absolute(destinationGtaPath, absEc));

    std::error_code sizeEc;
    const std::uintmax_t size = std::filesystem::file_size(destinationGtaPath, sizeEc);
    record.fileSizeBytes = sizeEc ? 0 : size;

    UpsertRecord(std::move(record));
    return guid;
}

std::optional<Guid> AssetDatabase::ImportRawFile(const std::filesystem::path& sourceFilePath,
    const std::filesystem::path& destinationGtaPath, AssetType type, AssetFlags flags)
{
    std::ifstream in(sourceFilePath, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> payload(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    return ImportAsset(destinationGtaPath, type, std::vector<std::uint8_t>{}, payload, flags);
}

const AssetRecord* AssetDatabase::FindByGuid(const Guid& guid) const
{
    const auto it = m_guidToIndex.find(guid);
    if (it == m_guidToIndex.end()) {
        return nullptr;
    }
    return &m_assets[it->second];
}

const AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& gtaPath) const
{
    std::error_code absEc;
    const std::string key = PathToUtf8(std::filesystem::absolute(gtaPath, absEc));
    const auto it = m_pathToIndex.find(key);
    if (it == m_pathToIndex.end()) {
        return nullptr;
    }
    return &m_assets[it->second];
}

std::vector<AssetRecord> AssetDatabase::GetAssetsOfType(AssetType type) const
{
    std::vector<AssetRecord> result;
    for (const AssetRecord& record : m_assets) {
        if (record.type == type) {
            result.push_back(record);
        }
    }
    return result;
}

} // namespace gte
