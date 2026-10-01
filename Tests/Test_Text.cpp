#include "TestRunner.h"

#include "Core/GermanText.h"
#include "Core/Generator.h"
#include "Core/PasswordStrength.h"
#include "Core/Random.h"
#include "Core/Result.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    bool IsValidUtf8(const char* text, std::string& why)
    {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
        while (*p)
        {
            const unsigned char lead = *p;
            int extra = 0;
            unsigned int cp = 0;
            if (lead < 0x80)            { extra = 0; cp = lead; }
            else if ((lead & 0xE0) == 0xC0) { extra = 1; cp = lead & 0x1Fu; }
            else if ((lead & 0xF0) == 0xE0) { extra = 2; cp = lead & 0x0Fu; }
            else if ((lead & 0xF8) == 0xF0) { extra = 3; cp = lead & 0x07u; }
            else { why = "ungueltiges Leitbyte"; return false; }

            ++p;
            for (int i = 0; i < extra; ++i, ++p)
            {
                if ((*p & 0xC0) != 0x80) { why = "continuation byte missing"; return false; }
                cp = (cp << 6) | (*p & 0x3Fu);
            }
            if (extra == 1 && cp < 0x80)    { why = "ueberlange Form"; return false; }
            if (extra == 2 && cp < 0x800)   { why = "ueberlange Form"; return false; }
            if (extra == 3 && cp < 0x10000) { why = "ueberlange Form"; return false; }
            if (cp > 0x10FFFF)              { why = "ausserhalb von Unicode"; return false; }
            if (cp >= 0xD800 && cp <= 0xDFFF) { why = "Surrogat"; return false; }
        }
        return true;
    }

    const ErrorCode kAllCodes[] = {
        ErrorCode::Ok,
        ErrorCode::OutOfMemory,
        ErrorCode::InvalidArgument,
        ErrorCode::NotInitialised,
        ErrorCode::CryptoSelfTestFailed,
        ErrorCode::KdfFailed,
        ErrorCode::EncryptFailed,
        ErrorCode::DecryptFailed,
        ErrorCode::WrongPassword,
        ErrorCode::FileTamperedOrCorrupt,
        ErrorCode::UnsupportedFormatVersion,
        ErrorCode::FileTooLarge,
    };
}

TEST_CASE(Text_TheValidatorRejectsWhatItShould)
{
    std::string why;
    CHECK(IsValidUtf8("Loeschen", why));
    CHECK(IsValidUtf8("L\xC3\xB6schen", why));
    CHECK(!IsValidUtf8("L\xE4schen", why));
    CHECK(!IsValidUtf8("abc\xC3", why));
    CHECK(!IsValidUtf8("\xC0\xAF", why));
    CHECK(!IsValidUtf8("\x80xyz", why));
}

TEST_CASE(Text_GermanErrorMessagesAreValidUtf8)
{
    for (ErrorCode code : kAllCodes)
    {
        const char* msg = ToUserMessage(code);
        CHECK_MSG(msg != nullptr && *msg != 0,
                  std::string("empty message for ") + ToString(code));
        std::string why;
        CHECK_MSG(IsValidUtf8(msg, why),
                  std::string("broken encoding in '") + msg + "': " + why);
    }
}

TEST_CASE(Text_StrengthOutputIsValidUtf8)
{
    const Crypto::KdfParams params = Crypto::KdfParams::Moderate();
    const char* samples[] = { "", "a", "abc", "hunter2", "korrekt-pferd-batterie-klammer",
                              "Vault test password 2026" };
    for (const char* s : samples)
    {
        const PasswordStrength st = EstimateStrength(s, std::strlen(s), params);
        std::string why;
        CHECK_MSG(IsValidUtf8(StrengthVerdict(st), why),
                  std::string("verdict broken: ") + why);
        const std::string d = HumanDuration(st.EntropyBits);
        CHECK_MSG(IsValidUtf8(d.c_str(), why), std::string("duration broken: ") + why);
    }

    const double seconds[] = { 0.0, 0.5, 3.0, 90.0, 7200.0, 200000.0,
                               3.0e7, 3.0e10, 1.0e30, 1.0e300 };
    for (double s : seconds)
    {
        const std::string d = HumanDuration(s);
        std::string why;
        CHECK_MSG(!d.empty(), "empty duration");
        CHECK_MSG(IsValidUtf8(d.c_str(), why),
                  std::string("duration broken at ") + std::to_string(s) + ": " + why);
    }
}

TEST_CASE(Text_PassphraseWordListStaysAsciiOnly)
{
    DeterministicRandom r(31337);
    std::vector<std::string> seen;
    for (int run = 0; run < 4000; ++run)
    {
        const auto result = GeneratePassphrase(6, ' ', r);
        CHECK(result.IsOk());
        for (unsigned char c : result.Value().Text)
        {
            const bool allowed = (c >= 'a' && c <= 'z') || c == ' ';
            CHECK_MSG(allowed,
                      "word list contains a character outside a-z: byte 0x" +
                      std::to_string(static_cast<int>(c)));
            if (!allowed)
                return;
        }
    }
}

TEST_CASE(Text_GeneratedPasswordsStayAscii)
{
    DeterministicRandom r(4711);
    GeneratorOptions o;
    o.Length = 64;
    for (int run = 0; run < 200; ++run)
    {
        const auto result = GeneratePassword(o, r);
        CHECK(result.IsOk());
        for (unsigned char c : result.Value().Text)
        {
            CHECK_MSG(c >= 0x21 && c <= 0x7E,
                      "password contains a byte outside printable ASCII: 0x" +
                      std::to_string(static_cast<int>(c)));
            if (c < 0x21 || c > 0x7E)
                return;
        }
    }
}

TEST_CASE(Text_UppercasingHandlesUmlautsAndSharpS)
{
    CHECK(UppercaseGerman("Benutzername") == "BENUTZERNAME");
    CHECK(UppercaseGerman("Passwort") == "PASSWORT");

    CHECK(UppercaseGerman("Größe") == "GRÖSSE");
    CHECK(UppercaseGerman("äöü") == "ÄÖÜ");
    CHECK(UppercaseGerman("ÄÖÜ") == "ÄÖÜ");

    CHECK(UppercaseGerman("straße") == "STRASSE");
    CHECK(UppercaseGerman("ß").size() == 2);

    CHECK(UppercaseGerman("×") == "×");
    CHECK(UppercaseGerman("÷") == "÷");

    const std::string cyrillic = "Привет";
    CHECK(UppercaseGerman(cyrillic) == cyrillic);
    const std::string padlock = "🔒";
    CHECK(UppercaseGerman(padlock) == padlock);

    CHECK(UppercaseGerman("") == "");

    const std::string truncated = std::string("abc") + static_cast<char>(0xC3);
    CHECK_EQ(truncated.size(), std::size_t{ 4 });
    CHECK_EQ(UppercaseGerman(truncated).size(), std::size_t{ 4 });
}

TEST_CASE(Text_UppercasingOutputIsAlwaysValidUtf8)
{
    const std::string inputs[] = {
        "Größe", "Kontogröße", "äöüß", "abc", "",
        std::string("abc") + static_cast<char>(0xC3),
        std::string(1, static_cast<char>(0x80)) + "abc",
        std::string(1, static_cast<char>(0xF0)),
    };
    for (const std::string& in : inputs)
    {
        const std::string out = UppercaseGerman(in);
        std::string why;
        if (IsValidUtf8(in.c_str(), why))
            CHECK_MSG(IsValidUtf8(out.c_str(), why),
                      std::string("valid UTF-8 became invalid: ") + why);
    }
}

TEST_CASE(Text_SnapBackLandsOnCharacterBoundaries)
{
    const std::string s = "Müller";
    CHECK_EQ(s.size(), std::size_t{ 7 });
    CHECK_EQ(Utf8SnapBack(s, 0), std::size_t{ 0 });
    CHECK_EQ(Utf8SnapBack(s, 1), std::size_t{ 1 });
    CHECK_MSG(Utf8SnapBack(s, 2) == 1, "a cut in the middle of a character was not moved back");
    CHECK_EQ(Utf8SnapBack(s, 3), std::size_t{ 3 });
    CHECK_EQ(Utf8SnapBack(s, 7), std::size_t{ 7 });
    CHECK_EQ(Utf8SnapBack(s, 99), std::size_t{ 7 });

    for (std::size_t i = 0; i <= s.size(); ++i)
    {
        const std::string head = s.substr(0, Utf8SnapBack(s, i));
        std::string why;
        CHECK_MSG(IsValidUtf8(head.c_str(), why),
                  "cut at " + std::to_string(i) + " gives broken UTF-8");
    }
}

TEST_CASE(Text_SequenceLengthCoversEveryLeadByte)
{
    CHECK_EQ(Utf8SequenceLength(0x41), std::size_t{ 1 });
    CHECK_EQ(Utf8SequenceLength(0xC3), std::size_t{ 2 });
    CHECK_EQ(Utf8SequenceLength(0xE2), std::size_t{ 3 });
    CHECK_EQ(Utf8SequenceLength(0xF0), std::size_t{ 4 });
    CHECK_EQ(Utf8SequenceLength(0x80), std::size_t{ 1 });
    CHECK_EQ(Utf8SequenceLength(0xFF), std::size_t{ 1 });
}
