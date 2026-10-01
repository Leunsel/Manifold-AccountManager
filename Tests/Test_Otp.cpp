#include "TestRunner.h"

#include "Core/Hmac.h"
#include "Core/Otp.h"
#include "Core/Sha1.h"
#include "Generated/OtpKat.h"

#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    std::string Hex(const unsigned char* data, std::size_t len)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (std::size_t i = 0; i < len; ++i)
        {
            out.push_back(digits[data[i] >> 4]);
            out.push_back(digits[data[i] & 0x0F]);
        }
        return out;
    }
}

TEST_CASE(Sha1_MatchesTheRfc3174Vectors)
{
    for (const auto& v : Kat::kSha1)
    {
        Sha1 h;
        const std::size_t len = std::strlen(v.Input);
        for (int i = 0; i < v.Repeat; ++i)
            h.Update(reinterpret_cast<const unsigned char*>(v.Input), len);

        unsigned char digest[Sha1::kDigestBytes];
        h.Final(digest);
        CHECK_MSG(std::memcmp(digest, v.Digest, sizeof(digest)) == 0,
                  std::string("SHA-1(\"") + (len > 12 ? std::string(v.Input, 12) + "..."
                                                      : std::string(v.Input)) +
                      "\" x" + std::to_string(v.Repeat) + ") = " +
                      Hex(digest, sizeof(digest)) + ", expected " +
                      Hex(v.Digest, sizeof(v.Digest)));
    }
}

TEST_CASE(Sha1_StreamingMatchesOneShotAtEveryBoundary)
{
    std::vector<unsigned char> data(200);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<unsigned char>(i * 7 + 3);

    for (std::size_t len = 0; len <= data.size(); ++len)
    {
        unsigned char oneShot[Sha1::kDigestBytes];
        Sha1::Hash(data.data(), len, oneShot);

        for (std::size_t split = 0; split <= len; ++split)
        {
            Sha1 h;
            h.Update(data.data(), split);
            h.Update(data.data() + split, len - split);
            unsigned char piecewise[Sha1::kDigestBytes];
            h.Final(piecewise);
            CHECK_MSG(std::memcmp(oneShot, piecewise, sizeof(oneShot)) == 0,
                      "length " + std::to_string(len) + ", split at " +
                          std::to_string(split) + ": piecewise differs");
            if (std::memcmp(oneShot, piecewise, sizeof(oneShot)) != 0)
                return;
        }
    }
}

TEST_CASE(Sha1_EmptyAndResetBehaveSensibly)
{
    unsigned char a[Sha1::kDigestBytes];
    Sha1::Hash(nullptr, 0, a);
    CHECK(Hex(a, sizeof(a)) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    Sha1 h;
    h.Update(reinterpret_cast<const unsigned char*>("abc"), 3);
    unsigned char first[Sha1::kDigestBytes];
    h.Final(first);
    h.Reset();
    unsigned char second[Sha1::kDigestBytes];
    h.Final(second);
    CHECK_BYTES_EQ(second, a, sizeof(a));
}

TEST_CASE(Sha1_DestructorWipesItsState)
{
    alignas(Sha1) unsigned char storage[sizeof(Sha1)];
    std::memset(storage, 0, sizeof(storage));

    unsigned char pattern[Sha1::kBlockBytes];
    std::memset(pattern, 0xA5, sizeof(pattern));

    Sha1* h = new (static_cast<void*>(storage)) Sha1();
    h->Update(pattern, sizeof(pattern));

    bool anythingSet = false;
    for (unsigned char b : storage)
        if (b != 0) { anythingSet = true; break; }
    CHECK_MSG(anythingSet, "the probe is worthless: the state was already empty");

    h->~Sha1();

    std::size_t survivors = 0;
    for (unsigned char b : storage)
        if (b != 0) ++survivors;
    CHECK_MSG(survivors == 0,
              std::to_string(survivors) + " of " + std::to_string(sizeof(storage)) +
                  " bytes of the SHA-1 state survived the destructor");
}

TEST_CASE(Sha1_ResetLeavesNoTraceOfThePreviousMessage)
{
    Sha1 h;
    unsigned char secret[Sha1::kBlockBytes];
    std::memset(secret, 0x5C, sizeof(secret));
    h.Update(secret, sizeof(secret));
    unsigned char afterSecret[Sha1::kDigestBytes];
    h.Final(afterSecret);

    h.Reset();
    h.Update(reinterpret_cast<const unsigned char*>("abc"), 3);
    unsigned char afterReset[Sha1::kDigestBytes];
    h.Final(afterReset);

    unsigned char fresh[Sha1::kDigestBytes];
    Sha1::Hash(reinterpret_cast<const unsigned char*>("abc"), 3, fresh);
    CHECK_BYTES_EQ(afterReset, fresh, sizeof(fresh));
}

TEST_CASE(Hmac_MatchesTheRfc2202Vectors)
{
    int index = 0;
    for (const auto& v : Kat::kHmacSha1)
    {
        ++index;
        unsigned char out[kMaxDigestBytes] = {};
        CHECK(Hmac(HashAlgorithm::Sha1, v.Key, v.KeyLen, v.Data, v.DataLen, out));
        CHECK_MSG(std::memcmp(out, v.Digest, sizeof(v.Digest)) == 0,
                  "RFC 2202 case " + std::to_string(index) + " (key " +
                      std::to_string(v.KeyLen) + " bytes): " +
                      Hex(out, sizeof(v.Digest)) + " instead of " +
                      Hex(v.Digest, sizeof(v.Digest)));
    }
}

TEST_CASE(Hmac_OverlongKeysGoThroughTheHashFirst)
{
    std::vector<unsigned char> longKey(80, 0xAA);
    unsigned char hashedKey[Sha1::kDigestBytes];
    Sha1::Hash(longKey.data(), longKey.size(), hashedKey);

    const char* message = "Test Using Larger Than Block-Size Key - Hash Key First";
    const auto* msg = reinterpret_cast<const unsigned char*>(message);

    unsigned char withLong[kMaxDigestBytes] = {};
    unsigned char withHashed[kMaxDigestBytes] = {};
    CHECK(Hmac(HashAlgorithm::Sha1, longKey.data(), longKey.size(),
               msg, std::strlen(message), withLong));
    CHECK(Hmac(HashAlgorithm::Sha1, hashedKey, sizeof(hashedKey),
               msg, std::strlen(message), withHashed));
    CHECK_BYTES_EQ(withLong, withHashed, Sha1::kDigestBytes);

    std::vector<unsigned char> exact(Sha1::kBlockBytes, 0xAA);
    unsigned char hashedExact[Sha1::kDigestBytes];
    Sha1::Hash(exact.data(), exact.size(), hashedExact);
    unsigned char withExact[kMaxDigestBytes] = {};
    unsigned char withExactHashed[kMaxDigestBytes] = {};
    CHECK(Hmac(HashAlgorithm::Sha1, exact.data(), exact.size(),
               msg, std::strlen(message), withExact));
    CHECK(Hmac(HashAlgorithm::Sha1, hashedExact, sizeof(hashedExact),
               msg, std::strlen(message), withExactHashed));
    CHECK_MSG(std::memcmp(withExact, withExactHashed, Sha1::kDigestBytes) != 0,
              "a key of exactly block size was hashed by mistake");
}

TEST_CASE(Hmac_DigestLengthsAndNamesAreConsistent)
{
    CHECK_EQ(DigestBytes(HashAlgorithm::Sha1), std::size_t{ 20 });
    CHECK_EQ(DigestBytes(HashAlgorithm::Sha256), std::size_t{ 32 });
    CHECK_EQ(DigestBytes(HashAlgorithm::Sha512), std::size_t{ 64 });
    CHECK(DigestBytes(HashAlgorithm::Sha512) <= kMaxDigestBytes);

    CHECK(std::strcmp(AlgorithmName(HashAlgorithm::Sha1), "SHA1") == 0);
    CHECK(std::strcmp(AlgorithmName(HashAlgorithm::Sha256), "SHA256") == 0);
    CHECK(std::strcmp(AlgorithmName(HashAlgorithm::Sha512), "SHA512") == 0);

    HashAlgorithm a = HashAlgorithm::Sha512;
    CHECK(ParseAlgorithm("SHA1", a) && a == HashAlgorithm::Sha1);
    CHECK(ParseAlgorithm("sha1", a) && a == HashAlgorithm::Sha1);
    CHECK(ParseAlgorithm("SHA-1", a) && a == HashAlgorithm::Sha1);
    CHECK(ParseAlgorithm("Sha256", a) && a == HashAlgorithm::Sha256);
    CHECK(ParseAlgorithm("SHA512", a) && a == HashAlgorithm::Sha512);

    CHECK(!ParseAlgorithm("SHA224", a));
    CHECK(!ParseAlgorithm("SHA384", a));
    CHECK(!ParseAlgorithm("MD5", a));
    CHECK(!ParseAlgorithm("", a));
    CHECK(!ParseAlgorithm(nullptr, a));
    CHECK(!ParseAlgorithm("SHA1111111111111111111111", a));
}

TEST_CASE(Hmac_Sha256AndSha512AgreeWithLibsodium)
{
    const unsigned char key[] = { 'k', 'e', 'y' };
    const unsigned char other[] = { 'K', 'e', 'y' };
    const auto* msg = reinterpret_cast<const unsigned char*>("nachricht");
    const std::size_t msgLen = 9;

    for (HashAlgorithm alg : { HashAlgorithm::Sha256, HashAlgorithm::Sha512 })
    {
        unsigned char a[kMaxDigestBytes] = {};
        unsigned char b[kMaxDigestBytes] = {};
        CHECK(Hmac(alg, key, sizeof(key), msg, msgLen, a));
        CHECK(Hmac(alg, other, sizeof(other), msg, msgLen, b));
        CHECK(std::memcmp(a, b, DigestBytes(alg)) != 0);

        unsigned char again[kMaxDigestBytes] = {};
        CHECK(Hmac(alg, key, sizeof(key), msg, msgLen, again));
        CHECK_BYTES_EQ(a, again, DigestBytes(alg));
    }
}

TEST_CASE(Otp_TruncationMatchesRfc4226)
{
    int withHighBitSet = 0;
    for (const auto& v : Kat::kHotp)
    {
        const std::uint32_t got = TruncateDynamic(v.Hmac, sizeof(v.Hmac));
        CHECK_MSG(got == v.Truncated,
                  "counter " + std::to_string(v.Counter) + ": truncated " +
                      std::to_string(got) + " instead of " + std::to_string(v.Truncated));

        const std::size_t offset = v.Hmac[sizeof(v.Hmac) - 1] & 0x0F;
        if ((v.Hmac[offset] & 0x80u) != 0)
            ++withHighBitSet;
    }
    CHECK_MSG(withHighBitSet == 4,
              "expected 4 rows with the high bit set, found " +
                  std::to_string(withHighBitSet) +
                  " -- without them the mask proves nothing");
}

TEST_CASE(Otp_TruncationReadsFromTheOffsetInTheLastByte)
{
    unsigned char digest[20] = {};
    digest[5] = 0x7F; digest[6] = 0xFF; digest[7] = 0xFF; digest[8] = 0xFF;
    digest[19] = 0x05;
    CHECK_EQ(TruncateDynamic(digest, sizeof(digest)), 0x7FFFFFFFu);

    digest[19] = 0x00;
    CHECK(TruncateDynamic(digest, sizeof(digest)) != 0x7FFFFFFFu);

    unsigned char high[20] = {};
    high[0] = 0xFF; high[1] = 0xFF; high[2] = 0xFF; high[3] = 0xFF;
    high[19] = 0x00;
    CHECK_MSG(TruncateDynamic(high, sizeof(high)) == 0x7FFFFFFFu,
              "the 0x7F mask is missing");

    CHECK_EQ(TruncateDynamic(nullptr, 20), 0u);
    CHECK_EQ(TruncateDynamic(high, 19), 0u);
}

TEST_CASE(Otp_CounterIsSerialisedBigEndian)
{
    unsigned char out[8] = {};
    SerialiseCounter(1, out);
    const unsigned char one[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    CHECK_BYTES_EQ(out, one, sizeof(one));

    SerialiseCounter(0x0123456789ABCDEFull, out);
    const unsigned char pattern[8] =
        { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    CHECK_BYTES_EQ(out, pattern, sizeof(pattern));

    SerialiseCounter(1111111109ull / 30ull, out);
    const unsigned char t[8] = { 0, 0, 0, 0, 0x02, 0x35, 0x23, 0xEC };
    CHECK_BYTES_EQ(out, t, sizeof(t));
}

TEST_CASE(Otp_HotpMatchesRfc4226)
{
    const auto* key = reinterpret_cast<const unsigned char*>(Kat::kHotpSecret);
    const std::size_t keyLen = sizeof(Kat::kHotpSecret) - 1;

    for (const auto& v : Kat::kHotp)
    {
        const auto code = Hotp(key, keyLen, v.Counter, 6, HashAlgorithm::Sha1);
        CHECK(code.IsOk());
        if (!code.IsOk())
            continue;
        CHECK_MSG(code.Value() == v.Code,
                  "counter " + std::to_string(v.Counter) + ": " + code.Value() +
                      " instead of " + v.Code);
    }
}

TEST_CASE(Otp_CodesAreZeroPaddedToTheirFullWidth)
{
    const auto* key = reinterpret_cast<const unsigned char*>(Kat::kHotpSecret);
    const std::size_t keyLen = sizeof(Kat::kHotpSecret) - 1;

    int leadingZeros = 0;
    for (std::uint8_t digits = kMinDigits; digits <= kMaxDigits; ++digits)
    {
        for (std::uint64_t counter = 0; counter < 300; ++counter)
        {
            const auto code = Hotp(key, keyLen, counter, digits, HashAlgorithm::Sha1);
            CHECK(code.IsOk());
            if (!code.IsOk())
                return;
            CHECK_MSG(code.Value().size() == digits,
                      "counter " + std::to_string(counter) + ": length " +
                          std::to_string(code.Value().size()) + " instead of " +
                          std::to_string(digits));
            for (char c : code.Value())
                if (c < '0' || c > '9')
                {
                    CHECK_MSG(false, "non-digit in the code: " + code.Value());
                    return;
                }
            if (code.Value()[0] == '0')
                ++leadingZeros;
        }
    }
    CHECK_MSG(leadingZeros > 0,
              "not a single leading zero in 900 codes -- so this "
              "test does not check the padding");
}

namespace
{
    void SeedFor(HashAlgorithm alg, const unsigned char*& seed, std::size_t& len)
    {
        seed = Kat::kTotpSeedSHA1;
        len = sizeof(Kat::kTotpSeedSHA1);
        if (alg == HashAlgorithm::Sha256)
        {
            seed = Kat::kTotpSeedSHA256;
            len = sizeof(Kat::kTotpSeedSHA256);
        }
        else if (alg == HashAlgorithm::Sha512)
        {
            seed = Kat::kTotpSeedSHA512;
            len = sizeof(Kat::kTotpSeedSHA512);
        }
    }
}

TEST_CASE(Otp_TotpMatchesRfc6238)
{
    for (const auto& v : Kat::kTotp)
    {
        HashAlgorithm alg = HashAlgorithm::Sha1;
        CHECK(ParseAlgorithm(v.Algorithm, alg));

        const unsigned char* seed = nullptr;
        std::size_t seedLen = 0;
        SeedFor(alg, seed, seedLen);

        const auto code = Totp(seed, seedLen,
                               static_cast<std::int64_t>(v.UnixTime), 30, 8, alg);
        CHECK(code.IsOk());
        if (!code.IsOk())
            continue;
        CHECK_MSG(code.Value() == v.Code,
                  std::string(v.Algorithm) + " at t=" + std::to_string(v.UnixTime) +
                      ": " + code.Value() + " instead of " + v.Code);
    }
}

TEST_CASE(Otp_TheThreeSeedsReallyDiffer)
{
    static_assert(sizeof(Kat::kTotpSeedSHA1) == 20, "SHA-1 seed must be 20 bytes");
    static_assert(sizeof(Kat::kTotpSeedSHA256) == 32, "SHA-256 seed must be 32 bytes");
    static_assert(sizeof(Kat::kTotpSeedSHA512) == 64, "SHA-512 seed must be 64 bytes");

    int checked = 0;
    for (const auto& v : Kat::kTotp)
    {
        if (std::strcmp(v.Algorithm, "SHA256") != 0)
            continue;
        ++checked;
        const auto wrong = Totp(Kat::kTotpSeedSHA1, sizeof(Kat::kTotpSeedSHA1),
                                static_cast<std::int64_t>(v.UnixTime), 30, 8,
                                HashAlgorithm::Sha256);
        CHECK(wrong.IsOk());
        CHECK_MSG(wrong.Value() != v.Code,
                  "the 20-byte seed reproduces a SHA256 row -- so "
                  "the vectors do not check the seed choice at all");
    }
    CHECK(checked >= 6);
}

TEST_CASE(Otp_CodeIsConstantWithinAStepAndChangesAtTheBoundary)
{
    const unsigned char* seed = Kat::kTotpSeedSHA1;
    const std::size_t seedLen = sizeof(Kat::kTotpSeedSHA1);
    const std::uint32_t period = 30;

    for (std::int64_t base : { std::int64_t{ 0 }, std::int64_t{ 59 },
                               std::int64_t{ 1111111109 },
                               std::int64_t{ 2000000000 } })
    {
        const std::int64_t start = base - (base % period);
        const auto atStart = Totp(seed, seedLen, start, period, 6);
        CHECK(atStart.IsOk());
        if (!atStart.IsOk())
            return;

        for (std::int64_t t = start; t < start + period; ++t)
        {
            const auto here = Totp(seed, seedLen, t, period, 6);
            CHECK(here.IsOk());
            CHECK_MSG(here.Value() == atStart.Value(),
                      "t=" + std::to_string(t) + " differs within the window");
        }

        const auto next = Totp(seed, seedLen, start + period, period, 6);
        CHECK(next.IsOk());
        CHECK_MSG(next.Value() != atStart.Value(),
                  "the code does not change at the window boundary");
    }
}

TEST_CASE(Otp_SecondsRemainingCountsDownAndNeverReachesZero)
{
    CHECK_EQ(SecondsRemaining(0, 30), 30u);
    CHECK_EQ(SecondsRemaining(1, 30), 29u);
    CHECK_EQ(SecondsRemaining(29, 30), 1u);
    CHECK_EQ(SecondsRemaining(30, 30), 30u);

    for (std::int64_t t = 0; t < 300; ++t)
    {
        const std::uint32_t left = SecondsRemaining(t, 30);
        CHECK_MSG(left >= 1 && left <= 30,
                  "t=" + std::to_string(t) + ": " + std::to_string(left) + " s");
    }

    CHECK_EQ(SecondsRemaining(-1, 30), 0u);
    CHECK_EQ(SecondsRemaining(0, 0), 0u);
}

TEST_CASE(Otp_RejectsImpossibleDigitsAndPeriod)
{
    const unsigned char key[] = { 'k' };

    for (int digits : { 0, 1, 5, 9, 10, 255 })
    {
        const auto r = Totp(key, sizeof(key), 59, 30, static_cast<std::uint8_t>(digits));
        CHECK_MSG(!r.IsOk(),
                  "digits " + std::to_string(digits) + " was accepted");
        if (!r.IsOk())
            CHECK(r.Code() == ErrorCode::InvalidArgument);
    }
    for (int digits : { 6, 7, 8 })
        CHECK_MSG(Totp(key, sizeof(key), 59, 30, static_cast<std::uint8_t>(digits)).IsOk(),
                  "digits " + std::to_string(digits) + " was rejected");

    CHECK(!Totp(key, sizeof(key), 59, 0, 6).IsOk());
    CHECK(!Totp(key, sizeof(key), 59, 601, 6).IsOk());
    CHECK(Totp(key, sizeof(key), 59, 1, 6).IsOk());
    CHECK(Totp(key, sizeof(key), 59, 600, 6).IsOk());

    CHECK(!Totp(key, sizeof(key), -1, 30, 6).IsOk());
}

TEST_CASE(Otp_TheRowBeyond2038IsStillInTheVectors)
{
    bool found = false;
    for (const auto& v : Kat::kTotp)
        if (v.UnixTime > 4000000000ull)
            found = true;
    CHECK_MSG(found, "the row beyond 2038 was removed from the vectors");
}
