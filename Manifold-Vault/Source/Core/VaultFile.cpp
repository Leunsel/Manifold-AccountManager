#include "Core/VaultFile.h"

#include "Core/Endian.h"
#include "Core/Palette.h"
#include "Core/SettingsPolicy.h"
#include "Core/VaultFormat.h"

#include <sodium.h>

#include <cstring>
#include <vector>

namespace Bunker::Vault
{
    namespace
    {
        constexpr std::size_t kPadBlock = 4096;
        constexpr std::size_t kMaxPlainBytes = 32u * 1024 * 1024;

        void BuildHeaderRegionA(unsigned char* h,
                                std::uint16_t versionMinor,
                                std::uint16_t slotCount,
                                const Crypto::KdfParams& params,
                                const unsigned char salt[Crypto::kSaltBytes],
                                const unsigned char nonce[Crypto::kNonceBytes],
                                std::uint64_t generation,
                                std::uint8_t gateTheme,
                                std::uint8_t gateTextSize)
        {
            std::memset(h, 0, kFixedHeaderBytes);
            std::memcpy(h + HeaderOffset::Magic, kMagic, sizeof(kMagic));
            StoreU16Le(h + HeaderOffset::VersionMajor, kVersionMajor);
            StoreU16Le(h + HeaderOffset::VersionMinor, versionMinor);
            StoreU16Le(h + HeaderOffset::HeaderLength,
                       static_cast<std::uint16_t>(HeaderLength(slotCount)));
            StoreU16Le(h + HeaderOffset::SlotCount, slotCount);
            StoreU32Le(h + HeaderOffset::Flags, 0);
            StoreU64Le(h + HeaderOffset::KdfOpsLimit, params.OpsLimit);
            StoreU64Le(h + HeaderOffset::KdfMemLimit, params.MemLimit);
            h[HeaderOffset::KdfAlgorithm] = kKdfArgon2id13;
            h[HeaderOffset::GateTheme] = gateTheme;
            h[HeaderOffset::GateTextSize] = gateTextSize;
            std::memcpy(h + HeaderOffset::KdfSalt, salt, Crypto::kSaltBytes);
            std::memcpy(h + HeaderOffset::BodyNonce, nonce, Crypto::kNonceBytes);
            StoreU64Le(h + HeaderOffset::Generation, generation);
        }

        Status WrapVaultKey(unsigned char* slot,
                            const SecureBuffer& kek,
                            const SecureBuffer& vaultKey,
                            const unsigned char* regionA)
        {
            std::memset(slot, 0, kSlotBytes);
            StoreU32Le(slot + SlotOffset::Type, kSlotTypePassword);
            StoreU32Le(slot + SlotOffset::Flags, 0);

            unsigned char nonce[Crypto::kNonceBytes];
            Crypto::RandomBytes(nonce, sizeof(nonce));
            std::memcpy(slot + SlotOffset::WrapNonce, nonce, sizeof(nonce));

            std::size_t produced = 0;
            return Crypto::Encrypt(slot + SlotOffset::WrappedKey, &produced,
                                   vaultKey.Data(), vaultKey.Size(),
                                   regionA, kFixedHeaderBytes,
                                   nonce, kek);
        }

        Status ComputeChecksum(unsigned char out[kChecksumBytes],
                               const unsigned char* header, std::size_t lenWithoutChecksum)
        {
            return Crypto::Hash(out, kChecksumBytes, header, lenWithoutChecksum);
        }
    }

    VaultFile::~VaultFile()
    {
        Lock();
    }

    void VaultFile::Lock()
    {
        WipeDocument(m_doc);
        m_doc = Document{};
        m_kek.Reset();
        m_vaultKey.Reset();
        sodium_memzero(m_salt, sizeof(m_salt));
        m_generation = 0;
        m_headerTheme = 0;
        m_headerTextSize = 0;
        m_unlocked = false;
    }

    Status VaultFile::Create(IFileSystem& fs, const std::string& path,
                             const char* password, std::size_t passwordLen,
                             const Crypto::KdfParams& params, VaultFile& out)
    {
        if (!Crypto::IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!password || passwordLen == 0)
            return Fail(ErrorCode::InvalidArgument, "empty master password");
        if (!params.IsSane())
            return Fail(ErrorCode::InvalidArgument, "nonsensical KDF parameters");

        out.Lock();
        Crypto::RandomBytes(out.m_salt, sizeof(out.m_salt));
        out.m_params = params;

        if (const Status st = Crypto::DeriveKeyFromPassword(out.m_kek, password, passwordLen,
                                                            out.m_salt, params); !st.IsOk())
            return st;

        out.m_vaultKey = SecureBuffer(Crypto::kKeyBytes);
        if (!out.m_vaultKey.IsValid())
            return Fail(ErrorCode::OutOfMemory, "vault key");
        Crypto::RandomBytes(out.m_vaultKey.Data(), out.m_vaultKey.Size());

        out.m_generation = 0;
        out.m_versionMinor = kVersionMinor;
        out.m_unlocked = true;
        out.m_doc = Document{};

        return out.Save(fs, path);
    }

    Status VaultFile::Unlock(IFileSystem& fs, const std::string& path,
                             const char* password, std::size_t passwordLen,
                             VaultFile& out)
    {
        if (!Crypto::IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!password || passwordLen == 0)
            return Fail(ErrorCode::InvalidArgument, "empty master password");

        out.Lock();

        std::vector<unsigned char> file;
        if (const Status st = fs.Read(path, file); !st.IsOk())
            return st;

        if (file.size() < kFixedHeaderBytes)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "shorter than the header");
        if (std::memcmp(file.data() + HeaderOffset::Magic, kMagic, sizeof(kMagic)) != 0)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "wrong magic bytes");

        const std::uint16_t major = LoadU16Le(file.data() + HeaderOffset::VersionMajor);
        const std::uint16_t minor = LoadU16Le(file.data() + HeaderOffset::VersionMinor);
        if (major > kVersionMajor)
            return Fail(ErrorCode::UnsupportedFormatVersion, "newer major version");

        const std::uint16_t slotCount = LoadU16Le(file.data() + HeaderOffset::SlotCount);
        if (slotCount < 1 || slotCount > kMaxSlots)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "implausible slot count");

        const std::uint16_t headerLen = LoadU16Le(file.data() + HeaderOffset::HeaderLength);
        if (headerLen != HeaderLength(slotCount) || file.size() < headerLen)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "header length does not match");

        Crypto::KdfParams params;
        params.OpsLimit = LoadU64Le(file.data() + HeaderOffset::KdfOpsLimit);
        params.MemLimit = LoadU64Le(file.data() + HeaderOffset::KdfMemLimit);
        if (file[HeaderOffset::KdfAlgorithm] != kKdfArgon2id13)
            return Fail(ErrorCode::UnsupportedFormatVersion, "unknown KDF algorithm");
        if (!params.IsSane())
            return Fail(ErrorCode::FileTamperedOrCorrupt, "nonsensical KDF parameters in the header");

        unsigned char expected[kChecksumBytes] = {};
        if (const Status st = ComputeChecksum(expected, file.data(),
                                              std::size_t{ headerLen } - kChecksumBytes);
            !st.IsOk())
            return st;
        if (!SecretEquals(expected, file.data() + headerLen - kChecksumBytes, kChecksumBytes))
            return Fail(ErrorCode::FileTamperedOrCorrupt, "header checksum does not match");

        const std::size_t bodyLen = file.size() - headerLen;
        if (bodyLen < Crypto::kTagBytes)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "body shorter than the auth tag");
        if (bodyLen > kMaxBodyBytes)
            return Fail(ErrorCode::FileTooLarge, "body implausibly large");

        std::memcpy(out.m_salt, file.data() + HeaderOffset::KdfSalt, Crypto::kSaltBytes);
        out.m_params = params;

        if (const Status st = Crypto::DeriveKeyFromPassword(out.m_kek, password, passwordLen,
                                                            out.m_salt, params); !st.IsOk())
        {
            out.Lock();
            return st;
        }

        out.m_vaultKey = SecureBuffer(Crypto::kKeyBytes);
        if (!out.m_vaultKey.IsValid())
        {
            out.Lock();
            return Fail(ErrorCode::OutOfMemory, "vault key");
        }

        bool unwrapped = false;
        for (std::uint16_t i = 0; i < slotCount && !unwrapped; ++i)
        {
            const unsigned char* slot = file.data() + kFixedHeaderBytes +
                                        std::size_t{ i } * kSlotBytes;
            if (LoadU32Le(slot + SlotOffset::Type) != kSlotTypePassword)
                continue;

            std::size_t produced = 0;
            if (Crypto::Decrypt(out.m_vaultKey.Data(), &produced,
                                slot + SlotOffset::WrappedKey, kWrappedKeyBytes,
                                file.data(), kFixedHeaderBytes,
                                slot + SlotOffset::WrapNonce, out.m_kek).IsOk() &&
                produced == Crypto::kKeyBytes)
            {
                unwrapped = true;
            }
        }

        if (!unwrapped)
        {
            out.Lock();
            return Fail(ErrorCode::WrongPassword, "no key slot matched");
        }

        SecureBuffer bodyKey;
        if (const Status st = Crypto::DeriveSubkey(bodyKey, Crypto::kKeyBytes, kSubkeyBody,
                                                   kKdfContext, out.m_vaultKey); !st.IsOk())
        {
            out.Lock();
            return st;
        }

        SecureBuffer plain(bodyLen);
        if (!plain.IsValid())
        {
            out.Lock();
            return Fail(ErrorCode::OutOfMemory, "body buffer");
        }

        std::size_t plainLen = 0;
        if (!Crypto::Decrypt(plain.Data(), &plainLen,
                             file.data() + headerLen, bodyLen,
                             file.data(), headerLen,
                             file.data() + HeaderOffset::BodyNonce, bodyKey).IsOk())
        {
            out.Lock();
            return Fail(ErrorCode::FileTamperedOrCorrupt, "body authentication failed");
        }

        std::size_t unpaddedLen = 0;
        if (sodium_unpad(&unpaddedLen, plain.Data(), plainLen, kPadBlock) != 0)
        {
            out.Lock();
            return Fail(ErrorCode::FileTamperedOrCorrupt, "invalid padding");
        }

        const std::size_t lostBefore = SecureArena::Instance().LostCount();
        if (!Decode(plain.Data(), unpaddedLen, out.m_doc))
        {
            out.Lock();
            if (SecureArena::Instance().LostCount() != lostBefore)
                return Fail(ErrorCode::OutOfMemory, "not enough protected memory");
            return Fail(ErrorCode::FileTamperedOrCorrupt, "content not readable");
        }

        out.m_generation = LoadU64Le(file.data() + HeaderOffset::Generation);
        out.m_headerTheme = file[HeaderOffset::GateTheme];
        out.m_headerTextSize = file[HeaderOffset::GateTextSize];
        out.m_versionMinor = minor;
        out.m_unlocked = true;
        return Ok();
    }

    Status VaultFile::Serialise(std::vector<unsigned char>& out)
    {
        SecureBuffer padded;
        std::size_t plainLen = 0;
        if (const Status st = EncodeToSecure(m_doc, padded, plainLen,
                                             kPadBlock, kMaxPlainBytes);
            !st.IsOk())
            return st;

        std::size_t paddedLen = 0;
        if (sodium_pad(&paddedLen, padded.Data(), plainLen, kPadBlock, padded.Size()) != 0)
            return Fail(ErrorCode::InvalidArgument, "padding failed");

        unsigned char bodyNonce[Crypto::kNonceBytes];
        // Fresh nonces on every save; the key slot is rewrapped as well.
        Crypto::RandomBytes(bodyNonce, sizeof(bodyNonce));

        const std::uint16_t slotCount = kSlotsWritten;
        const std::size_t headerLen = HeaderLength(slotCount);

        out.assign(headerLen + paddedLen + Crypto::kTagBytes, 0);

        BuildHeaderRegionA(out.data(), m_versionMinor, slotCount, m_params,
                           m_salt, bodyNonce, m_generation + 1, GateThemeToWrite(),
                           GateTextSizeToWrite());

        if (const Status st = WrapVaultKey(out.data() + kFixedHeaderBytes,
                                           m_kek, m_vaultKey, out.data()); !st.IsOk())
            return st;

        if (const Status st = ComputeChecksum(out.data() + headerLen - kChecksumBytes,
                                              out.data(), headerLen - kChecksumBytes);
            !st.IsOk())
            return st;

        SecureBuffer bodyKey;
        if (const Status st = Crypto::DeriveSubkey(bodyKey, Crypto::kKeyBytes, kSubkeyBody,
                                                   kKdfContext, m_vaultKey); !st.IsOk())
            return st;

        std::size_t produced = 0;
        if (const Status st = Crypto::Encrypt(out.data() + headerLen, &produced,
                                              padded.Data(), paddedLen,
                                              out.data(), headerLen,
                                              bodyNonce, bodyKey); !st.IsOk())
            return st;

        if (produced != paddedLen + Crypto::kTagBytes)
            return Fail(ErrorCode::EncryptFailed, "unexpected ciphertext length");
        return Ok();
    }

    Status VaultFile::PeekHeader(IFileSystem& fs, const std::string& path,
                                 HeaderInfo& out)
    {
        out = HeaderInfo{};

        std::vector<unsigned char> file;
        if (const Status st = fs.Read(path, file); !st.IsOk())
            return st;

        if (file.size() < kFixedHeaderBytes)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "shorter than the header");
        if (std::memcmp(file.data() + HeaderOffset::Magic, kMagic, sizeof(kMagic)) != 0)
            return Fail(ErrorCode::FileTamperedOrCorrupt, "wrong magic bytes");

        out.VersionMajor = LoadU16Le(file.data() + HeaderOffset::VersionMajor);
        out.VersionMinor = LoadU16Le(file.data() + HeaderOffset::VersionMinor);
        out.SlotCount = LoadU16Le(file.data() + HeaderOffset::SlotCount);
        out.Generation = LoadU64Le(file.data() + HeaderOffset::Generation);
        out.GateTheme = file[HeaderOffset::GateTheme];
        out.GateTextSize = file[HeaderOffset::GateTextSize];

        if (out.VersionMajor > kVersionMajor)
            return Fail(ErrorCode::UnsupportedFormatVersion, "newer major version");
        return Ok();
    }

    Status VaultFile::Save(IFileSystem& fs, const std::string& path)
    {
        if (!m_unlocked || !m_vaultKey.IsValid() || !m_kek.IsValid())
            return Fail(ErrorCode::NotInitialised, "vault is locked");

        if (fs.Exists(path))
        {
            HeaderInfo onDisk;
            if (const Status st = PeekHeader(fs, path, onDisk); !st.IsOk())
                return st;
            if (onDisk.Generation != m_generation)
                return Fail(ErrorCode::VaultChangedOnDisk,
                            "unexpected generation in the vault file");
        }

        std::vector<unsigned char> file;
        if (const Status st = Serialise(file); !st.IsOk())
            return st;

        if (const Status st = fs.WriteAtomic(path, file.data(), file.size()); !st.IsOk())
            return st;

        ++m_generation;
        m_headerTheme = file[HeaderOffset::GateTheme];
        m_headerTextSize = file[HeaderOffset::GateTextSize];
        return Ok();
    }

    std::uint8_t VaultFile::GateThemeToWrite() const
    {
        const std::string& id = m_doc.VaultSettings.Theme;
        const std::uint8_t code = PaletteHeaderCode(id);
        if (code == 0 && !id.empty())
            return m_headerTheme;
        return code;
    }

    std::uint8_t VaultFile::GateTextSizeToWrite() const
    {
        const std::uint32_t size = TextSizePercent(m_doc.VaultSettings);
        return size == kDefaultTextSize ? 0 : static_cast<std::uint8_t>(size);
    }

    bool VaultFile::HeaderHintsStale() const
    {
        return m_unlocked && (GateThemeToWrite() != m_headerTheme ||
                              GateTextSizeToWrite() != m_headerTextSize);
    }

    Status VaultFile::ChangePassword(IFileSystem& fs, const std::string& path,
                                     const char* newPassword, std::size_t newPasswordLen,
                                     const Crypto::KdfParams& params)
    {
        if (!m_unlocked)
            return Fail(ErrorCode::NotInitialised, "vault is locked");
        if (!newPassword || newPasswordLen == 0)
            return Fail(ErrorCode::InvalidArgument, "empty master password");
        if (!params.IsSane())
            return Fail(ErrorCode::InvalidArgument, "nonsensical KDF parameters");

        unsigned char newSalt[Crypto::kSaltBytes];
        Crypto::RandomBytes(newSalt, sizeof(newSalt));

        SecureBuffer newKek;
        if (const Status st = Crypto::DeriveKeyFromPassword(newKek, newPassword, newPasswordLen,
                                                            newSalt, params); !st.IsOk())
            return st;

        const Status st = ChangePasswordWithKek(fs, path, std::move(newKek), newSalt, params);
        sodium_memzero(newSalt, sizeof(newSalt));
        return st;
    }

    bool VaultFile::KekMatches(const SecureBuffer& candidate) const
    {
        if (!m_unlocked || !m_kek.IsValid() || !candidate.IsValid())
            return false;
        if (candidate.Size() != m_kek.Size())
            return false;
        return SecretEquals(candidate.Data(), m_kek.Data(), m_kek.Size());
    }

    Status VaultFile::ChangePasswordWithKek(IFileSystem& fs, const std::string& path,
                                            SecureBuffer newKek,
                                            const unsigned char newSalt[Crypto::kSaltBytes],
                                            const Crypto::KdfParams& params)
    {
        if (!m_unlocked)
            return Fail(ErrorCode::NotInitialised, "vault is locked");
        if (!newKek.IsValid() || !newSalt)
            return Fail(ErrorCode::InvalidArgument, "no key");
        if (!params.IsSane())
            return Fail(ErrorCode::InvalidArgument, "nonsensical KDF parameters");

        SecureBuffer oldKek = std::move(m_kek);
        unsigned char oldSalt[Crypto::kSaltBytes];
        std::memcpy(oldSalt, m_salt, sizeof(oldSalt));
        const Crypto::KdfParams oldParams = m_params;

        m_kek = std::move(newKek);
        std::memcpy(m_salt, newSalt, Crypto::kSaltBytes);
        m_params = params;

        const Status st = Save(fs, path);
        if (!st.IsOk())
        {
            m_kek = std::move(oldKek);
            std::memcpy(m_salt, oldSalt, sizeof(m_salt));
            m_params = oldParams;
        }
        sodium_memzero(oldSalt, sizeof(oldSalt));
        return st;
    }
}
