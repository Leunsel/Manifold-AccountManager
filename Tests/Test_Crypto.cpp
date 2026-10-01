#include "TestRunner.h"

#include "Core/Crypto.h"
#include "Core/Endian.h"
#include "Core/Result.h"
#include "Core/Secret.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    void Ready()
    {
        static bool done = false;
        if (!done)
        {
            const Status st = Crypto::Initialise();
            CHECK_MSG(st.IsOk(), std::string("Initialise: ") +
                                 (st.GetError().Detail ? st.GetError().Detail : "?"));
            done = true;
        }
    }
}

TEST_CASE(Crypto_SelfTestPasses)
{
    Ready();
    CHECK(Crypto::IsReady());
    CHECK(Crypto::RunSelfTest().IsOk());
}

TEST_CASE(Crypto_RoundTripWithAad)
{
    Ready();

    SecureBuffer key(Crypto::kKeyBytes);
    CHECK(key.IsValid());
    Crypto::RandomBytes(key.Data(), key.Size());

    unsigned char nonce[Crypto::kNonceBytes];
    Crypto::RandomBytes(nonce, sizeof(nonce));

    const std::string plain = "Sehr secret: aeoeue 42 EUR";
    const unsigned char aad[] = { 'h', 'e', 'a', 'd', 'e', 'r' };

    std::vector<unsigned char> cipher(plain.size() + Crypto::kTagBytes);
    std::size_t cipherLen = 0;
    CHECK(Crypto::Encrypt(cipher.data(), &cipherLen,
                          reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
                          aad, sizeof(aad), nonce, key).IsOk());
    CHECK_EQ(cipherLen, plain.size() + Crypto::kTagBytes);

    std::vector<unsigned char> back(plain.size());
    std::size_t backLen = 0;
    CHECK(Crypto::Decrypt(back.data(), &backLen, cipher.data(), cipherLen,
                          aad, sizeof(aad), nonce, key).IsOk());
    CHECK_EQ(backLen, plain.size());
    CHECK_BYTES_EQ(back.data(), plain.data(), plain.size());
}

TEST_CASE(Crypto_TamperedCiphertextIsRejected)
{
    Ready();

    SecureBuffer key(Crypto::kKeyBytes);
    Crypto::RandomBytes(key.Data(), key.Size());
    unsigned char nonce[Crypto::kNonceBytes];
    Crypto::RandomBytes(nonce, sizeof(nonce));

    const unsigned char plain[64] = {};
    std::vector<unsigned char> cipher(sizeof(plain) + Crypto::kTagBytes);
    std::size_t cipherLen = 0;
    CHECK(Crypto::Encrypt(cipher.data(), &cipherLen, plain, sizeof(plain),
                          nullptr, 0, nonce, key).IsOk());

    std::vector<unsigned char> out(sizeof(plain));
    std::size_t outLen = 0;
    int accepted = 0;
    for (std::size_t i = 0; i < cipherLen; ++i)
    {
        std::vector<unsigned char> bad(cipher.begin(), cipher.begin() + cipherLen);
        bad[i] ^= 0x01;
        if (Crypto::Decrypt(out.data(), &outLen, bad.data(), cipherLen,
                            nullptr, 0, nonce, key).IsOk())
            ++accepted;
    }
    CHECK_MSG(accepted == 0,
              "the AEAD accepted " + std::to_string(accepted) + " tampered ciphertexts");
}

TEST_CASE(Crypto_ModifiedAadIsRejected)
{
    Ready();
    SecureBuffer key(Crypto::kKeyBytes);
    Crypto::RandomBytes(key.Data(), key.Size());
    unsigned char nonce[Crypto::kNonceBytes];
    Crypto::RandomBytes(nonce, sizeof(nonce));

    unsigned char aad[16];
    std::memset(aad, 0x33, sizeof(aad));
    const unsigned char plain[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    unsigned char cipher[sizeof(plain) + Crypto::kTagBytes];
    std::size_t cipherLen = 0;
    CHECK(Crypto::Encrypt(cipher, &cipherLen, plain, sizeof(plain),
                          aad, sizeof(aad), nonce, key).IsOk());

    unsigned char out[sizeof(plain)];
    std::size_t outLen = 0;
    for (std::size_t i = 0; i < sizeof(aad); ++i)
    {
        unsigned char bad[sizeof(aad)];
        std::memcpy(bad, aad, sizeof(aad));
        bad[i] ^= 0x80;
        CHECK(!Crypto::Decrypt(out, &outLen, cipher, cipherLen,
                               bad, sizeof(bad), nonce, key).IsOk());
    }
    CHECK(!Crypto::Decrypt(out, &outLen, cipher, cipherLen, nullptr, 0, nonce, key).IsOk());
}

TEST_CASE(Crypto_WrongKeyAndWrongNonceFail)
{
    Ready();
    SecureBuffer key(Crypto::kKeyBytes), other(Crypto::kKeyBytes);
    Crypto::RandomBytes(key.Data(), key.Size());
    Crypto::RandomBytes(other.Data(), other.Size());

    unsigned char nonce[Crypto::kNonceBytes];
    Crypto::RandomBytes(nonce, sizeof(nonce));

    const unsigned char plain[32] = {};
    unsigned char cipher[sizeof(plain) + Crypto::kTagBytes];
    std::size_t cipherLen = 0;
    CHECK(Crypto::Encrypt(cipher, &cipherLen, plain, sizeof(plain),
                          nullptr, 0, nonce, key).IsOk());

    unsigned char out[sizeof(plain)];
    std::size_t outLen = 0;
    CHECK(!Crypto::Decrypt(out, &outLen, cipher, cipherLen, nullptr, 0, nonce, other).IsOk());

    unsigned char badNonce[Crypto::kNonceBytes];
    std::memcpy(badNonce, nonce, sizeof(badNonce));
    badNonce[0] ^= 0x01;
    CHECK(!Crypto::Decrypt(out, &outLen, cipher, cipherLen, nullptr, 0, badNonce, key).IsOk());
}

TEST_CASE(Crypto_Argon2idIsSaltDependent)
{
    Ready();
    const char pw[] = "a password";
    unsigned char salt[Crypto::kSaltBytes];
    std::memset(salt, 0x07, sizeof(salt));

    const Crypto::KdfParams params = Crypto::KdfParams::Interactive();

    SecureBuffer a, b, c;
    CHECK(Crypto::DeriveKeyFromPassword(a, pw, sizeof(pw) - 1, salt, params).IsOk());
    CHECK(Crypto::DeriveKeyFromPassword(b, pw, sizeof(pw) - 1, salt, params).IsOk());
    CHECK_EQ(a.Size(), Crypto::kKeyBytes);
    CHECK(SecretEquals(a.Data(), b.Data(), a.Size()));

    salt[0] ^= 0x01;
    CHECK(Crypto::DeriveKeyFromPassword(c, pw, sizeof(pw) - 1, salt, params).IsOk());
    CHECK(!SecretEquals(a.Data(), c.Data(), a.Size()));
}

TEST_CASE(Crypto_KdfParamsSanityBounds)
{
    Crypto::KdfParams p = Crypto::KdfParams::Moderate();
    CHECK(p.IsSane());

    p.MemLimit = 64ull * 1024 * 1024 * 1024;
    CHECK(!p.IsSane());

    p = Crypto::KdfParams::Moderate();
    p.OpsLimit = 0;
    CHECK(!p.IsSane());

    p = Crypto::KdfParams::Moderate();
    p.OpsLimit = 1000000;
    CHECK(!p.IsSane());
}

TEST_CASE(Crypto_SubkeysAreIndependent)
{
    Ready();
    SecureBuffer master(32);
    Crypto::RandomBytes(master.Data(), master.Size());
    const char ctx[Crypto::kKdfContextBytes] = { 'B','u','n','k','e','r','0','1' };

    SecureBuffer k1, k2, k1again;
    CHECK(Crypto::DeriveSubkey(k1, 32, 1, ctx, master).IsOk());
    CHECK(Crypto::DeriveSubkey(k2, 32, 2, ctx, master).IsOk());
    CHECK(Crypto::DeriveSubkey(k1again, 32, 1, ctx, master).IsOk());

    CHECK(SecretEquals(k1.Data(), k1again.Data(), 32));
    CHECK(!SecretEquals(k1.Data(), k2.Data(), 32));
    CHECK(!SecretEquals(k1.Data(), master.Data(), 32));
}

TEST_CASE(Crypto_RandomBytesAreNotConstant)
{
    Ready();
    unsigned char a[32] = {}, b[32] = {};
    Crypto::RandomBytes(a, sizeof(a));
    Crypto::RandomBytes(b, sizeof(b));
    CHECK(std::memcmp(a, b, sizeof(a)) != 0);

    bool allZero = true;
    for (unsigned char x : a)
        if (x != 0) { allZero = false; break; }
    CHECK(!allZero);
}
