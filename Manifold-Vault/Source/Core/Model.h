#pragma once

#include "Core/Codec.h"
#include "Core/Crypto.h"
#include "Core/FieldRole.h"
#include "Core/Otp.h"
#include "Core/Secret.h"
#include "Core/VaultFormat.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace Bunker::Vault
{
    struct Field
    {
        std::string Key;
        SecureString Value;
        std::uint32_t Flags = 0;
        FieldRole Role = FieldRole::Custom;

        bool IsSecret() const { return (Flags & kFieldFlagSecret) != 0; }

        std::vector<UnknownRecord> Unknown;
    };

    struct StoredTotp
    {
        TotpConfig Config;
        std::vector<UnknownRecord> Unknown;
    };

    enum SecondFactorState : std::uint32_t
    {
        SecondFactorUnknown = 0,
        SecondFactorNotOffered = 1,
        SecondFactorElsewhere = 2,
    };

    constexpr bool SecondFactorIsHandled(std::uint32_t state)
    {
        return state == SecondFactorNotOffered || state == SecondFactorElsewhere;
    }

    struct RecoveryCode
    {
        SecureString Value;
        std::uint64_t UsedUtc = 0;

        bool IsUsed() const { return UsedUtc != 0; }

        std::vector<UnknownRecord> Unknown;
    };

    inline constexpr std::uint32_t kListKindOther = 0;
    inline constexpr std::uint32_t kListKindProductKey = 1;
    inline constexpr std::uint32_t kListKindSecurityQuestion = 2;
    inline constexpr std::uint32_t kListKindOrderNumber = 3;
    inline constexpr std::uint32_t kListKindGiftCode = 4;
    inline constexpr std::uint32_t kListKindSerialNumber = 5;

    struct ListItem
    {
        std::uint32_t Kind = kListKindOther;

        std::string Label;

        SecureString Secret;

        std::uint64_t AddedUtc = 0;

        std::vector<UnknownRecord> Unknown;
    };

    struct PasswordMemory
    {
        unsigned char Mac[Crypto::kHashBytes] = {};
        std::uint64_t RetiredUtc = 0;

        std::vector<UnknownRecord> Unknown;
    };

    struct Entry
    {
        std::uint64_t Id = 0;
        std::string Title;
        std::string PlatformId;

        std::string Folder;

        bool Favourite = false;

        std::uint32_t Accent = 0;

        std::string Symbol;

        std::uint32_t SecondFactorState = 0;

        std::uint64_t CreatedUtc = 0;
        std::uint64_t ModifiedUtc = 0;
        std::vector<Field> Fields;

        bool HasTotp = false;
        StoredTotp Totp;

        std::vector<RecoveryCode> RecoveryCodes;

        std::vector<PasswordMemory> PasswordHistory;

        std::vector<ListItem> ListItems;

        std::vector<UnknownRecord> Unknown;
    };

    inline constexpr std::uint32_t kDefaultTextSize = 100;

    struct Settings
    {
        std::uint32_t AutoLockSeconds = 300;
        std::uint32_t ClipboardClearSeconds = 20;

        bool LockOnFocusLost = false;
        std::uint32_t FocusGraceSeconds = 20;
        bool LockOnMinimise = false;

        bool BackupEnabled = true;
        std::uint32_t BackupKeep = 7;
        std::uint32_t BackupMinIntervalSeconds = 3600;

        std::uint64_t LastExportUtc = 0;
        std::string LastExportPath;

        std::string Language;

        bool SidebarCollapsed = false;

        std::string Theme;

        bool MonochromeIcons = false;

        std::uint32_t ListOrder = 0;

        std::uint32_t TextSize = kDefaultTextSize;

        std::vector<UnknownRecord> Unknown;
    };

    struct Document
    {
        Settings VaultSettings;
        std::vector<Entry> Entries;

        std::uint64_t NextId = 1;

        std::vector<unsigned char> HistoryKey;

        std::vector<UnknownRecord> Unknown;

        std::uint64_t DerivedNextEntryId() const
        {
            std::uint64_t highest = 0;
            for (const Entry& e : Entries)
                if (e.Id > highest)
                    highest = e.Id;
            return highest + 1;
        }
    };

    void Encode(const Document& doc, Writer& out);

    Status EncodeToSecure(const Document& doc, SecureBuffer& out, std::size_t& len,
                          std::size_t slack, std::size_t maxBytes);

    bool Decode(const unsigned char* data, std::size_t len, Document& out);

    void WipeField(Field& f) noexcept;

    void WipeListItem(ListItem& item) noexcept;

    void DropListItems(Entry& e) noexcept;

    [[nodiscard]] bool CopyListItems(const std::vector<ListItem>& from,
                                     std::vector<ListItem>& out);

    bool SameListItems(const std::vector<ListItem>& a, const std::vector<ListItem>& b);

    void WipeDocument(Document& doc) noexcept;

    [[nodiscard]] bool CopyEntry(const Entry& from, Entry& out);

    static_assert(std::is_nothrow_move_constructible_v<Field> &&
                      std::is_nothrow_move_assignable_v<Field>,
                  "Field must stay nothrow-movable, or vector growth copies values");
    static_assert(std::is_nothrow_move_constructible_v<Entry> &&
                      std::is_nothrow_move_assignable_v<Entry>,
                  "Entry must stay nothrow-movable, or vector growth copies values");
    static_assert(std::is_nothrow_move_constructible_v<RecoveryCode> &&
                      std::is_nothrow_move_assignable_v<RecoveryCode>,
                  "RecoveryCode must stay nothrow-movable, or vector growth copies codes");
    static_assert(std::is_nothrow_move_constructible_v<ListItem> &&
                      std::is_nothrow_move_assignable_v<ListItem>,
                  "ListItem must stay nothrow-movable, or vector growth copies values");
}
