#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Secret.h"
#include "Core/VaultFormat.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;

    std::uint16_t T(Tag t) { return static_cast<std::uint16_t>(t); }

    Field Make(const char* key, const char* value, bool secret = false)
    {
        Field f;
        f.Key = key;
        f.Value = value;
        f.Flags = secret ? kFieldFlagSecret : 0u;
        return f;
    }

    Document FatDocument(std::size_t entries = 40)
    {
        Document doc;
        for (std::size_t i = 0; i < entries; ++i)
        {
            Entry e;
            e.Title = "Account " + std::to_string(i);
            e.PlatformId = "beispiel";
            e.Folder = (i % 3 == 0) ? "Arbeit" : "Privat";
            e.Fields.push_back(Make("Benutzername", "alex.doe@example.com"));
            e.Fields.push_back(Make("Password", "SehrLangesPassword-0123456789", true));
            e.Fields.push_back(Make("Webseite", "https://example.com"));
            e.Fields.push_back(Make("Notiz", "two lines of text, so that it counts"));
            const auto id = AddEntry(doc, std::move(e), kNow);
            CHECK(id.IsOk());
        }
        return doc;
    }

    bool Contains(const unsigned char* hay, std::size_t hayLen,
                  const char* needle)
    {
        const std::size_t n = std::strlen(needle);
        if (n == 0 || hayLen < n)
            return false;
        for (std::size_t i = 0; i + n <= hayLen; ++i)
            if (std::memcmp(hay + i, needle, n) == 0)
                return true;
        return false;
    }
}

TEST_CASE(Codec_MeasuringTouchesNoByte)
{
    const Document doc = FatDocument();

    Writer measure = Writer::Measuring();
    Encode(doc, measure);

    CHECK_MSG(measure.Data() == nullptr,
              "the measuring pass touched a buffer");
    CHECK_MSG(measure.Size() > 1000,
              "measured only " + std::to_string(measure.Size()) + " bytes");
    CHECK(measure.IsBalanced());
    CHECK(!measure.Failed());
}

TEST_CASE(Codec_MeasureAndFillAgree)
{
    const Document doc = FatDocument();

    Writer measure = Writer::Measuring();
    Encode(doc, measure);
    Writer::Plan plan;
    CHECK(measure.Measured(plan));

    std::vector<unsigned char> buf(plan.Bytes(), 0);
    Writer fill = Writer::Filling(plan, buf.data(), buf.size());
    Encode(doc, fill);

    CHECK_MSG(fill.Filled(), "fill pass did not finish cleanly");
    CHECK_EQ(fill.Size(), plan.Bytes());

    Document back;
    CHECK(Decode(buf.data(), buf.size(), back));
    CHECK_EQ(back.Entries.size(), doc.Entries.size());
}

TEST_CASE(Codec_DivergenceRefusesInsteadOfTruncating)
{
    const Document small = FatDocument(3);
    Document bigger = FatDocument(3);
    {
        Entry extra;
        extra.Title = "Einer mehr";
        extra.Fields.push_back(Make("Password", "one more", true));
        CHECK(AddEntry(bigger, std::move(extra), kNow).IsOk());
    }

    Writer measure = Writer::Measuring();
    Encode(small, measure);
    Writer::Plan plan;
    CHECK(measure.Measured(plan));

    constexpr std::size_t kGuard = 512;
    std::vector<unsigned char> buf(plan.Bytes() + kGuard, 0x5A);
    Writer fill = Writer::Filling(plan, buf.data(), buf.size());
    Encode(bigger, fill);

    CHECK_MSG(!fill.Filled(),
              "a diverging fill pass was accepted");

    std::size_t past = 0;
    for (std::size_t i = plan.Bytes(); i < buf.size(); ++i)
        if (buf[i] != 0x5A)
            ++past;
    CHECK_MSG(past == 0,
              std::to_string(past) + " bytes beyond the plan were "
              "written");

    Writer measureBig = Writer::Measuring();
    Encode(bigger, measureBig);
    Writer::Plan bigPlan;
    CHECK(measureBig.Measured(bigPlan));

    std::vector<unsigned char> roomy(bigPlan.Bytes(), 0);
    Writer underfill = Writer::Filling(bigPlan, roomy.data(), roomy.size());
    Encode(small, underfill);

    CHECK_MSG(underfill.Size() < bigPlan.Bytes(),
              "the case is set up wrong: no less was "
              "written");
    CHECK_MSG(!underfill.Filled(),
              "a fill pass that was too short was accepted as complete");

    SecureBuffer out;
    std::size_t len = 123;
    const Status st = EncodeToSecure(small, out, len, 0, 16);
    CHECK(!st.IsOk());
    CHECK_EQ(len, std::size_t{ 0 });
    CHECK_MSG(!out.IsValid(), "memory was allocated despite the rejection");
}

TEST_CASE(Codec_AShortBufferWritesNothing)
{
    const Document doc = FatDocument(2);

    Writer measure = Writer::Measuring();
    Encode(doc, measure);
    Writer::Plan plan;
    CHECK(measure.Measured(plan));
    CHECK(plan.Bytes() > 8);

    const std::size_t tight = plan.Bytes() - 1;
    std::vector<unsigned char> field(plan.Bytes() + 512, 0xA5);
    Writer fill = Writer::Filling(plan, field.data(), tight);
    Encode(doc, fill);

    CHECK_MSG(!fill.Filled(), "a buffer that is too small was accepted");

    CHECK_MSG(fill.Size() == 0,
              "a poisoned fill pass counted " +
                  std::to_string(fill.Size()) + " bytes");

    std::size_t touched = 0;
    for (unsigned char b : field)
        if (b != 0xA5)
            ++touched;
    CHECK_MSG(touched == 0,
              std::to_string(touched) + " bytes were written anyway");
}

TEST_CASE(Codec_FillStaysInsideThePlan)
{
    const Document doc = FatDocument(2);

    Writer measure = Writer::Measuring();
    Encode(doc, measure);
    Writer::Plan plan;
    CHECK(measure.Measured(plan));

    constexpr std::size_t kSlack = 64;
    std::vector<unsigned char> buf(plan.Bytes() + kSlack, 0x5A);
    Writer fill = Writer::Filling(plan, buf.data(), plan.Bytes());
    Encode(doc, fill);
    CHECK(fill.Filled());

    std::size_t touched = 0;
    for (std::size_t i = plan.Bytes(); i < buf.size(); ++i)
        if (buf[i] != 0x5A)
            ++touched;
    CHECK_MSG(touched == 0,
              "the writer wrote " + std::to_string(touched) +
                  " bytes into the padding");
}

TEST_CASE(Codec_TheWriterOwnsNoOrdinaryHeap)
{
    const bool ownsNoHeap = std::is_trivially_copyable_v<Writer>;
    CHECK_MSG(ownsNoHeap, "the writer owns a heap buffer again");
}

TEST_CASE(Codec_NestingTooDeepFailsInsteadOfGrowing)
{
    Writer w = Writer::Measuring();
    for (int i = 0; i < 9; ++i)
        w.BeginRecord(T(Tag::Entry));

    CHECK_MSG(w.Failed(), "nine levels were accepted without complaint");

    Writer ok = Writer::Measuring();
    for (int i = 0; i < 8; ++i)
        ok.BeginRecord(T(Tag::Entry));
    CHECK_MSG(!ok.Failed(), "eight levels were rejected");
}

TEST_CASE(Codec_TheSearchWorksAtAll)
{
    const char* kCanary = "CANARYVOGEL-0123456789-ABCDEF";

    Document doc;
    Entry e;
    e.Title = "Canary";
    e.Fields.push_back(Make("Password", kCanary, true));
    CHECK(AddEntry(doc, std::move(e), kNow).IsOk());

    SecureBuffer body;
    std::size_t len = 0;
    const Status st = EncodeToSecure(doc, body, len, 0, 1u << 20);
    CHECK(st.IsOk());
    CHECK(body.IsValid());
    CHECK(len > 0);

    CHECK_MSG(Contains(body.Data(), len, kCanary),
              "the canary is not in the filled body -- so "
              "none of the search cases measures anything");
}

TEST_CASE(Codec_EarlyRefusalHoldsNoBody)
{
    {
        Document doc;
        Entry e;
        e.Title = "Vergiftet";
        std::string poisoned = "pre";
        poisoned.push_back('\0');
        poisoned += "post";
        Field f;
        f.Key = "Password";
        f.Value = poisoned;
        f.Flags = kFieldFlagSecret;
        e.Fields.push_back(std::move(f));
        CHECK(AddEntry(doc, std::move(e), kNow).IsOk());

        SecureBuffer out;
        std::size_t len = 7;
        const Status st = EncodeToSecure(doc, out, len, 0, 1u << 20);
        CHECK_MSG(!st.IsOk(), "a NUL byte was written without complaint");
        CHECK_EQ(len, std::size_t{ 0 });
        CHECK_MSG(!out.IsValid(),
                  "memory was allocated although the request was rejected");
    }

    {
        const Document doc = FatDocument(5);
        SecureBuffer out;
        std::size_t len = 7;
        const Status st = EncodeToSecure(doc, out, len, 0, 64);
        CHECK(!st.IsOk());
        CHECK_EQ(len, std::size_t{ 0 });
        CHECK_MSG(!out.IsValid(),
                  "a body that is too large was allocated anyway");
    }

    {
        const Document doc = FatDocument(5);
        SecureBuffer out;
        std::size_t len = 0;
        const Status st = EncodeToSecure(doc, out, len, 32, 1u << 20);
        CHECK_MSG(st.IsOk(), "the valid case was rejected as well");
        CHECK(out.IsValid());
        CHECK(len > 0);
        CHECK_EQ(out.Size(), len + 32);
    }
}

TEST_CASE(Codec_SecureStringRoundTrips)
{
    const SecureString secret = "WIEDERHERSTELLUNG-0123456789";
    Body body([&](Writer& w) { w.WriteString(T(Tag::RecoveryCodeValue), secret); });
    CHECK(body.Ok());

    Reader r(body.data(), body.size());
    std::uint16_t tag = 0;
    const unsigned char* p = nullptr;
    std::size_t n = 0;
    CHECK(r.Next(tag, p, n));
    CHECK_EQ(tag, T(Tag::RecoveryCodeValue));

    SecureString back;
    CHECK(Reader::AsString(p, n, back));
    CHECK(back == secret);
    CHECK_MSG(back.InArena(), "the value read is not in the arena");
}

TEST_CASE(Codec_SecureStringWithANulIsRefusedBothWays)
{
    std::string poisoned = "pre";
    poisoned.push_back('\0');
    poisoned += "post";

    SecureString s;
    s = poisoned;
    Body body([&](Writer& w) { w.WriteString(T(Tag::RecoveryCodeValue), s); });
    CHECK_MSG(body.Failed(), "a value with a NUL byte was written");

    SecureString out = "vorher";
    CHECK_MSG(!Reader::AsString(reinterpret_cast<const unsigned char*>(poisoned.data()),
                                poisoned.size(), out),
              "a value with a NUL byte was read");
}

TEST_CASE(Codec_ALiteralStillWritesAsText)
{
    Body literal([](Writer& w) { w.WriteString(T(Tag::TotpAlgorithm), "SHA1"); });
    Body viaString([](Writer& w) { w.WriteString(T(Tag::TotpAlgorithm), std::string("SHA1")); });
    CHECK(literal.Ok());
    CHECK(literal == viaString);
    CHECK_EQ(literal.size(), std::size_t{ 6 + 4 });
}

TEST_CASE(Codec_AnEmptySecureStringIsAValidValue)
{
    const SecureString empty;
    Body body([&](Writer& w) { w.WriteString(T(Tag::RecoveryCodeValue), empty); });
    CHECK(body.Ok());
    CHECK_EQ(body.size(), std::size_t{ 6 });

    SecureString out = "vorher";
    CHECK(Reader::AsString(body.data() + 6, 0, out));
    CHECK(out.Empty());
}

TEST_CASE(Codec_AsSecureFailsWhenTheValueIsLost)
{
    const std::string big(9000, 'q');
    const unsigned char* p = reinterpret_cast<const unsigned char*>(big.data());

    SecureString out = "vorher";
    {
        const NoHeapFallbackForTest noHeap;
        CHECK_MSG(!Reader::AsString(p, big.size(), out),
                  "a lost value was reported as read");
    }
    CHECK(out.Empty());

    CHECK(Reader::AsString(p, big.size(), out));
    CHECK_EQ(out.Size(), big.size());
}

TEST_CASE(Codec_OpeningFailsWhenASecretIsLost)
{
    for (int which = 0; which < 2; ++which)
    {
        Document doc;
        Entry e;
        e.Id = 1;
        e.Title = "Bank";
        if (which == 0)
        {
            e.HasTotp = true;
            e.Totp.Config.SecretBase32 = std::string(9000, 'A');
        }
        else
        {
            e.RecoveryCodes.push_back(RecoveryCode{});
            e.RecoveryCodes.back().Value = std::string(9000, 'C');
        }
        doc.Entries.push_back(std::move(e));
        doc.NextId = 2;

        Body body([&](Writer& w) { Encode(doc, w); });
        CHECK(body.Ok());

        {
            const NoHeapFallbackForTest noHeap;
            Document back;
            CHECK_MSG(!Decode(body.data(), body.size(), back),
                      which == 0 ? "a lost TOTP secret was reported as read"
                                 : "a lost code was reported as read");
        }

        Document back;
        CHECK(Decode(body.data(), body.size(), back));
        if (back.Entries.size() == 1)
        {
            if (which == 0)
                CHECK_EQ(back.Entries[0].Totp.Config.SecretBase32.Size(), std::size_t{ 9000 });
            else if (back.Entries[0].RecoveryCodes.size() == 1)
                CHECK_EQ(back.Entries[0].RecoveryCodes[0].Value.Size(), std::size_t{ 9000 });
        }
    }
}
