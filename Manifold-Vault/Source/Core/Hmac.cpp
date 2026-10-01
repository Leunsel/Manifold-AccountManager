#include "Core/Hmac.h"

#include "Core/Sha1.h"

#include <sodium.h>

#include <cstring>

namespace Bunker
{
    std::size_t DigestBytes(HashAlgorithm algorithm)
    {
        switch (algorithm)
        {
        case HashAlgorithm::Sha1:   return Sha1::kDigestBytes;
        case HashAlgorithm::Sha256: return crypto_auth_hmacsha256_BYTES;
        case HashAlgorithm::Sha512: return crypto_auth_hmacsha512_BYTES;
        }
        return 0;
    }

    const char* AlgorithmName(HashAlgorithm algorithm)
    {
        switch (algorithm)
        {
        case HashAlgorithm::Sha1:   return "SHA1";
        case HashAlgorithm::Sha256: return "SHA256";
        case HashAlgorithm::Sha512: return "SHA512";
        }
        return "SHA1";
    }

    bool ParseAlgorithm(const char* text, HashAlgorithm& out)
    {
        if (!text)
            return false;

        char upper[16] = {};
        std::size_t n = 0;
        for (const char* p = text; *p; ++p)
        {
            if (n + 1 >= sizeof(upper))
                return false;
            const unsigned char c = static_cast<unsigned char>(*p);
            upper[n++] = static_cast<char>((c >= 'a' && c <= 'z') ? (c - 32) : c);
        }
        upper[n] = 0;

        if (std::strcmp(upper, "SHA1") == 0 || std::strcmp(upper, "SHA-1") == 0)
        {
            out = HashAlgorithm::Sha1;
            return true;
        }
        if (std::strcmp(upper, "SHA256") == 0 || std::strcmp(upper, "SHA-256") == 0)
        {
            out = HashAlgorithm::Sha256;
            return true;
        }
        if (std::strcmp(upper, "SHA512") == 0 || std::strcmp(upper, "SHA-512") == 0)
        {
            out = HashAlgorithm::Sha512;
            return true;
        }
        return false;
    }

    namespace
    {
        bool HmacSha1(const unsigned char* key, std::size_t keyLen,
                      const unsigned char* message, std::size_t messageLen,
                      unsigned char out[Sha1::kDigestBytes])
        {
            unsigned char block[Sha1::kBlockBytes] = {};

            if (keyLen > Sha1::kBlockBytes)
            {
                Sha1::Hash(key, keyLen, block);
            }
            else if (keyLen > 0)
            {
                std::memcpy(block, key, keyLen);
            }

            unsigned char inner[Sha1::kBlockBytes];
            unsigned char outer[Sha1::kBlockBytes];
            for (std::size_t i = 0; i < Sha1::kBlockBytes; ++i)
            {
                inner[i] = static_cast<unsigned char>(block[i] ^ 0x36);
                outer[i] = static_cast<unsigned char>(block[i] ^ 0x5C);
            }

            unsigned char innerDigest[Sha1::kDigestBytes];
            {
                Sha1 h;
                h.Update(inner, sizeof(inner));
                h.Update(message, messageLen);
                h.Final(innerDigest);

                Sha1 g;
                g.Update(outer, sizeof(outer));
                g.Update(innerDigest, sizeof(innerDigest));
                g.Final(out);
            }

            sodium_memzero(block, sizeof(block));
            sodium_memzero(inner, sizeof(inner));
            sodium_memzero(outer, sizeof(outer));
            sodium_memzero(innerDigest, sizeof(innerDigest));
            return true;
        }
    }

    bool Hmac(HashAlgorithm algorithm,
              const unsigned char* key, std::size_t keyLen,
              const unsigned char* message, std::size_t messageLen,
              unsigned char (&out)[kMaxDigestBytes])
    {
        if ((!key && keyLen > 0) || (!message && messageLen > 0))
            return false;

        static const unsigned char kNothing = 0;
        const unsigned char* k = key ? key : &kNothing;
        const unsigned char* m = message ? message : &kNothing;

        switch (algorithm)
        {
        case HashAlgorithm::Sha1:
            return HmacSha1(k, keyLen, m, messageLen, out);

        case HashAlgorithm::Sha256:
        {
            crypto_auth_hmacsha256_state st;
            if (crypto_auth_hmacsha256_init(&st, k, keyLen) != 0)
                return false;
            crypto_auth_hmacsha256_update(&st, m, messageLen);
            const bool ok = crypto_auth_hmacsha256_final(&st, out) == 0;
            sodium_memzero(&st, sizeof(st));
            return ok;
        }

        case HashAlgorithm::Sha512:
        {
            crypto_auth_hmacsha512_state st;
            if (crypto_auth_hmacsha512_init(&st, k, keyLen) != 0)
                return false;
            crypto_auth_hmacsha512_update(&st, m, messageLen);
            const bool ok = crypto_auth_hmacsha512_final(&st, out) == 0;
            sodium_memzero(&st, sizeof(st));
            return ok;
        }
        }
        return false;
    }
}
