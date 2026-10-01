#include "Core/Crypto.h"

#include "Generated/CryptoKat.h"

#include <sodium.h>

#include <atomic>
#include <cstring>

namespace Bunker::Crypto
{
    namespace
    {
        std::atomic<bool> g_ready{ false };

        static_assert(kKeyBytes == crypto_aead_xchacha20poly1305_ietf_KEYBYTES, "key size drift");
        static_assert(kNonceBytes == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, "nonce size drift");
        static_assert(kTagBytes == crypto_aead_xchacha20poly1305_ietf_ABYTES, "tag size drift");
        static_assert(kSaltBytes == crypto_pwhash_SALTBYTES, "salt size drift");
        static_assert(kKdfContextBytes == crypto_kdf_CONTEXTBYTES, "kdf context size drift");

        bool Same(const unsigned char* a, const unsigned char* b, std::size_t n)
        {
            return sodium_memcmp(a, b, n) == 0;
        }

        bool KatBlake2b()
        {
            unsigned char out[64] = {};
            const unsigned char abc[3] = { 'a', 'b', 'c' };
            if (crypto_generichash(out, sizeof(out), abc, sizeof(abc), nullptr, 0) != 0)
                return false;
            return Same(out, Kat::kBlake2bAbc, sizeof(out));
        }

        bool KatChaCha20Poly1305()
        {
            unsigned char cipher[sizeof(Kat::kC20Plain) + crypto_aead_chacha20poly1305_ietf_ABYTES] = {};
            unsigned long long cipherLen = 0;
            if (crypto_aead_chacha20poly1305_ietf_encrypt(
                    cipher, &cipherLen,
                    Kat::kC20Plain, sizeof(Kat::kC20Plain),
                    Kat::kC20Aad, sizeof(Kat::kC20Aad),
                    nullptr, Kat::kC20Nonce, Kat::kC20Key) != 0)
                return false;
            if (cipherLen != sizeof(cipher))
                return false;
            if (!Same(cipher, Kat::kC20Cipher, sizeof(Kat::kC20Cipher)))
                return false;
            return Same(cipher + sizeof(Kat::kC20Cipher), Kat::kC20Tag, sizeof(Kat::kC20Tag));
        }

        bool KatXChaCha20Poly1305()
        {
            unsigned char cipher[sizeof(Kat::kXC20Plain) + kTagBytes] = {};
            unsigned long long cipherLen = 0;
            if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                    cipher, &cipherLen,
                    Kat::kXC20Plain, sizeof(Kat::kXC20Plain),
                    Kat::kXC20Aad, sizeof(Kat::kXC20Aad),
                    nullptr, Kat::kXC20Nonce, Kat::kXC20Key) != 0)
                return false;
            if (cipherLen != sizeof(cipher))
                return false;
            if (!Same(cipher, Kat::kXC20Cipher, sizeof(Kat::kXC20Cipher)))
                return false;
            if (!Same(cipher + sizeof(Kat::kXC20Cipher), Kat::kXC20Tag, sizeof(Kat::kXC20Tag)))
                return false;

            unsigned char plain[sizeof(Kat::kXC20Plain)] = {};
            unsigned long long plainLen = 0;
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                    plain, &plainLen, nullptr,
                    cipher, cipherLen,
                    Kat::kXC20Aad, sizeof(Kat::kXC20Aad),
                    Kat::kXC20Nonce, Kat::kXC20Key) != 0)
                return false;
            if (plainLen != sizeof(plain) || !Same(plain, Kat::kXC20Plain, sizeof(plain)))
                return false;

            unsigned char tampered[sizeof(cipher)];
            std::memcpy(tampered, cipher, sizeof(cipher));
            tampered[0] ^= 0x01;
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                    plain, &plainLen, nullptr,
                    tampered, cipherLen,
                    Kat::kXC20Aad, sizeof(Kat::kXC20Aad),
                    Kat::kXC20Nonce, Kat::kXC20Key) == 0)
                return false;

            unsigned char aad[sizeof(Kat::kXC20Aad)];
            std::memcpy(aad, Kat::kXC20Aad, sizeof(aad));
            aad[0] ^= 0x01;
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                    plain, &plainLen, nullptr,
                    cipher, cipherLen,
                    aad, sizeof(aad),
                    Kat::kXC20Nonce, Kat::kXC20Key) == 0)
                return false;

            return true;
        }

        bool KatArgon2idDeterminism()
        {
            const char pw[] = "correct horse battery staple";
            unsigned char salt1[crypto_pwhash_SALTBYTES] = {};
            unsigned char salt2[crypto_pwhash_SALTBYTES] = {};
            std::memset(salt1, 0x11, sizeof(salt1));
            std::memset(salt2, 0x11, sizeof(salt2));
            salt2[0] = 0x12;

            unsigned char a[32] = {}, b[32] = {}, c[32] = {};
            const unsigned long long ops = crypto_pwhash_OPSLIMIT_INTERACTIVE;
            const std::size_t mem = crypto_pwhash_MEMLIMIT_INTERACTIVE;

            if (crypto_pwhash(a, sizeof(a), pw, sizeof(pw) - 1, salt1, ops, mem,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) return false;
            if (crypto_pwhash(b, sizeof(b), pw, sizeof(pw) - 1, salt1, ops, mem,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) return false;
            if (crypto_pwhash(c, sizeof(c), pw, sizeof(pw) - 1, salt2, ops, mem,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) return false;

            if (!Same(a, b, sizeof(a)))
                return false;
            if (Same(a, c, sizeof(a)))
                return false;
            return true;
        }

        bool KatKdfSubkeys()
        {
            unsigned char master[crypto_kdf_KEYBYTES] = {};
            std::memset(master, 0x42, sizeof(master));
            unsigned char s1[32] = {}, s2[32] = {}, s1again[32] = {};
            const char ctx[crypto_kdf_CONTEXTBYTES] = { 'B','u','n','k','e','r','0','1' };

            if (crypto_kdf_derive_from_key(s1, sizeof(s1), 1, ctx, master) != 0) return false;
            if (crypto_kdf_derive_from_key(s2, sizeof(s2), 2, ctx, master) != 0) return false;
            if (crypto_kdf_derive_from_key(s1again, sizeof(s1again), 1, ctx, master) != 0) return false;

            if (!Same(s1, s1again, sizeof(s1)))
                return false;
            if (Same(s1, s2, sizeof(s1)))
                return false;
            return true;
        }

        bool KatSecureMemory()
        {
            unsigned char x[32];
            std::memset(x, 0xAB, sizeof(x));
            sodium_memzero(x, sizeof(x));
            for (unsigned char b : x)
                if (b != 0)
                    return false;

            unsigned char p[16], q[16];
            std::memset(p, 0x5A, sizeof(p));
            std::memset(q, 0x5A, sizeof(q));
            if (sodium_memcmp(p, q, sizeof(p)) != 0)
                return false;
            q[15] ^= 0x01;
            return sodium_memcmp(p, q, sizeof(p)) != 0;
        }
    }

    KdfParams KdfParams::Moderate()
    {
        KdfParams p;
        p.OpsLimit = crypto_pwhash_OPSLIMIT_MODERATE;
        p.MemLimit = crypto_pwhash_MEMLIMIT_MODERATE;
        return p;
    }

    KdfParams KdfParams::Interactive()
    {
        KdfParams p;
        p.OpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
        p.MemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;
        return p;
    }

    KdfParams KdfParams::TestOnlyMinimum()
    {
        KdfParams p;
        p.OpsLimit = crypto_pwhash_OPSLIMIT_MIN;
        p.MemLimit = crypto_pwhash_MEMLIMIT_MIN;
        return p;
    }

    bool KdfParams::IsSane() const
    {
        if (OpsLimit < crypto_pwhash_OPSLIMIT_MIN || OpsLimit > 16)
            return false;
        if (MemLimit < crypto_pwhash_MEMLIMIT_MIN)
            return false;
        return MemLimit <= (std::uint64_t{ 2 } * 1024 * 1024 * 1024);
    }

    Status Initialise()
    {
        if (sodium_init() < 0)
            return Fail(ErrorCode::NotInitialised, "sodium_init failed");
        const Status st = RunSelfTest();
        if (!st.IsOk())
            return st;
        g_ready.store(true, std::memory_order_release);
        return Ok();
    }

    bool IsReady() { return g_ready.load(std::memory_order_acquire); }

    Status RunSelfTest()
    {
        if (!KatSecureMemory())
            return Fail(ErrorCode::CryptoSelfTestFailed, "secure memory primitives");
        if (!KatBlake2b())
            return Fail(ErrorCode::CryptoSelfTestFailed, "BLAKE2b RFC 7693");
        if (!KatChaCha20Poly1305())
            return Fail(ErrorCode::CryptoSelfTestFailed, "ChaCha20-Poly1305 RFC 8439");
        if (!KatXChaCha20Poly1305())
            return Fail(ErrorCode::CryptoSelfTestFailed, "XChaCha20-Poly1305 draft A.3.1");
        if (!KatKdfSubkeys())
            return Fail(ErrorCode::CryptoSelfTestFailed, "crypto_kdf subkey derivation");
        if (!KatArgon2idDeterminism())
            return Fail(ErrorCode::CryptoSelfTestFailed, "Argon2id determinism");
        return Ok();
    }

    void RandomBytes(unsigned char* out, std::size_t len)
    {
        if (out && len)
            randombytes_buf(out, len);
    }

    Status DeriveKeyFromPassword(SecureBuffer& outKey,
                                 const char* password, std::size_t passwordLen,
                                 const unsigned char salt[kSaltBytes],
                                 const KdfParams& params)
    {
        if (!IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!password || !salt || !params.IsSane())
            return Fail(ErrorCode::InvalidArgument, "bad KDF arguments");

        outKey = SecureBuffer(kKeyBytes);
        if (!outKey.IsValid())
            return Fail(ErrorCode::OutOfMemory, "key buffer");

        if (crypto_pwhash(outKey.Data(), kKeyBytes,
                          password, passwordLen, salt,
                          params.OpsLimit, static_cast<std::size_t>(params.MemLimit),
                          crypto_pwhash_ALG_ARGON2ID13) != 0)
        {
            outKey.Reset();
            return Fail(ErrorCode::KdfFailed, "Argon2id could not allocate");
        }
        return Ok();
    }

    Status DeriveSubkey(SecureBuffer& outKey, std::size_t outLen,
                        std::uint64_t subkeyId,
                        const char context[kKdfContextBytes],
                        const SecureBuffer& masterKey)
    {
        if (!IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!context || !masterKey.IsValid() || masterKey.Size() != crypto_kdf_KEYBYTES)
            return Fail(ErrorCode::InvalidArgument, "bad subkey arguments");
        if (outLen < crypto_kdf_BYTES_MIN || outLen > crypto_kdf_BYTES_MAX)
            return Fail(ErrorCode::InvalidArgument, "subkey length out of range");

        outKey = SecureBuffer(outLen);
        if (!outKey.IsValid())
            return Fail(ErrorCode::OutOfMemory, "subkey buffer");

        if (crypto_kdf_derive_from_key(outKey.Data(), outLen, subkeyId, context,
                                       masterKey.Data()) != 0)
        {
            outKey.Reset();
            return Fail(ErrorCode::KdfFailed, "crypto_kdf_derive_from_key");
        }
        return Ok();
    }

    Status Encrypt(unsigned char* out, std::size_t* outLen,
                   const unsigned char* plain, std::size_t plainLen,
                   const unsigned char* aad, std::size_t aadLen,
                   const unsigned char nonce[kNonceBytes],
                   const SecureBuffer& key)
    {
        if (!IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!out || !outLen || !nonce || !key.IsValid() || key.Size() != kKeyBytes)
            return Fail(ErrorCode::InvalidArgument, "bad encrypt arguments");

        unsigned long long produced = 0;
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                out, &produced, plain, plainLen, aad, aadLen,
                nullptr, nonce, key.Data()) != 0)
            return Fail(ErrorCode::EncryptFailed, "aead encrypt");

        *outLen = static_cast<std::size_t>(produced);
        return Ok();
    }

    Status Decrypt(unsigned char* out, std::size_t* outLen,
                   const unsigned char* cipher, std::size_t cipherLen,
                   const unsigned char* aad, std::size_t aadLen,
                   const unsigned char nonce[kNonceBytes],
                   const SecureBuffer& key)
    {
        if (!IsReady())
            return Fail(ErrorCode::NotInitialised, "crypto not initialised");
        if (!out || !outLen || !nonce || !key.IsValid() || key.Size() != kKeyBytes)
            return Fail(ErrorCode::InvalidArgument, "bad decrypt arguments");
        if (cipherLen < kTagBytes)
            return Fail(ErrorCode::DecryptFailed, "ciphertext shorter than the tag");

        unsigned long long produced = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                out, &produced, nullptr, cipher, cipherLen, aad, aadLen,
                nonce, key.Data()) != 0)
            return Fail(ErrorCode::DecryptFailed, "authentication failed");

        *outLen = static_cast<std::size_t>(produced);
        return Ok();
    }

    Status Hash(unsigned char* out, std::size_t outLen,
                const unsigned char* in, std::size_t inLen)
    {
        if (!out || outLen < crypto_generichash_BYTES_MIN || outLen > crypto_generichash_BYTES_MAX)
            return Fail(ErrorCode::InvalidArgument, "bad hash length");
        if (crypto_generichash(out, outLen, in, inLen, nullptr, 0) != 0)
            return Fail(ErrorCode::EncryptFailed, "generichash");
        return Ok();
    }
}
