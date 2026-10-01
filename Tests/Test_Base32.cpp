#include "TestRunner.h"

#include "Core/Base32.h"
#include "Core/Random.h"
#include "Core/Secret.h"
#include "Generated/OtpKat.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    struct Decoded
    {
        bool Ok = false;
        std::vector<unsigned char> Bytes;
    };

    Decoded Decode(const std::string& text)
    {
        Decoded d;
        SecureBuffer buffer(MaxDecodedBase32Bytes(text.size()) + 1);
        const auto r = DecodeBase32(text.c_str(), text.size(), buffer);
        d.Ok = r.IsOk();
        if (d.Ok)
            d.Bytes.assign(buffer.Data(), buffer.Data() + r.Value());
        return d;
    }

    std::string Ascii(const std::vector<unsigned char>& bytes)
    {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}

TEST_CASE(Base32_MatchesRfc4648Section10)
{
    for (const auto& v : Kat::kBase32)
    {
        const std::string plain = v.Plain;
        const std::string encoded = v.Encoded;

        std::string expected;
        for (char c : encoded)
            if (c != '=')
                expected.push_back(c);
        SecureString got;
        CHECK(EncodeBase32(reinterpret_cast<const unsigned char*>(plain.data()),
                           plain.size(), got).IsOk());
        CHECK_MSG(got == expected,
                  "BASE32(\"" + plain + "\") = " + got.ToStdString() + ", expected " + expected);

        if (plain.empty())
            continue;
        const Decoded d = Decode(encoded);
        CHECK_MSG(d.Ok, "RFC line \"" + encoded + "\" was rejected");
        if (d.Ok)
            CHECK_MSG(Ascii(d.Bytes) == plain,
                      "\"" + encoded + "\" gave \"" + Ascii(d.Bytes) + "\"");
    }
}

TEST_CASE(Base32_PaddedAndUnpaddedAgree)
{
    for (const auto& v : Kat::kBase32)
    {
        const std::string padded = v.Encoded;
        std::string bare;
        for (char c : padded)
            if (c != '=')
                bare.push_back(c);
        if (bare.empty())
            continue;

        const Decoded a = Decode(padded);
        const Decoded b = Decode(bare);
        CHECK_MSG(a.Ok && b.Ok, "one of the two spellings was rejected: " + padded);
        if (a.Ok && b.Ok)
            CHECK_MSG(a.Bytes == b.Bytes,
                      "padded and unpadded give different keys: " + padded);
    }
}

TEST_CASE(Base32_ToleratesCaseAndGrouping)
{
    const std::string canonical = "JBSWY3DPEHPK3PXP";
    const Decoded reference = Decode(canonical);
    CHECK(reference.Ok);

    const char* spellings[] = {
        "jbswy3dpehpk3pxp",
        "JbSwY3dPeHpK3pXp",
        "JBSW Y3DP EHPK 3PXP",
        "JBSW-Y3DP-EHPK-3PXP",
        "  JBSWY3DPEHPK3PXP  ",
        "JBSWY3DP\nEHPK3PXP",
        "JBSWY3DPEHPK3PXP========",
    };
    for (const char* s : spellings)
    {
        const Decoded d = Decode(s);
        CHECK_MSG(d.Ok, std::string("rejected: \"") + s + "\"");
        if (d.Ok)
            CHECK_MSG(d.Bytes == reference.Bytes,
                      std::string("different key from \"") + s + "\"");
    }
}

TEST_CASE(Base32_RejectsCharactersOutsideTheAlphabet)
{
    const char* bad[] = {
        "JBSWY3DPEHPK3PX0",
        "JBSWY3DPEHPK3PX1",
        "JBSWY3DPEHPK3PX8",
        "JBSWY3DPEHPK3PX9",
        "JBSWY3DPEHPK3PX!",
        "JBSWY3DP+HPK3PXP",
        "JBSWY3DP_HPK3PXP",
    };
    for (const char* s : bad)
        CHECK_MSG(!Decode(s).Ok, std::string("wrongly accepted: \"") + s + "\"");

    const Decoded withO = Decode("JBSWY3DPEHPK3PXO");
    const Decoded withP = Decode("JBSWY3DPEHPK3PXP");
    CHECK(withO.Ok && withP.Ok);
    CHECK_MSG(withO.Bytes != withP.Bytes,
              "O and P give the same key -- the example proves nothing");
}

TEST_CASE(Base32_RejectsImpossibleGroupLengths)
{
    CHECK(!Decode("A").Ok);
    CHECK(!Decode("ABC").Ok);
    CHECK(!Decode("ABCDEF").Ok);
    CHECK(!Decode("JBSWY3DPA").Ok);
    CHECK(!Decode("JBSWY3DPABC").Ok);

    CHECK(Decode("AB").Ok);
    CHECK(Decode("ABCD").Ok);
    CHECK(Decode("ABCDE").Ok);
    CHECK(Decode("ABCDEFG").Ok);
    CHECK(Decode("ABCDEFGH").Ok);
}

TEST_CASE(Base32_RejectsMisplacedPadding)
{
    CHECK(!Decode("JBSW=Y3DP").Ok);
    CHECK(!Decode("JBSWY3DP==").Ok);

    CHECK(!Decode("JBSWY3DPEHPK3PXP=").Ok);
    CHECK(!Decode("=").Ok);
    CHECK(!Decode("").Ok);
    CHECK(!Decode("   ").Ok);
}

TEST_CASE(Base32_AcceptsNonCanonicalLeftoverBits)
{
    const Decoded canonical = Decode("MZXQ");
    const Decoded sloppy = Decode("MZXR");
    CHECK(canonical.Ok);
    CHECK_MSG(sloppy.Ok, "non-canonical trailing bits were rejected");
    if (canonical.Ok && sloppy.Ok)
    {
        CHECK(Ascii(canonical.Bytes) == "fo");
        CHECK_MSG(canonical.Bytes == sloppy.Bytes,
                  "the discarded trailing bits changed the key");
    }
}

TEST_CASE(Base32_RoundTripsEveryLength)
{
    DeterministicRandom r(8823);
    for (std::size_t len = 1; len <= 64; ++len)
    {
        std::vector<unsigned char> original(len);
        r.Fill(original.data(), original.size());

        SecureString canonical;
        CHECK(EncodeBase32(original.data(), original.size(), canonical).IsOk());
        CHECK_EQ(canonical.Size(), EncodedBase32Length(len));
        const std::string encoded = canonical.ToStdString();
        const Decoded back = Decode(encoded);
        CHECK_MSG(back.Ok, "length " + std::to_string(len) + " was rejected");
        if (!back.Ok)
            return;
        CHECK_MSG(back.Bytes == original,
                  "length " + std::to_string(len) + " came back different");

        for (char c : encoded)
            CHECK_MSG((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'),
                      std::string("output contains '") + c + "'");
    }
}

TEST_CASE(Base32_WipesTheBufferWhenItRefuses)
{
    SecureBuffer buffer(64);
    std::memset(buffer.Data(), 0xEE, buffer.Size());

    const std::string poisoned = "JBSWY3DPEHPK3PX0";
    const auto r = DecodeBase32(poisoned.c_str(), poisoned.size(), buffer);
    CHECK(!r.IsOk());

    std::size_t nonZero = 0;
    for (std::size_t i = 0; i < buffer.Size(); ++i)
        if (buffer.Data()[i] != 0)
            ++nonZero;
    CHECK_MSG(nonZero == 0,
              std::to_string(nonZero) + " bytes remained after the rejection");
}

TEST_CASE(Base32_RefusesABufferThatIsTooSmall)
{
    SecureBuffer tiny(2);
    const std::string text = "JBSWY3DPEHPK3PXP";
    const auto r = DecodeBase32(text.c_str(), text.size(), tiny);
    CHECK(!r.IsOk());

    SecureBuffer exact(MaxDecodedBase32Bytes(text.size()));
    CHECK(DecodeBase32(text.c_str(), text.size(), exact).IsOk());
}

TEST_CASE(Base32_ErrorsNeverEchoTheInput)
{
    const char* inputs[] = {
        "GEHEIMNIS0", "JBSW=Y3DP", "A", "", "   ", "JBSWY3DPEHPK3PX!",
    };
    for (const char* in : inputs)
    {
        SecureBuffer buffer(64);
        const auto r = DecodeBase32(in, std::strlen(in), buffer);
        CHECK(!r.IsOk());
        if (r.IsOk())
            continue;
        const char* detail = r.GetError().Detail;
        CHECK(detail != nullptr);
        if (!detail)
            continue;
        CHECK_MSG(std::strstr(detail, in) == nullptr || *in == 0,
                  std::string("the message contains the input: ") + detail);
        for (const char* p = detail; *p; ++p)
            CHECK_MSG(*p != '"', std::string("message with quotes: ") + detail);
    }
}

TEST_CASE(Base32_TheRawDecoderWipesItsBufferWhenItRefuses)
{
    const char* bad[] = {
        "JBSWY3DPEHPK3PX1",
        "JBSWY3DPEHPK3PXPA",
        "JBSW=Y3DP",
        "JBSWY3DPEHPK3PXP=",
        "JBSWY3DP==",
    };
    for (const char* text : bad)
    {
        unsigned char buffer[64];
        std::memset(buffer, 0x5A, sizeof(buffer));
        const auto r = DecodeBase32(text, std::strlen(text), buffer, sizeof(buffer));
        CHECK_MSG(!r.IsOk(), std::string("accepted: ") + text);

        std::size_t left = 0;
        for (unsigned char b : buffer)
            if (b != 0)
                ++left;
        CHECK_MSG(left == 0, std::to_string(left) + " bytes remained after rejecting \"" +
                                 text + "\"");
    }
}

TEST_CASE(Base32_TheRawDecoderStaysInsideItsCapacity)
{
    unsigned char buffer[64];
    std::memset(buffer, 0x5A, sizeof(buffer));
    const char text[] = "JBSWY3DPEHPK3PXP";
    const std::size_t capacity = 8;
    const auto r = DecodeBase32(text, sizeof(text) - 1, buffer, capacity);
    CHECK_MSG(!r.IsOk(), "ten bytes fit into a capacity of eight");

    std::size_t wiped = 0, sentinel = 0;
    for (std::size_t i = 0; i < capacity; ++i)
        if (buffer[i] == 0)
            ++wiped;
    for (std::size_t i = capacity; i < sizeof(buffer); ++i)
        if (buffer[i] == 0x5A)
            ++sentinel;
    CHECK_MSG(wiped == capacity, "the buffer was not wiped after the rejection");
    CHECK_MSG(sentinel == sizeof(buffer) - capacity,
              "something was written or wiped beyond the capacity");

    const auto exact = DecodeBase32(text, sizeof(text) - 1, buffer, 10);
    CHECK(exact.IsOk());
    if (exact.IsOk())
        CHECK_EQ(exact.Value(), std::size_t{ 10 });
}
