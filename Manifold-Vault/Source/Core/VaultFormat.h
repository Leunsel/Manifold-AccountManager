// Vault file layout (v1, little-endian):
//   A  fixed header, 96 bytes          AEAD associated data (authenticated)
//   B  key slots, 80 bytes each        AEAD associated data (authenticated)
//   C  BLAKE2b-128 of A and B          catches accidental damage only
//   D  XChaCha20-Poly1305 body, padded to 4 KiB
#pragma once

#include "Core/Crypto.h"

#include <cstddef>
#include <cstdint>

namespace Bunker::Vault
{
    // Never change: identifies the file format.
    inline constexpr unsigned char kMagic[8] = { 'B', 'U', 'N', 'K', 'E', 'R', 0x1A, 0x00 };

    inline constexpr std::uint16_t kVersionMajor = 1;
    inline constexpr std::uint16_t kVersionMinor = 0;

    inline constexpr std::size_t kFixedHeaderBytes = 96;
    inline constexpr std::size_t kSlotBytes = 80;
    inline constexpr std::size_t kChecksumBytes = 16;

    inline constexpr std::uint16_t kSlotsWritten = 1;
    inline constexpr std::uint16_t kMaxSlots = 4;

    inline constexpr std::uint64_t kMaxBodyBytes = 64ull * 1024 * 1024;

    namespace HeaderOffset
    {
        inline constexpr std::size_t Magic = 0;
        inline constexpr std::size_t VersionMajor = 8;
        inline constexpr std::size_t VersionMinor = 10;
        inline constexpr std::size_t HeaderLength = 12;
        inline constexpr std::size_t SlotCount = 14;
        inline constexpr std::size_t Flags = 16;
        inline constexpr std::size_t Reserved0 = 20;
        inline constexpr std::size_t KdfOpsLimit = 24;
        inline constexpr std::size_t KdfMemLimit = 32;
        inline constexpr std::size_t KdfAlgorithm = 40;
        // Hints for the unlock screen (colour scheme, text size): unencrypted, but authenticated.
        inline constexpr std::size_t GateTheme = 41;
        inline constexpr std::size_t GateTextSize = 42;
        inline constexpr std::size_t Reserved1 = 43;
        inline constexpr std::size_t KdfSalt = 48;
        inline constexpr std::size_t BodyNonce = 64;
        inline constexpr std::size_t Generation = 88;
    }
    static_assert(HeaderOffset::Generation + 8 == kFixedHeaderBytes, "region A layout drift");

    namespace SlotOffset
    {
        inline constexpr std::size_t Type = 0;
        inline constexpr std::size_t Flags = 4;
        inline constexpr std::size_t WrapNonce = 8;
        inline constexpr std::size_t WrappedKey = 32;
    }
    static_assert(SlotOffset::WrappedKey + 48 == kSlotBytes, "slot layout drift");

    inline constexpr std::uint32_t kSlotTypePassword = 1;
    inline constexpr std::uint8_t kKdfArgon2id13 = 1;

    inline constexpr std::size_t kWrappedKeyBytes = Crypto::kKeyBytes + Crypto::kTagBytes;
    static_assert(kWrappedKeyBytes == 48, "wrapped key size drift");

    inline constexpr std::size_t HeaderLength(std::uint16_t slotCount)
    {
        return kFixedHeaderBytes + std::size_t{ slotCount } * kSlotBytes + kChecksumBytes;
    }
    static_assert(HeaderLength(1) == 192, "v1 header should be 192 bytes");

    // Never change: part of every derived key.
    inline constexpr char kKdfContext[Crypto::kKdfContextBytes] = { 'B','u','n','k','e','r','V','1' };
    inline constexpr std::uint64_t kSubkeyBody = 1;

    // Tag numbers are part of the format: never renumber or reuse one.
    // Readers keep records with unknown tags and write them back unchanged.
    enum class Tag : std::uint16_t
    {
        VaultSettings = 1,
        VaultNextEntryId = 2,
        VaultHistoryKey = 3,
        Entry = 10,

        EntryId = 100,
        EntryTitle = 101,
        EntryPlatformId = 102,
        EntryCreatedUtc = 103,
        EntryModifiedUtc = 104,
        EntryFolder = 105,
        EntryFavourite = 106,
        EntryAccent = 107,
        EntrySecondFactorState = 108,
        EntrySymbol = 109,
        EntryField = 110,

        EntryTotp = 120,

        EntryRecoveryCode = 130,

        EntryPasswordHistory = 140,

        EntryListItem = 150,

        FieldKey = 200,
        FieldValue = 201,
        FieldFlags = 202,
        FieldRole = 203,

        TotpSecret = 300,
        TotpDigits = 301,
        TotpPeriod = 302,
        TotpAlgorithm = 303,
        TotpIssuer = 304,
        TotpAccount = 305,

        RecoveryCodeValue = 400,
        RecoveryCodeUsedUtc = 401,

        HistoryMacBytes = 500,
        HistoryRetiredUtc = 501,

        ListItemKind = 600,
        ListItemLabel = 601,
        ListItemSecret = 602,
        ListItemAddedUtc = 603,
    };

    inline constexpr std::uint32_t kFieldFlagSecret = 1u << 0;

    // Same rule as Tag: numbers are assigned here once and never reused.
    enum class SettingsTag : std::uint16_t
    {
        AutoLockSeconds = 1,
        ClipboardClearSeconds = 2,

        LockOnFocusLost = 3,
        FocusGraceSeconds = 4,
        LockOnMinimise = 5,

        BackupEnabled = 6,
        BackupKeep = 7,
        BackupMinIntervalSeconds = 8,

        LastExportUtc = 9,
        LastExportPath = 10,

        Language = 11,

        SidebarCollapsed = 12,

        Theme = 13,

        MonochromeIcons = 14,

        ListOrder = 15,

        TextSize = 16,
    };
}
