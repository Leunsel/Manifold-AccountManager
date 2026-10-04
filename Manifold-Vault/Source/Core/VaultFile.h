#pragma once

#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/Model.h"
#include "Core/Result.h"
#include "Core/Secret.h"
#include "Core/VaultFormat.h"

#include <cstdint>
#include <string>

namespace Bunker::Vault
{
    class VaultFile
    {
    public:
        VaultFile() = default;
        ~VaultFile();

        VaultFile(const VaultFile&) = delete;
        VaultFile& operator=(const VaultFile&) = delete;
        VaultFile(VaultFile&&) = default;
        VaultFile& operator=(VaultFile&&) = default;

        static Status Create(IFileSystem& fs, const std::string& path,
                             const char* password, std::size_t passwordLen,
                             const Crypto::KdfParams& params,
                             VaultFile& out);

        static Status Unlock(IFileSystem& fs, const std::string& path,
                             const char* password, std::size_t passwordLen,
                             VaultFile& out);

        struct HeaderInfo
        {
            std::uint16_t VersionMajor = 0;
            std::uint16_t VersionMinor = 0;
            std::uint16_t SlotCount = 0;
            std::uint64_t Generation = 0;
            std::uint8_t GateTheme = 0;
            std::uint8_t GateTextSize = 0;
        };

        static Status PeekHeader(IFileSystem& fs, const std::string& path,
                                 HeaderInfo& out);

        Status Save(IFileSystem& fs, const std::string& path);

        Status ChangePassword(IFileSystem& fs, const std::string& path,
                              const char* newPassword, std::size_t newPasswordLen,
                              const Crypto::KdfParams& params);

        Status ChangePasswordWithKek(IFileSystem& fs, const std::string& path,
                                     SecureBuffer newKek,
                                     const unsigned char newSalt[Crypto::kSaltBytes],
                                     const Crypto::KdfParams& params);

        bool KekMatches(const SecureBuffer& candidate) const;

        const unsigned char* Salt() const { return m_salt; }

        bool HeaderHintsStale() const;

        bool IsUnlocked() const { return m_unlocked; }
        Document& Doc() { return m_doc; }
        const Document& Doc() const { return m_doc; }
        std::uint64_t Generation() const { return m_generation; }
        const Crypto::KdfParams& Params() const { return m_params; }

        void Lock();

    private:
        Status Serialise(std::vector<unsigned char>& out);
        std::uint8_t GateThemeToWrite() const;
        std::uint8_t GateTextSizeToWrite() const;

        Document m_doc;
        SecureBuffer m_kek;
        SecureBuffer m_vaultKey;
        unsigned char m_salt[Crypto::kSaltBytes] = {};
        Crypto::KdfParams m_params;
        std::uint64_t m_generation = 0;
        std::uint16_t m_versionMinor = kVersionMinor;
        std::uint8_t m_headerTheme = 0;
        std::uint8_t m_headerTextSize = 0;
        bool m_unlocked = false;
    };
}
