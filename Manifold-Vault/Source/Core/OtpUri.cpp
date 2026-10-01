#include "Core/OtpUri.h"

#include "Core/Base32.h"
#include "Core/GermanText.h"
#include "Core/Secret.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace Bunker
{
    namespace
    {
        constexpr std::string_view kScheme = "otpauth://";

        bool StartsWithScheme(std::string_view text, std::size_t& afterScheme)
        {
            const std::size_t n = kScheme.size();
            if (text.size() < n)
                return false;
            for (std::size_t i = 0; i < n; ++i)
            {
                const unsigned char a = static_cast<unsigned char>(text[i]);
                const unsigned char b = static_cast<unsigned char>(kScheme[i]);
                const unsigned char lowered =
                    (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
                if (lowered != b)
                    return false;
            }
            afterScheme = n;
            return true;
        }

        bool EqualsLowerAscii(std::string_view text, std::string_view lower)
        {
            if (text.size() != lower.size())
                return false;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const unsigned char a = static_cast<unsigned char>(text[i]);
                const unsigned char folded =
                    (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
                if (folded != static_cast<unsigned char>(lower[i]))
                    return false;
            }
            return true;
        }

        int HexValue(unsigned char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        }

        template <class Put>
        bool PercentDecodeWith(std::string_view in, Put put)
        {
            for (std::size_t i = 0; i < in.size(); ++i)
            {
                const char c = in[i];
                if (c == '\0')
                    return false;
                if (c == '+')
                {
                    put('+');
                    continue;
                }
                if (c != '%')
                {
                    put(c);
                    continue;
                }
                if (i + 2 >= in.size())
                    return false;
                const int hi = HexValue(static_cast<unsigned char>(in[i + 1]));
                const int lo = HexValue(static_cast<unsigned char>(in[i + 2]));
                if (hi < 0 || lo < 0)
                    return false;
                const int value = (hi << 4) | lo;
                if (value == 0)
                    return false;
                put(static_cast<char>(value));
                i += 2;
            }
            return true;
        }

        bool PercentDecode(std::string_view in, std::string& out)
        {
            out.clear();
            out.reserve(in.size());
            if (!PercentDecodeWith(in, [&out](char c) { out.push_back(c); }))
                return false;
            return Utf8IsValid(out);
        }

        bool PercentDecodeSecret(std::string_view in, SecureBuffer& out, std::size_t& length)
        {
            length = 0;
            if (!out.IsValid() || out.Size() < in.size())
                return false;
            unsigned char* dst = out.Data();
            return PercentDecodeWith(in, [dst, &length](char c) {
                dst[length++] = static_cast<unsigned char>(c);
            });
        }

        struct Parameter
        {
            std::string Key;
            std::string_view RawValue;
        };

        bool SplitQuery(std::string_view query, std::vector<Parameter>& out)
        {
            std::size_t start = 0;
            while (start <= query.size())
            {
                const std::size_t amp = query.find('&', start);
                const std::size_t end = (amp == std::string_view::npos) ? query.size() : amp;
                const std::string_view pair = query.substr(start, end - start);
                if (!pair.empty())
                {
                    const std::size_t eq = pair.find('=');
                    if (eq == std::string_view::npos)
                        return false;
                    Parameter p;
                    p.Key = std::string(pair.substr(0, eq));
                    for (char& ch : p.Key)
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch + 32);
                    p.RawValue = pair.substr(eq + 1);
                    for (const Parameter& seen : out)
                        if (seen.Key == p.Key)
                            return false;
                    out.push_back(std::move(p));
                }
                if (amp == std::string_view::npos)
                    break;
                start = amp + 1;
            }
            return true;
        }

        const Parameter* Find(const std::vector<Parameter>& params, const char* key)
        {
            for (const Parameter& p : params)
                if (p.Key == key)
                    return &p;
            return nullptr;
        }

        bool ParseUnsigned(std::string_view text, std::uint32_t& out)
        {
            if (text.empty() || text.size() > 9)
                return false;
            std::uint32_t value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10u + static_cast<std::uint32_t>(c - '0');
            }
            out = value;
            return true;
        }

        Result<TotpConfig> Reject(ErrorCode code, const char* detail)
        {
            return Error{ code, detail };
        }

        Status CanonicaliseInto(const char* raw, std::size_t n, SecureString& out)
        {
            SecureBuffer key(MaxDecodedBase32Bytes(n) + 1);
            if (!key.IsValid())
                return Error{ ErrorCode::OutOfMemory, "no locked memory" };

            const auto decoded = DecodeBase32(raw, n, key);
            if (!decoded.IsOk())
                return Error{ ErrorCode::OtpSecretInvalid, decoded.GetError().Detail };

            return EncodeBase32(key.Data(), decoded.Value(), out);
        }
    }

    bool LooksLikeOtpAuthUri(std::string_view text)
    {
        std::size_t after = 0;
        return StartsWithScheme(text, after);
    }

    Result<TotpConfig> ParseOtpAuthUri(std::string_view text)
    {
        std::size_t pos = 0;
        if (!StartsWithScheme(text, pos))
            return Reject(ErrorCode::OtpUriUnsupported, "not an otpauth URI");

        const std::size_t typeEnd = text.find('/', pos);
        if (typeEnd == std::string_view::npos)
            return Reject(ErrorCode::OtpUriUnsupported, "URI without a type");
        if (!EqualsLowerAscii(text.substr(pos, typeEnd - pos), "totp"))
        {
            return Reject(ErrorCode::OtpUriUnsupported,
                          "only time-based codes are supported");
        }

        const std::size_t labelStart = typeEnd + 1;
        const std::size_t query = text.find('?', labelStart);
        const std::string_view rawLabel =
            text.substr(labelStart, (query == std::string_view::npos)
                                        ? std::string_view::npos
                                        : query - labelStart);

        std::vector<Parameter> params;
        if (query != std::string_view::npos)
        {
            if (!SplitQuery(text.substr(query + 1), params))
                return Reject(ErrorCode::OtpUriUnsupported,
                              "malformed or duplicated parameters");
        }

        std::string_view issuerPart;
        std::string_view accountPart = rawLabel;
        const std::size_t colon = rawLabel.find(':');
        if (colon != std::string_view::npos)
        {
            const std::size_t encoded = rawLabel.find("%3A");
            const std::size_t encodedLower = rawLabel.find("%3a");
            const std::size_t firstEncoded =
                (encoded < encodedLower) ? encoded : encodedLower;
            if (firstEncoded != std::string_view::npos && firstEncoded < colon)
                return Reject(ErrorCode::OtpUriUnsupported, "ambiguous label");

            issuerPart = rawLabel.substr(0, colon);
            accountPart = rawLabel.substr(colon + 1);
        }

        while (!accountPart.empty() && accountPart[0] == ' ')
            accountPart.remove_prefix(1);
        if (accountPart.starts_with("%20"))
            accountPart.remove_prefix(3);

        TotpConfig config;

        const Parameter* secret = Find(params, "secret");
        if (!secret || secret->RawValue.empty())
            return Reject(ErrorCode::OtpSecretInvalid, "no secret in the URI");
        {
            SecureBuffer decoded(secret->RawValue.size() + 1);
            if (!decoded.IsValid())
                return Reject(ErrorCode::OutOfMemory, "no locked memory");
            std::size_t decodedLength = 0;
            if (!PercentDecodeSecret(secret->RawValue, decoded, decodedLength))
                return Reject(ErrorCode::OtpSecretInvalid, "secret not readable");
            const Status stored = CanonicaliseInto(
                reinterpret_cast<const char*>(decoded.Data()), decodedLength,
                config.SecretBase32);
            if (!stored.IsOk())
                return stored.GetError();
        }

        if (!issuerPart.empty() && !PercentDecode(issuerPart, config.Issuer))
            return Reject(ErrorCode::OtpUriUnsupported, "label not readable");
        if (!accountPart.empty() && !PercentDecode(accountPart, config.AccountName))
            return Reject(ErrorCode::OtpUriUnsupported, "label not readable");

        if (const Parameter* p = Find(params, "digits"))
        {
            std::uint32_t digits = 0;
            if (!ParseUnsigned(p->RawValue, digits) ||
                digits < kMinDigits || digits > kMaxDigits)
                return Reject(ErrorCode::OtpUriUnsupported, "digit count not supported");
            config.Digits = digits;
        }
        if (const Parameter* p = Find(params, "period"))
        {
            std::uint32_t period = 0;
            if (!ParseUnsigned(p->RawValue, period) ||
                period < kMinPeriodSeconds || period > kMaxPeriodSeconds)
                return Reject(ErrorCode::OtpUriUnsupported, "time step not supported");
            config.Period = period;
        }
        if (const Parameter* p = Find(params, "algorithm"))
        {
            HashAlgorithm algorithm = HashAlgorithm::Sha1;
            const std::string name(p->RawValue);
            if (!ParseAlgorithm(name.c_str(), algorithm))
                return Reject(ErrorCode::OtpUriUnsupported, "algorithm not supported");
            config.Algorithm = algorithm;
        }
        if (const Parameter* p = Find(params, "issuer"))
        {
            std::string fromParameter;
            if (!PercentDecode(p->RawValue, fromParameter))
                return Reject(ErrorCode::OtpUriUnsupported, "issuer not readable");
            if (!fromParameter.empty())
                config.Issuer = fromParameter;
        }

        return config;
    }

    Result<TotpConfig> ParseBareSecret(std::string_view text)
    {
        TotpConfig config;
        const char* raw = text.empty() ? "" : text.data();
        const Status stored = CanonicaliseInto(raw, text.size(), config.SecretBase32);
        if (!stored.IsOk())
            return stored.GetError();
        return config;
    }

    Result<TotpConfig> ParsePastedSecret(std::string_view text)
    {
        return LooksLikeOtpAuthUri(text) ? ParseOtpAuthUri(text) : ParseBareSecret(text);
    }
}
