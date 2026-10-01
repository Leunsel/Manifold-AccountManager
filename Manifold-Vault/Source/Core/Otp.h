#pragma once

#include "Core/Base32.h"
#include "Core/Hmac.h"
#include "Core/Result.h"
#include "Core/SecureString.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Bunker
{
    inline constexpr std::uint8_t kMinDigits = 6;
    inline constexpr std::uint8_t kMaxDigits = 8;
    inline constexpr std::uint8_t kDefaultDigits = 6;

    inline constexpr std::uint32_t kMinPeriodSeconds = 1;
    inline constexpr std::uint32_t kMaxPeriodSeconds = 600;
    inline constexpr std::uint32_t kDefaultPeriodSeconds = 30;

    inline constexpr std::size_t kMaxTotpKeyBytes = 320;

    struct TotpConfig
    {
        SecureString SecretBase32;
        std::uint32_t Digits = kDefaultDigits;
        std::uint32_t Period = kDefaultPeriodSeconds;
        HashAlgorithm Algorithm = HashAlgorithm::Sha1;

        std::string Issuer;
        std::string AccountName;
    };

    void WipeTotpKey(unsigned char* key, std::size_t n);

    template <class R, class Use>
    R WithDecodedTotpKey(const SecureString& secret, unsigned char* key,
                         std::size_t capacity, Use&& use)
    {
        struct Guard
        {
            unsigned char* Key;
            std::size_t Capacity;
            ~Guard() { WipeTotpKey(Key, Capacity); }
        } guard{ key, capacity };

        const auto decoded = DecodeBase32(secret.CStr(), secret.Size(), key, capacity);
        if (!decoded.IsOk())
            return R(Error{ ErrorCode::OtpSecretInvalid, decoded.GetError().Detail });
        return use(static_cast<const unsigned char*>(key), decoded.Value());
    }

    Result<std::string> TotpFromConfig(const TotpConfig& config, std::int64_t unixSeconds);

    std::uint32_t TruncateDynamic(const unsigned char* digest, std::size_t digestLen);

    void SerialiseCounter(std::uint64_t counter, unsigned char out[8]);

    Result<std::string> Hotp(const unsigned char* key, std::size_t keyLen,
                             std::uint64_t counter,
                             std::uint8_t digits = kDefaultDigits,
                             HashAlgorithm algorithm = HashAlgorithm::Sha1);

    Result<std::string> Totp(const unsigned char* key, std::size_t keyLen,
                             std::int64_t unixSeconds,
                             std::uint32_t period = kDefaultPeriodSeconds,
                             std::uint8_t digits = kDefaultDigits,
                             HashAlgorithm algorithm = HashAlgorithm::Sha1);

    std::uint32_t SecondsRemaining(std::int64_t unixSeconds, std::uint32_t period);
}
