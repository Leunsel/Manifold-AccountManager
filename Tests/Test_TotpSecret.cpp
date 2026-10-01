#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "HeapWatch.h"

#include "Core/Base32.h"
#include "Core/CsvExport.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Otp.h"
#include "Core/OtpUri.h"
#include "Core/Secret.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace Bunker;
using namespace Bunker::Vault;
using Test::HeapWatch;

namespace
{
    constexpr std::string_view kCanonical = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    constexpr std::string_view kCanonicalLower = "gezdgnbvgy3tqojqgezdgnbvgy3tqojq";
    constexpr std::string_view kKeyBytes = "12345678901234567890";

    TotpConfig RfcConfig(std::string_view seed, HashAlgorithm algorithm)
    {
        TotpConfig c;
        CHECK(EncodeBase32(reinterpret_cast<const unsigned char*>(seed.data()),
                           seed.size(), c.SecretBase32).IsOk());
        c.Digits = 8;
        c.Algorithm = algorithm;
        return c;
    }

    using Test::HeapCopiesDuring;
}

TEST_CASE(HeapWatch_SeesACopyFreedWhileArmed)
{
    const char canary[] = "CANARYVOGEL-HEAPWATCH-0123456789";
    const std::size_t hits = HeapCopiesDuring({ canary }, [&] {
        std::string copy(canary);
        (void)copy;
    });
    CHECK_MSG(hits == 1, "the guard saw " + std::to_string(hits) +
                             " copies instead of one -- so none of the cases "
                             "below measures anything");
}

TEST_CASE(HeapWatch_IgnoresWhatDiesOutsideTheWindow)
{
    const char canary[] = "CANARYVOGEL-HEAPWATCH-0123456789";
    HeapWatch watch;
    CHECK(watch.Watch(canary));
    {
        std::string before(canary);
        (void)before;
    }
    watch.Arm();
    watch.Disarm();
    {
        std::string after(canary);
        (void)after;
    }
    CHECK_EQ(watch.Hits(), std::size_t{ 0 });
}

TEST_CASE(HeapWatch_SeesTheBytesAfterAnEmbeddedNul)
{
    const unsigned char needle[] = { 0x00, 0x11, 0x00, 0x22, 0x33, 0x00, 0x44, 0x55 };
    HeapWatch watch;
    CHECK(watch.Watch(needle, sizeof(needle)));
    watch.Arm();
    {
        std::string holder(64, 'x');
        std::memcpy(&holder[20], needle, sizeof(needle));
    }
    watch.Disarm();
    CHECK_EQ(watch.Hits(), std::size_t{ 1 });
}

TEST_CASE(HeapWatch_SeesACopyThatWasOnlyCleared)
{
    const char canary[] = "CANARYVOGEL-HEAPWATCH-0123456789";
    const std::size_t cleared = HeapCopiesDuring({ canary }, [&] {
        std::string copy(canary);
        copy.clear();
    });
    CHECK_MSG(cleared == 1, "a copy that was only cleared stayed invisible");

    const std::size_t nicked = HeapCopiesDuring({ canary }, [&] {
        std::string copy(canary);
        copy.front() = '#';
        copy.back() = '#';
    });
    CHECK_MSG(nicked == 1, "a copy changed at both ends stayed invisible");
}

TEST_CASE(HeapWatch_SeesAnOverAlignedBlock)
{
    struct alignas(64) Box
    {
        char Data[64];
    };
    const char canary[] = "CANARYVOGEL-HEAPWATCH-0123456789";
    const std::size_t hits = HeapCopiesDuring({ canary }, [&] {
        Box* box = new Box{};
        std::memcpy(box->Data, canary, sizeof(canary));
        delete box;
    });
    CHECK_MSG(hits == 1, "an aligned block was not searched");
}

TEST_CASE(Totp_TheDecodedKeyIsWipedOnEveryPath)
{
    const TotpConfig config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    unsigned char buffer[kMaxTotpKeyBytes + 32];

    auto look = [&buffer](std::size_t& dirty, std::size_t& sentinel) {
        dirty = sentinel = 0;
        for (std::size_t i = 0; i < kMaxTotpKeyBytes; ++i)
            if (buffer[i] != 0)
                ++dirty;
        for (std::size_t i = kMaxTotpKeyBytes; i < sizeof(buffer); ++i)
            if (buffer[i] == 0x5A)
                ++sentinel;
    };

    std::memset(buffer, 0x5A, sizeof(buffer));
    bool sawKey = false;
    const Status ok = WithDecodedTotpKey<Status>(
        config.SecretBase32, buffer, kMaxTotpKeyBytes,
        [&](const unsigned char* key, std::size_t n) {
            sawKey = n == kKeyBytes.size() &&
                     std::memcmp(key, kKeyBytes.data(), n) == 0;
            return Status{};
        });
    CHECK(ok.IsOk());
    CHECK_MSG(sawKey, "the key never arrived -- the case measures nothing");
    std::size_t dirty = 0, sentinel = 0;
    look(dirty, sentinel);
    CHECK_MSG(dirty == 0, std::to_string(dirty) + " bytes of the key remained");
    CHECK_EQ(sentinel, std::size_t{ 32 });

    TotpConfig broken;
    broken.SecretBase32 = "JBSWY3DPEHPK3PX1";
    std::memset(buffer, 0x5A, sizeof(buffer));
    bool called = false;
    const Status bad = WithDecodedTotpKey<Status>(
        broken.SecretBase32, buffer, kMaxTotpKeyBytes,
        [&](const unsigned char*, std::size_t) {
            called = true;
            return Status{};
        });
    CHECK(!bad.IsOk());
    CHECK(bad.Code() == ErrorCode::OtpSecretInvalid);
    CHECK(!called);
    look(dirty, sentinel);
    CHECK_EQ(dirty, std::size_t{ 0 });
    CHECK_EQ(sentinel, std::size_t{ 32 });
}

TEST_CASE(Totp_ABrokenSecretIsReportedAsSuch)
{
    TotpConfig broken;
    broken.SecretBase32 = "!!!";
    CHECK(TotpFromConfig(broken, 59).Code() == ErrorCode::OtpSecretInvalid);
    CHECK(ValidateTotp(broken).Code() == ErrorCode::OtpSecretInvalid);

    TotpConfig empty;
    CHECK(TotpFromConfig(empty, 59).Code() == ErrorCode::OtpSecretInvalid);

    CHECK(ParseBareSecret("not base32!").Code() == ErrorCode::OtpSecretInvalid);
}

TEST_CASE(Totp_FromConfigCoversEveryAlgorithm)
{
    struct Case { const char* Seed; HashAlgorithm Algorithm; const char* Want; };
    const Case cases[] = {
        { "12345678901234567890", HashAlgorithm::Sha1, "94287082" },
        { "12345678901234567890123456789012", HashAlgorithm::Sha256, "46119246" },
        { "1234567890123456789012345678901234567890123456789012345678901234",
          HashAlgorithm::Sha512, "90693936" },
    };
    for (const Case& c : cases)
    {
        const TotpConfig config = RfcConfig(c.Seed, c.Algorithm);
        const auto code = TotpFromConfig(config, 59);
        CHECK(code.IsOk());
        if (code.IsOk())
            CHECK_MSG(code.Value() == c.Want,
                      "code " + code.Value() + " instead of " + c.Want);
    }
}

TEST_CASE(Totp_FromConfigRefusesADigitCountThatWouldWrap)
{
    TotpConfig config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    config.Digits = 262;
    CHECK_MSG(!TotpFromConfig(config, 59).IsOk(),
              "262 digits were narrowed to 6 and accepted");

    config.Digits = 8;
    CHECK(TotpFromConfig(config, 59).IsOk());
}

TEST_CASE(Totp_FromConfigTakesTheLongestAcceptedSecretAndNoMore)
{
    TotpConfig longest;
    longest.SecretBase32 = std::string(kMaxTotpSecretBytes, 'A');
    CHECK(ValidateTotp(longest).IsOk());
    CHECK_MSG(TotpFromConfig(longest, 59).IsOk(),
              "the longest allowed secret does not fit the stack buffer");

    TotpConfig tooLong;
    tooLong.SecretBase32 = std::string(kMaxTotpSecretBytes + 8, 'A');
    CHECK(!TotpFromConfig(tooLong, 59).IsOk());
}

TEST_CASE(Totp_FromConfigToleratesAnOlderSpelling)
{
    TotpConfig canonical = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    TotpConfig older = canonical;
    older.SecretBase32 = "gezd gnbv gy3t qojq-gezd gnbv gy3t qojq";

    const auto a = TotpFromConfig(canonical, 59);
    const auto b = TotpFromConfig(older, 59);
    CHECK(a.IsOk() && b.IsOk());
    if (a.IsOk() && b.IsOk())
        CHECK(a.Value() == b.Value());
}

TEST_CASE(Totp_ComputingACodeLeavesNoHeapCopy)
{
    const TotpConfig config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes }, [&] {
        const auto code = TotpFromConfig(config, 59);
        ok = code.IsOk() && code.Value() == "94287082";
    });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) +
                             " heap blocks held the secret or the key");
}

TEST_CASE(Totp_ValidatingLeavesNoHeapCopy)
{
    const TotpConfig config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes }, [&] {
        ok = ValidateTotp(config).IsOk();
    });
    CHECK(ok);
    CHECK_EQ(hits, std::size_t{ 0 });
}

TEST_CASE(Totp_PastingABareSecretLeavesNoHeapCopy)
{
    const char typed[] = "gezd gnbv gy3t qojq gezd gnbv gy3t qojq";
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes, typed }, [&] {
        const auto r = ParsePastedSecret(typed);
        ok = r.IsOk() && r.Value().SecretBase32 == kCanonical;
    });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the secret");
}

TEST_CASE(Totp_PastingAUriLeavesNoHeapCopy)
{
    const char uri[] =
        "otpauth://totp/Acme:alex%40example.com?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
        "&issuer=Acme&digits=8&algorithm=SHA1";
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring(
        { kCanonical, kCanonicalLower, kKeyBytes, "secret=GEZD" }, [&] {
            const auto r = ParsePastedSecret(uri);
            ok = r.IsOk() && r.Value().SecretBase32 == kCanonical &&
                 r.Value().Digits == 8 && r.Value().Issuer == "Acme" &&
                 r.Value().AccountName == "alex@example.com";
        });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the secret");
}

TEST_CASE(Totp_PastingAPercentEncodedSecretLeavesNoHeapCopy)
{
    const char uri[] =
        "otpauth://totp/x?secret=GEZDGNBVGY3TQOJQ%47%45%5A%44GNBVGY3TQOJQ";
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes }, [&] {
        const auto r = ParsePastedSecret(uri);
        ok = r.IsOk() && r.Value().SecretBase32 == kCanonical;
    });
    CHECK(ok);
    CHECK_EQ(hits, std::size_t{ 0 });
}

TEST_CASE(Totp_ARejectedUriLeavesNoHeapCopy)
{
    const char noQuery[] =
        "otpauth://totp/x&secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    const char slashLate[] =
        "otpauth://totp?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=a/b";
    for (const char* uri : { noQuery, slashLate })
    {
        bool refused = false;
        const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower }, [&] {
            refused = !ParsePastedSecret(uri).IsOk();
        });
        CHECK_MSG(refused, std::string("accepted: ") + uri);
        CHECK_MSG(hits == 0, std::to_string(hits) +
                                 " heap blocks held the secret of a rejected "
                                 "address: " + uri);
    }
}

TEST_CASE(Totp_TheUriTypeIsCaseInsensitive)
{
    const auto r = ParseOtpAuthUri("otpauth://TOTP/x?secret=JBSWY3DPEHPK3PXP");
    CHECK(r.IsOk());
    CHECK(!ParseOtpAuthUri("otpauth://TOTPX/x?secret=JBSWY3DPEHPK3PXP").IsOk());
    CHECK(!ParseOtpAuthUri("otpauth://TOT/x?secret=JBSWY3DPEHPK3PXP").IsOk());
}

TEST_CASE(Totp_SettingAndClearingLeavesNoHeapCopy)
{
    Document doc;
    Entry draft;
    draft.Title = "Bank";
    const auto id = AddEntry(doc, std::move(draft), 100);
    CHECK(id.IsOk());
    const TotpConfig config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);

    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes }, [&] {
        ok = SetTotp(doc, id.Value(), config, 200).IsOk() &&
             ClearTotp(doc, id.Value(), 300).IsOk();
    });
    CHECK(ok);
    CHECK_EQ(hits, std::size_t{ 0 });
}

TEST_CASE(Totp_SavingAndOpeningLeavesNoHeapCopy)
{
    Document doc;
    Entry e;
    e.Title = "Bank";
    e.HasTotp = true;
    e.Totp.Config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    CHECK(AddEntry(doc, std::move(e), 100).IsOk());

    bool ok = false;
    const std::size_t hits = HeapCopiesDuring({ kCanonical, kCanonicalLower, kKeyBytes }, [&] {
        SecureBuffer body;
        std::size_t len = 0;
        if (!EncodeToSecure(doc, body, len, 0, 1u << 20).IsOk())
            return;
        Document back;
        ok = Decode(body.Data(), len, back) && back.Entries.size() == 1 &&
             back.Entries[0].Totp.Config.SecretBase32 == kCanonical;
    });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the secret");
}

TEST_CASE(Totp_ExportingLeavesNoHeapCopyOfTheSecondFactor)
{
    Document doc;
    Entry e;
    e.Title = "Bank";
    e.HasTotp = true;
    e.Totp.Config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    for (int i = 0; i < 8; ++i)
        e.RecoveryCodes.push_back(RecoveryCode{ "WIEDERHERSTELLUNG-0123456789", 0, {} });
    CHECK(AddEntry(doc, std::move(e), 100).IsOk());

    Entry plain;
    plain.Title = "Bank, nackt";
    plain.HasTotp = true;
    plain.Totp.Config = RfcConfig(kKeyBytes, HashAlgorithm::Sha1);
    plain.Totp.Config.Digits = kDefaultDigits;
    CHECK(AddEntry(doc, std::move(plain), 100).IsOk());

    CsvOptions options;
    options.IncludeSecondFactor = true;
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring(
        { kCanonical, kCanonicalLower, "WIEDERHERSTELLUNG-0123456789" }, [&] {
            SecureBuffer out;
            std::size_t length = 0;
            ok = WriteCsv(doc, options, out, length).IsOk() && length > 0;
        });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) +
                             " heap blocks held the secret or a code");
}

TEST_CASE(Totp_ASecretThatCannotBeStoredIsRefusedOnPaste)
{
    const std::string bare(9000, 'A');
    const std::string uri = "otpauth://totp/x?secret=" + bare;
    {
        const NoHeapFallbackForTest noHeap;
        CHECK_MSG(!ParseBareSecret(bare).IsOk(),
                  "a secret that cannot be stored was accepted");
        CHECK_MSG(!ParseOtpAuthUri(uri).IsOk(),
                  "an unstorable secret inside an address was accepted");
    }
    const auto a = ParseBareSecret(bare);
    const auto b = ParseOtpAuthUri(uri);
    CHECK(a.IsOk() && b.IsOk());
    if (a.IsOk())
        CHECK_EQ(a.Value().SecretBase32.Size(), bare.size());
}

TEST_CASE(Totp_EncodingEmptiesTheTargetWhenItCannotStore)
{
    SecureString out = "ALTERWERTALTERWERT";
    const std::string bytes(5625, 'k');
    {
        const NoHeapFallbackForTest noHeap;
        CHECK(!EncodeBase32(reinterpret_cast<const unsigned char*>(bytes.data()),
                            bytes.size(), out).IsOk());
    }
    CHECK(out.Empty());
}

TEST_CASE(Totp_EncodingKnowsEmptyFromMissing)
{
    SecureString out = "ALTERWERTALTERWERT";
    CHECK(EncodeBase32(nullptr, 0, out).IsOk());
    CHECK_MSG(out.Empty(), "an empty input left the old value");

    out = "ALTERWERTALTERWERT";
    CHECK_MSG(!EncodeBase32(nullptr, 5, out).IsOk(),
              "five bytes without a pointer were reported as an empty success");
    CHECK(out.Empty());
}

TEST_CASE(Totp_ARawNulInAUriIsRefused)
{
    std::string label = "otpauth://totp/a";
    label.push_back('\0');
    label += "b?secret=JBSWY3DPEHPK3PXP";
    CHECK_MSG(!ParseOtpAuthUri(label).IsOk(), "a NUL byte in the label was accepted");

    std::string issuer = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&issuer=a";
    issuer.push_back('\0');
    issuer += "b";
    CHECK_MSG(!ParseOtpAuthUri(issuer).IsOk(), "a NUL byte in the issuer was accepted");

    CHECK(ParseOtpAuthUri("otpauth://totp/ab?secret=JBSWY3DPEHPK3PXP").IsOk());
    CHECK(ParseOtpAuthUri("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&issuer=ab").IsOk());
}

TEST_CASE(Totp_ReadsOnlyTheViewItIsGiven)
{
    const char buffer[] = "JBSWY3DPEHPK3PXPMZXW6YTB";
    const auto bare = ParseBareSecret(std::string_view(buffer, 16));
    CHECK(bare.IsOk());
    if (bare.IsOk())
        CHECK(bare.Value().SecretBase32 == "JBSWY3DPEHPK3PXP");

    const char uri[] = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=8";
    const auto cut = ParseOtpAuthUri(std::string_view(uri, std::strlen(uri) - 9));
    CHECK(cut.IsOk());
    if (cut.IsOk())
        CHECK_MSG(cut.Value().Digits == kDefaultDigits,
                  "reading continued past the end of the view");
}
