#pragma once

#include "Core/Model.h"
#include "Core/Result.h"

#include <cstdint>

namespace Bunker::Vault
{
    std::uint64_t SystemUtcNow();

    std::uint64_t SystemUtcMillis();

    inline constexpr std::size_t kMaxTitleBytes = 200;
    inline constexpr std::size_t kMaxPlatformIdBytes = 64;
    inline constexpr std::size_t kMaxSymbolBytes = 64;
    inline constexpr std::size_t kMaxFieldKeyBytes = 100;
    inline constexpr std::size_t kMaxFieldValueBytes = 8192;
    static_assert(kMaxFieldValueBytes + 1 <=
                      SecureArena::kClasses[SecureArena::kClassCount - 1],
                  "the longest field value must fit the largest arena class");
    inline constexpr std::size_t kMaxFieldsPerEntry = 64;
    inline constexpr std::size_t kMaxFolderNameBytes = 100;
    inline constexpr std::size_t kMaxEntries = 10000;

    inline constexpr std::size_t kMinTotpSecretBytes = 10;

    inline constexpr std::size_t kMaxTotpSecretBytes = 512;

    static_assert(kMaxTotpSecretBytes * 5 / 8 <= kMaxTotpKeyBytes,
                  "the longest accepted secret must decode into the stack buffer");

    inline constexpr std::size_t kMaxRecoveryCodesPerEntry = 64;
    inline constexpr std::size_t kMaxRecoveryCodeBytes = 64;

    inline constexpr std::size_t kMaxListItemsPerEntry = 1000;
    inline constexpr std::size_t kMaxListLabelBytes = 200;
    inline constexpr std::size_t kMaxListSecretBytes = 256;
    static_assert(kMaxListSecretBytes + 1 <=
                      SecureArena::kClasses[SecureArena::kClassCount - 1],
                  "the longest list value must fit the largest arena class");

    void Normalise(Entry& entry);

    Status ValidateTotp(const TotpConfig& config);

    Status ValidateFolderName(const std::string& name);

    struct DraftField
    {
        const char* Key = nullptr;
        std::size_t ValueBytes = 0;
        bool HasRole = false;
    };

    struct DraftShape
    {
        const char* Title = nullptr;
        const char* PlatformId = nullptr;
        const char* Folder = nullptr;
        const DraftField* Fields = nullptr;
        std::size_t FieldCount = 0;
    };

    Status ValidateShape(const DraftShape& draft);

    struct DraftListItem
    {
        const char* Label = nullptr;
        std::size_t SecretBytes = 0;
        bool CarriesUnknown = false;
    };
    Status ValidateListShape(const DraftListItem* items, std::size_t count);

    Status ValidateListItems(const std::vector<ListItem>& items);

    Status Validate(const Entry& entry);

    Entry* FindEntry(Document& doc, std::uint64_t id);
    const Entry* FindEntry(const Document& doc, std::uint64_t id);

    Result<std::uint64_t> AddEntry(Document& doc, Entry&& draft, std::uint64_t nowUtc);
    Result<std::uint64_t> AddEntry(Document& doc, const Entry& draft, std::uint64_t nowUtc);

    Status UpdateEntry(Document& doc, std::uint64_t id, Entry&& replacement,
                       std::uint64_t nowUtc);
    Status UpdateEntry(Document& doc, std::uint64_t id, const Entry& replacement,
                       std::uint64_t nowUtc);

    Status SetListItems(Document& doc, std::uint64_t id, std::vector<ListItem>&& items,
                        std::uint64_t nowUtc);

    Status DeleteEntry(Document& doc, std::uint64_t id);

    Status TakeEntry(Document& doc, std::uint64_t id, Entry& out, std::size_t& index);
    Status RestoreEntry(Document& doc, Entry&& entry, std::size_t index);

    bool EnsureHistoryKey(Document& doc);

    Status SetFavourite(Document& doc, std::uint64_t id, bool favourite,
                        std::uint64_t nowUtc);

    Status SetEntryTitle(Document& doc, std::uint64_t id, std::string title,
                         std::uint64_t nowUtc);

    Status SetAccent(Document& doc, std::uint64_t id, std::uint32_t accent,
                     std::uint64_t nowUtc);

    Status SetSymbol(Document& doc, std::uint64_t id, std::string symbol,
                     std::uint64_t nowUtc);

    Status SetSecondFactorState(Document& doc, std::uint64_t id,
                                std::uint32_t state, std::uint64_t nowUtc);

    Status SetEntryFolder(Document& doc, std::uint64_t id, std::string folder,
                          std::uint64_t nowUtc);

    Result<std::size_t> RenameFolder(Document& doc, const std::string& from,
                                     const std::string& to, std::uint64_t nowUtc);

    Result<std::size_t> DissolveFolder(Document& doc, const std::string& name,
                                       std::uint64_t nowUtc);

    Status SetTotp(Document& doc, std::uint64_t id, const TotpConfig& config,
                   std::uint64_t nowUtc);
    Status ClearTotp(Document& doc, std::uint64_t id, std::uint64_t nowUtc);

    Status AddRecoveryCodes(Document& doc, std::uint64_t id,
                            const std::vector<std::string>& codes,
                            std::uint64_t nowUtc);

    Status DeleteRecoveryCode(Document& doc, std::uint64_t id, std::size_t index,
                              std::uint64_t nowUtc);

    Status ClearRecoveryCodes(Document& doc, std::uint64_t id, std::uint64_t nowUtc);

    Status SetRecoveryCodeUsed(Document& doc, std::uint64_t id, std::size_t index,
                               bool used, std::uint64_t nowUtc);

    void SetVaultSettings(Document& doc, Settings draft);

    void RecordExport(Document& doc, std::uint64_t whenUtc, std::string path);

    Status MoveField(Document& doc, std::uint64_t id, std::size_t from,
                     std::size_t to, std::uint64_t nowUtc);
}
