#pragma once

#include "Core/Result.h"
#include "Core/Secret.h"

#include <cstddef>
#include <cstdint>

namespace Bunker::Crypto
{
    inline constexpr std::size_t kKeyBytes = 32;
    inline constexpr std::size_t kNonceBytes = 24;
    inline constexpr std::size_t kTagBytes = 16;
    inline constexpr std::size_t kSaltBytes = 16;
    inline constexpr std::size_t kKdfContextBytes = 8;
    inline constexpr std::size_t kHashBytes = 32;

    struct KdfParams
    {
        std::uint64_t OpsLimit = 0;
        std::uint64_t MemLimit = 0;

        static KdfParams Moderate();
        static KdfParams Interactive();

        static KdfParams TestOnlyMinimum();

        bool IsSane() const;
    };

    Status Initialise();

    bool IsReady();

    Status RunSelfTest();

    void RandomBytes(unsigned char* out, std::size_t len);

    Status DeriveKeyFromPassword(SecureBuffer& outKey,
                                 const char* password, std::size_t passwordLen,
                                 const unsigned char salt[kSaltBytes],
                                 const KdfParams& params);

    Status DeriveSubkey(SecureBuffer& outKey, std::size_t outLen,
                        std::uint64_t subkeyId,
                        const char context[kKdfContextBytes],
                        const SecureBuffer& masterKey);

    Status Encrypt(unsigned char* out, std::size_t* outLen,
                   const unsigned char* plain, std::size_t plainLen,
                   const unsigned char* aad, std::size_t aadLen,
                   const unsigned char nonce[kNonceBytes],
                   const SecureBuffer& key);

    Status Decrypt(unsigned char* out, std::size_t* outLen,
                   const unsigned char* cipher, std::size_t cipherLen,
                   const unsigned char* aad, std::size_t aadLen,
                   const unsigned char nonce[kNonceBytes],
                   const SecureBuffer& key);

    Status Hash(unsigned char* out, std::size_t outLen,
                const unsigned char* in, std::size_t inLen);
}
