#include "Core/Otp.h"

#include "Core/Base32.h"

#include <sodium.h>

namespace Bunker
{
    namespace
    {
        constexpr std::uint32_t kDigitsPower[] = {
            1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u, 100000000u,
        };
        static_assert(kMaxDigits < sizeof(kDigitsPower) / sizeof(kDigitsPower[0]),
                      "the digit power table must cover kMaxDigits");

        Status ValidateShape(std::uint8_t digits, std::uint32_t period, bool checkPeriod)
        {
            if (digits < kMinDigits || digits > kMaxDigits)
                return Error{ ErrorCode::InvalidArgument,
                              "digit count outside 6 to 8" };
            if (checkPeriod && (period < kMinPeriodSeconds || period > kMaxPeriodSeconds))
                return Error{ ErrorCode::InvalidArgument,
                              "time step outside 1 to 600 seconds" };
            return Status{};
        }
    }

    Result<std::string> TotpFromConfig(const TotpConfig& config, std::int64_t unixSeconds)
    {
        if (config.Digits < kMinDigits || config.Digits > kMaxDigits)
            return Error{ ErrorCode::InvalidArgument, "digit count outside 6 to 8" };

        unsigned char key[kMaxTotpKeyBytes];
        return WithDecodedTotpKey<Result<std::string>>(
            config.SecretBase32, key, sizeof(key),
            [&](const unsigned char* k, std::size_t n) {
                return Totp(k, n, unixSeconds, config.Period,
                            static_cast<std::uint8_t>(config.Digits), config.Algorithm);
            });
    }

    void WipeTotpKey(unsigned char* key, std::size_t n)
    {
        if (key && n)
            sodium_memzero(key, n);
    }

    std::uint32_t TruncateDynamic(const unsigned char* digest, std::size_t digestLen)
    {
        if (!digest || digestLen < 20)
            return 0;

        const std::size_t offset = digest[digestLen - 1] & 0x0Fu;

        const std::uint32_t value =
            (static_cast<std::uint32_t>(digest[offset + 0]) << 24) |
            (static_cast<std::uint32_t>(digest[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(digest[offset + 2]) << 8) |
            (static_cast<std::uint32_t>(digest[offset + 3]));

        return value & 0x7FFFFFFFu;
    }

    void SerialiseCounter(std::uint64_t counter, unsigned char out[8])
    {
        for (int i = 0; i < 8; ++i)
            out[i] = static_cast<unsigned char>((counter >> (56 - i * 8)) & 0xFF);
    }

    Result<std::string> Hotp(const unsigned char* key, std::size_t keyLen,
                             std::uint64_t counter, std::uint8_t digits,
                             HashAlgorithm algorithm)
    {
        const Status shape = ValidateShape(digits, 0, false);
        if (!shape.IsOk())
            return shape.GetError();
        if (!key && keyLen > 0)
            return Error{ ErrorCode::InvalidArgument, "no key given" };

        unsigned char message[8];
        SerialiseCounter(counter, message);

        unsigned char digest[kMaxDigestBytes] = {};
        if (!Hmac(algorithm, key, keyLen, message, sizeof(message), digest))
        {
            sodium_memzero(digest, sizeof(digest));
            return Error{ ErrorCode::EncryptFailed, "HMAC failed" };
        }

        const std::uint32_t truncated = TruncateDynamic(digest, DigestBytes(algorithm));
        sodium_memzero(digest, sizeof(digest));

        std::uint32_t value = truncated % kDigitsPower[digits];

        std::string code(digits, '0');
        for (std::size_t i = digits; i-- > 0; )
        {
            code[i] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        }
        return code;
    }

    Result<std::string> Totp(const unsigned char* key, std::size_t keyLen,
                             std::int64_t unixSeconds, std::uint32_t period,
                             std::uint8_t digits, HashAlgorithm algorithm)
    {
        const Status shape = ValidateShape(digits, period, true);
        if (!shape.IsOk())
            return shape.GetError();
        if (unixSeconds < 0)
            return Error{ ErrorCode::InvalidArgument, "system clock is before 1970" };

        const std::uint64_t counter =
            static_cast<std::uint64_t>(unixSeconds) / static_cast<std::uint64_t>(period);
        return Hotp(key, keyLen, counter, digits, algorithm);
    }

    std::uint32_t SecondsRemaining(std::int64_t unixSeconds, std::uint32_t period)
    {
        if (period < kMinPeriodSeconds || period > kMaxPeriodSeconds || unixSeconds < 0)
            return 0;
        const std::uint32_t elapsed =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(unixSeconds) % period);
        return period - elapsed;
    }
}
