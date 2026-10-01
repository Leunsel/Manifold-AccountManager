#include "TestRunner.h"

#include "Core/GermanText.h"
#include "Core/Otp.h"
#include "Core/OtpUri.h"

#include <cstring>
#include <initializer_list>
#include <string>

using namespace Bunker;

namespace
{
    const char* kGoogleStyle =
        "otpauth://totp/Example:alice@example.com"
        "?secret=JBSWY3DPEHPK3PXP&issuer=Example";
}

TEST_CASE(OtpUri_RecognisesTheSchemeWithoutParsing)
{
    CHECK(LooksLikeOtpAuthUri(kGoogleStyle));
    CHECK(LooksLikeOtpAuthUri("OTPAUTH://totp/x?secret=JBSWY3DPEHPK3PXP"));
    CHECK(LooksLikeOtpAuthUri("otpauth://"));
    CHECK(!LooksLikeOtpAuthUri("JBSWY3DPEHPK3PXP"));
    CHECK(!LooksLikeOtpAuthUri(""));
    CHECK(!LooksLikeOtpAuthUri("https://example.com"));
    CHECK(!LooksLikeOtpAuthUri("otpaut"));
}

TEST_CASE(OtpUri_ParsesTheCommonShape)
{
    const auto r = ParseOtpAuthUri(kGoogleStyle);
    CHECK(r.IsOk());
    if (!r.IsOk())
        return;
    const TotpConfig& c = r.Value();
    CHECK(c.SecretBase32 == "JBSWY3DPEHPK3PXP");
    CHECK(c.Issuer == "Example");
    CHECK(c.AccountName == "alice@example.com");
    CHECK_EQ(c.Digits, std::uint32_t{ 6 });
    CHECK_EQ(c.Period, std::uint32_t{ 30 });
    CHECK(c.Algorithm == HashAlgorithm::Sha1);
}

TEST_CASE(OtpUri_AbsentParametersTakeTheDefaults)
{
    const auto r = ParseOtpAuthUri("otpauth://totp/only-account?secret=JBSWY3DPEHPK3PXP");
    CHECK(r.IsOk());
    if (!r.IsOk())
        return;
    CHECK_EQ(r.Value().Digits, std::uint32_t{ 6 });
    CHECK_EQ(r.Value().Period, std::uint32_t{ 30 });
    CHECK(r.Value().Algorithm == HashAlgorithm::Sha1);
    CHECK(r.Value().Issuer.empty());
    CHECK(r.Value().AccountName == "only-account");
}

TEST_CASE(OtpUri_ReadsTheOptionalParameters)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/Bank:kunde?secret=JBSWY3DPEHPK3PXP"
        "&digits=8&period=60&algorithm=SHA512");
    CHECK(r.IsOk());
    if (!r.IsOk())
        return;
    CHECK_EQ(r.Value().Digits, std::uint32_t{ 8 });
    CHECK_EQ(r.Value().Period, std::uint32_t{ 60 });
    CHECK(r.Value().Algorithm == HashAlgorithm::Sha512);

    const auto seven = ParseOtpAuthUri(
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=7");
    CHECK(seven.IsOk());
    if (seven.IsOk())
        CHECK_EQ(seven.Value().Digits, std::uint32_t{ 7 });
}

TEST_CASE(OtpUri_PresentButUnreadableIsRefusedNotDefaulted)
{
    const char* bad[] = {
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=abc",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=5",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=9",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&period=0",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&period=99999",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&period=dreissig",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&algorithm=SHA224",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&algorithm=MD5",
    };
    for (const char* s : bad)
    {
        const auto r = ParseOtpAuthUri(s);
        CHECK_MSG(!r.IsOk(), std::string("wrongly accepted: ") + s);
        if (!r.IsOk())
            CHECK(r.Code() == ErrorCode::OtpUriUnsupported);
    }
}

TEST_CASE(OtpUri_RefusesWhatItCannotDo)
{
    const auto hotp = ParseOtpAuthUri("otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP&counter=1");
    CHECK(!hotp.IsOk());
    if (!hotp.IsOk())
        CHECK(hotp.Code() == ErrorCode::OtpUriUnsupported);

    CHECK(!ParseOtpAuthUri("otpauth://totp/x").IsOk());
    CHECK(!ParseOtpAuthUri("otpauth://totp/x?secret=").IsOk());
    CHECK(!ParseOtpAuthUri("otpauth://totp/x?secret=NICHT!BASE32").IsOk());
    CHECK(!ParseOtpAuthUri("otpauth://").IsOk());
    CHECK(!ParseOtpAuthUri("https://example.com").IsOk());
}

TEST_CASE(OtpUri_RefusesARepeatedParameter)
{
    CHECK(!ParseOtpAuthUri(
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&secret=MZXW6YTBOI======").IsOk());
    CHECK(!ParseOtpAuthUri(
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=6&digits=8").IsOk());

    CHECK(!ParseOtpAuthUri("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits").IsOk());
}

TEST_CASE(OtpUri_SplitsTheLabelOnTheFirstRawColon)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/GitHub:alex.doe?secret=JBSWY3DPEHPK3PXP");
    CHECK(r.IsOk());
    if (r.IsOk())
    {
        CHECK(r.Value().Issuer == "GitHub");
        CHECK(r.Value().AccountName == "alex.doe");
    }

    const auto plain = ParseOtpAuthUri("otpauth://totp/alex.doe?secret=JBSWY3DPEHPK3PXP");
    CHECK(plain.IsOk());
    if (plain.IsOk())
    {
        CHECK(plain.Value().Issuer.empty());
        CHECK(plain.Value().AccountName == "alex.doe");
    }

    for (const char* s : { "otpauth://totp/Bank: kunde?secret=JBSWY3DPEHPK3PXP",
                           "otpauth://totp/Bank:%20kunde?secret=JBSWY3DPEHPK3PXP" })
    {
        const auto spaced = ParseOtpAuthUri(s);
        CHECK_MSG(spaced.IsOk(), std::string("rejected: ") + s);
        if (spaced.IsOk())
            CHECK_MSG(spaced.Value().AccountName == "kunde",
                      "account = \"" + spaced.Value().AccountName + "\"");
    }
}

TEST_CASE(OtpUri_RefusesAnAmbiguousLabel)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/a%3Ab:c?secret=JBSWY3DPEHPK3PXP");
    CHECK_MSG(!r.IsOk(), "an ambiguous name was accepted");

    const auto encoded = ParseOtpAuthUri(
        "otpauth://totp/a%3Ab?secret=JBSWY3DPEHPK3PXP");
    CHECK(encoded.IsOk());
    if (encoded.IsOk())
    {
        CHECK(encoded.Value().Issuer.empty());
        CHECK_MSG(encoded.Value().AccountName == "a:b",
                  "account = \"" + encoded.Value().AccountName + "\"");
    }
}

TEST_CASE(OtpUri_TheIssuerParameterWinsOverTheLabel)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/Alt:konto?secret=JBSWY3DPEHPK3PXP&issuer=Neu");
    CHECK(r.IsOk());
    if (r.IsOk())
    {
        CHECK(r.Value().Issuer == "Neu");
        CHECK(r.Value().AccountName == "konto");
    }
}

TEST_CASE(OtpUri_RefusesBrokenPercentEncoding)
{
    const char* bad[] = {
        "otpauth://totp/a%?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%2?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%zz?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%00b?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%C3?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%FF%FE?secret=JBSWY3DPEHPK3PXP",
    };
    for (const char* s : bad)
        CHECK_MSG(!ParseOtpAuthUri(s).IsOk(), std::string("accepted: ") + s);

    CHECK(!ParseOtpAuthUri("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&issuer=a%00b").IsOk());
}

TEST_CASE(OtpUri_DecodesUmlautsInTheLabel)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/Sparkasse:j%C3%B6rg%40example.de?secret=JBSWY3DPEHPK3PXP");
    CHECK(r.IsOk());
    if (!r.IsOk())
        return;
    CHECK(r.Value().Issuer == "Sparkasse");
    CHECK_MSG(r.Value().AccountName == "j\xC3\xB6rg@example.de",
              "account = \"" + r.Value().AccountName + "\"");
    CHECK(Utf8IsValid(r.Value().AccountName));
}

TEST_CASE(OtpUri_CanonicalisesTheSecret)
{
    const char* spellings[] = {
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/x?secret=jbswy3dpehpk3pxp",
        "otpauth://totp/x?secret=JBSW%20Y3DP%20EHPK%203PXP",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP========",
    };
    for (const char* s : spellings)
    {
        const auto r = ParseOtpAuthUri(s);
        CHECK_MSG(r.IsOk(), std::string("rejected: ") + s);
        if (r.IsOk())
            CHECK_MSG(r.Value().SecretBase32 == "JBSWY3DPEHPK3PXP",
                      std::string("not canonical: ").append(r.Value().SecretBase32.View()));
    }
}

TEST_CASE(OtpUri_BareSecretsTakeTheOtherPath)
{
    const auto r = ParseBareSecret("jbsw y3dp ehpk 3pxp");
    CHECK(r.IsOk());
    if (r.IsOk())
    {
        CHECK(r.Value().SecretBase32 == "JBSWY3DPEHPK3PXP");
        CHECK_EQ(r.Value().Digits, std::uint32_t{ 6 });
    }
    CHECK(!ParseBareSecret("").IsOk());
    CHECK(!ParseBareSecret("not base32!").IsOk());

    CHECK(ParsePastedSecret(kGoogleStyle).IsOk());
    CHECK(ParsePastedSecret("JBSWY3DPEHPK3PXP").IsOk());

    const auto bare = ParsePastedSecret("JBSWY3DPEHPK3PXP");
    CHECK(bare.IsOk());
    const auto broken = ParsePastedSecret("otpauth://totp/x?secret=!!!");
    CHECK(!broken.IsOk());
}

TEST_CASE(OtpUri_ErrorsNeverEchoTheUri)
{
    const char* inputs[] = {
        "otpauth://totp/x?secret=GEHEIMGEHEIMGEHEIM0",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=abc",
        "otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%3Ab:c?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&secret=JBSWY3DPEHPK3PXP",
    };
    for (const char* s : inputs)
    {
        const auto r = ParseOtpAuthUri(s);
        CHECK_MSG(!r.IsOk(), std::string("accepted: ") + s);
        if (r.IsOk())
            continue;
        const char* detail = r.GetError().Detail;
        CHECK(detail != nullptr);
        if (!detail)
            continue;
        CHECK_MSG(std::strstr(detail, "secret") == nullptr,
                  std::string("message names the parameter: ") + detail);
        CHECK_MSG(std::strstr(detail, "JBSW") == nullptr &&
                      std::strstr(detail, "GEHEIM") == nullptr,
                  std::string("message contains the secret: ") + detail);
        CHECK_MSG(std::strstr(detail, "otpauth") == nullptr,
                  std::string("message contains the address: ") + detail);
    }
}

TEST_CASE(OtpUri_ProducesAConfigThatActuallyGeneratesACode)
{
    const auto r = ParseOtpAuthUri(
        "otpauth://totp/RFC:6238?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&digits=8");
    CHECK(r.IsOk());
    if (!r.IsOk())
        return;

    const auto code = TotpFromConfig(r.Value(), 59);
    CHECK(code.IsOk());
    if (code.IsOk())
        CHECK_MSG(code.Value() == "94287082",
                  "code " + code.Value() + " instead of 94287082");
}

TEST_CASE(OtpUri_RefusesOverlongUtf8)
{
    const char* bad[] = {
        "otpauth://totp/a%C0%80b?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%E0%80%80b?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%C1%81b?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&issuer=a%C0%80b",
        "otpauth://totp/a%ED%A0%80b?secret=JBSWY3DPEHPK3PXP",
        "otpauth://totp/a%F7%BF%BF%BFb?secret=JBSWY3DPEHPK3PXP",
    };
    for (const char* s : bad)
        CHECK_MSG(!ParseOtpAuthUri(s).IsOk(),
                  std::string("overlong or impossible form accepted: ") + s);

    auto bytes = [](std::initializer_list<int> v) {
        std::string s;
        for (int b : v) s.push_back(static_cast<char>(b));
        return s;
    };
    CHECK(!Utf8IsValid(bytes({ 0xC0, 0x80 })));
    CHECK(!Utf8IsValid(bytes({ 0xC1, 0x81 })));
    CHECK(!Utf8IsValid(bytes({ 0xE0, 0x80, 0x80 })));
    CHECK(!Utf8IsValid(bytes({ 0xF0, 0x80, 0x80, 0x80 })));
    CHECK(!Utf8IsValid(bytes({ 0xED, 0xA0, 0x80 })));
    CHECK(!Utf8IsValid(bytes({ 0xF7, 0xBF, 0xBF, 0xBF })));
    CHECK(!Utf8IsValid(bytes({ 0x80 })));
    CHECK(!Utf8IsValid(bytes({ 0xC3 })));
    CHECK(Utf8IsValid(bytes({ 0xC3, 0xB6 })));
    CHECK(Utf8IsValid(std::string("ganz normal")));
}
