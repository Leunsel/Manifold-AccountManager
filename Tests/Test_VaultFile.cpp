#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/Model.h"
#include "Core/Palette.h"
#include "Core/VaultFile.h"
#include "Core/VaultFormat.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    const char kPath[] = "test.vault";
    const char kPassword[] = "a fairly long master password";

    void Ready()
    {
        static bool done = false;
        if (!done) { CHECK(Crypto::Initialise().IsOk()); done = true; }
    }

    Crypto::KdfParams Fast() { return Crypto::KdfParams::Interactive(); }

    Crypto::KdfParams Cheap() { return Crypto::KdfParams::TestOnlyMinimum(); }

    Entry MakeEntry(std::uint64_t id, const std::string& title, const std::string& platform)
    {
        Entry e;
        e.Id = id;
        e.Title = title;
        e.PlatformId = platform;
        e.CreatedUtc = 1700000000ull + id;
        e.ModifiedUtc = 1700000500ull + id;
        Field user; user.Key = "Benutzername"; user.Value = "alex.doe";
        Field pw;   pw.Key = "Password";      pw.Value = "hunter2";  pw.Flags = kFieldFlagSecret;
        e.Fields.push_back(user);
        e.Fields.push_back(pw);
        return e;
    }
}

TEST_CASE(Vault_CreateUnlockRoundTrip)
{
    Ready();
    MemoryFileSystem fs;

    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Fast(), v).IsOk());
        CHECK(v.IsUnlocked());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        v.Doc().Entries.push_back(MakeEntry(2, "Große Übersicht € \U0001F510", "google"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    VaultFile v2;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), v2).IsOk());
    CHECK_EQ(v2.Doc().Entries.size(), std::size_t{ 2 });
    CHECK(v2.Doc().Entries[0].Title == "Steam");
    CHECK(v2.Doc().Entries[1].Title == "Große Übersicht € \U0001F510");
    CHECK_EQ(v2.Doc().Entries[0].Fields.size(), std::size_t{ 2 });
    CHECK(v2.Doc().Entries[0].Fields[1].IsSecret());
    CHECK(v2.Doc().Entries[0].Fields[1].Value == "hunter2");
}

TEST_CASE(Vault_EmptyAndLargeVaults)
{
    Ready();
    MemoryFileSystem fs;

    VaultFile empty;
    CHECK(VaultFile::Create(fs, "empty.vault", kPassword, std::strlen(kPassword), Cheap(), empty).IsOk());
    VaultFile back;
    CHECK(VaultFile::Unlock(fs, "empty.vault", kPassword, std::strlen(kPassword), back).IsOk());
    CHECK_EQ(back.Doc().Entries.size(), std::size_t{ 0 });

    VaultFile many;
    CHECK(VaultFile::Create(fs, "many.vault", kPassword, std::strlen(kPassword), Cheap(), many).IsOk());
    for (std::uint64_t i = 1; i <= 1000; ++i)
        many.Doc().Entries.push_back(MakeEntry(i, "Entry " + std::to_string(i), "steam"));
    CHECK(many.Save(fs, "many.vault").IsOk());

    VaultFile many2;
    CHECK(VaultFile::Unlock(fs, "many.vault", kPassword, std::strlen(kPassword), many2).IsOk());
    CHECK_EQ(many2.Doc().Entries.size(), std::size_t{ 1000 });
    CHECK(many2.Doc().Entries[999].Title == "Entry 1000");
}

TEST_CASE(Vault_WrongPasswordIsNotCorruption)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Fast(), v).IsOk());

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, kPath, "wrong!", 6, out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::WrongPassword,
              std::string("got ") + ToString(st.Code()));
    CHECK(!out.IsUnlocked());
    CHECK_EQ(out.Doc().Entries.size(), std::size_t{ 0 });
}

TEST_CASE(Vault_DamagedBodyIsNotWrongPassword)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> raw;
    CHECK(fs.Read(kPath, raw).IsOk());
    raw[HeaderLength(1) + 8] ^= 0x01;
    fs.Put(kPath, raw);

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::FileTamperedOrCorrupt,
              std::string("got ") + ToString(st.Code()));
}

TEST_CASE(Vault_EveryByteFlipIsDetected)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> original;
    CHECK(fs.Read(kPath, original).IsOk());

    std::size_t tested = 0, accepted = 0;
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        std::vector<unsigned char> bad = original;
        bad[i] ^= 0x01;
        fs.Put(kPath, bad);

        VaultFile out;
        const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), out);
        ++tested;
        if (st.IsOk())
            ++accepted;
    }

    CHECK_MSG(tested == original.size(),
              "only " + std::to_string(tested) + " of " +
              std::to_string(original.size()) + " bytes checked");
    CHECK_MSG(accepted == 0,
              std::to_string(accepted) + " tampered files were accepted");
}

TEST_CASE(Vault_KdfParametersCannotBeDowngraded)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> raw;
    CHECK(fs.Read(kPath, raw).IsOk());

    std::uint64_t original = 0;
    for (int i = 7; i >= 0; --i)
        original = (original << 8) | raw[HeaderOffset::KdfOpsLimit + i];
    const std::uint64_t tampered = original + 1;
    CHECK_MSG(tampered != original, "the tampering would have no effect");
    for (int i = 0; i < 8; ++i)
        raw[HeaderOffset::KdfOpsLimit + i] =
            static_cast<unsigned char>((tampered >> (8 * i)) & 0xFF);

    std::vector<unsigned char> pristine;
    CHECK(fs.Read(kPath, pristine).IsOk());
    CHECK_MSG(std::memcmp(raw.data() + HeaderOffset::KdfOpsLimit,
                          pristine.data() + HeaderOffset::KdfOpsLimit, 8) != 0,
              "the header was not changed at all - the test would check nothing");
    unsigned char sum[kChecksumBytes];
    CHECK(Crypto::Hash(sum, sizeof(sum), raw.data(), HeaderLength(1) - kChecksumBytes).IsOk());
    std::memcpy(raw.data() + HeaderLength(1) - kChecksumBytes, sum, sizeof(sum));
    fs.Put(kPath, raw);

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::WrongPassword,
              std::string("got ") + ToString(st.Code()));
}

TEST_CASE(Vault_SlotBytesAreCoveredByBodyAad)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> raw;
    CHECK(fs.Read(kPath, raw).IsOk());

    const std::size_t flagsAt = kFixedHeaderBytes + SlotOffset::Flags;
    CHECK_MSG(raw[flagsAt] == 0, "slot flags should be zero when written");
    raw[flagsAt] = 0x5A;

    unsigned char sum[kChecksumBytes];
    CHECK(Crypto::Hash(sum, sizeof(sum), raw.data(), HeaderLength(1) - kChecksumBytes).IsOk());
    std::memcpy(raw.data() + HeaderLength(1) - kChecksumBytes, sum, sizeof(sum));
    fs.Put(kPath, raw);

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::FileTamperedOrCorrupt,
              std::string("got ") + ToString(st.Code()));
}

TEST_CASE(Vault_RejectsHostileHeaders)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
    std::vector<unsigned char> good;
    CHECK(fs.Read(kPath, good).IsOk());

    auto expectFailure = [&](const char* what, const char* because,
                             std::vector<unsigned char> raw) {
        fs.Put("hostile.vault", std::move(raw));
        VaultFile out;
        const Status st = VaultFile::Unlock(fs, "hostile.vault", kPassword,
                                            std::strlen(kPassword), out);
        CHECK_MSG(!st.IsOk(), std::string("accepted although: ") + what);
        CHECK(!out.IsUnlocked());
        if (because)
        {
            const char* detail = st.GetError().Detail;
            CHECK_MSG(detail && std::strcmp(detail, because) == 0,
                      std::string("rejected because of '") + (detail ? detail : "(no detail)") +
                      "', expected '" + because + "'");
        }
    };

    expectFailure("empty file", "shorter than the header", {});
    expectFailure("a single byte", "shorter than the header", { 'B' });
    expectFailure("truncated in the header", "shorter than the header",
                  std::vector<unsigned char>(good.begin(), good.begin() + 40));
    expectFailure("header complete, body missing", "body shorter than the auth tag",
                  std::vector<unsigned char>(good.begin(), good.begin() + HeaderLength(1)));

    { auto r = good; r[0] = 'X';
      expectFailure("falsche Signatur", "wrong magic bytes", r); }
    { auto r = good; r[HeaderOffset::VersionMajor] = 99;
      expectFailure("neuere Hauptversion", "newer major version", r); }
    { auto r = good; r[HeaderOffset::SlotCount] = 0;
      expectFailure("null Slots", "implausible slot count", r); }
    { auto r = good; r[HeaderOffset::SlotCount] = 200;
      expectFailure("200 Slots", "implausible slot count", r); }
    { auto r = good; r[HeaderOffset::HeaderLength] = 0xFF;
      expectFailure("absurd header length", "header length does not match", r); }
    { auto r = good; r[HeaderOffset::KdfAlgorithm] = 77;
      expectFailure("unbekanntes KDF", "unknown KDF algorithm", r); }
    {
        auto r = good;
        const std::uint64_t huge = 64ull * 1024 * 1024 * 1024;
        for (int i = 0; i < 8; ++i)
            r[HeaderOffset::KdfMemLimit + i] = static_cast<unsigned char>((huge >> (8 * i)) & 0xFF);
        expectFailure("64 GiB Speicherbedarf", "nonsensical KDF parameters in the header", r);
    }
}

TEST_CASE(Vault_SaveIsAtomicUnderFaults)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Original", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> before;
    CHECK(fs.Read(kPath, before).IsOk());

    const FaultPoint points[] = { FaultPoint::AfterCreateTemp, FaultPoint::AfterWrite,
                                  FaultPoint::AfterFlush, FaultPoint::DuringReplace };
    for (FaultPoint fp : points)
    {
        VaultFile v;
        CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), v).IsOk());
        v.Doc().Entries[0].Title = "Changed";

        fs.SetFaultPoint(fp);
        CHECK(!v.Save(fs, kPath).IsOk());
        fs.SetFaultPoint(FaultPoint::None);

        std::vector<unsigned char> after;
        CHECK(fs.Read(kPath, after).IsOk());
        CHECK_MSG(after == before, "the original file was changed on a failure");

        VaultFile check;
        CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), check).IsOk());
        CHECK(check.Doc().Entries[0].Title == "Original");
    }
}

TEST_CASE(Vault_ChangePasswordInvalidatesTheOldOne)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Fast(), v).IsOk());
        v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> beforeRaw;
    CHECK(fs.Read(kPath, beforeRaw).IsOk());

    const char newPw[] = "a completely different password";
    {
        VaultFile v;
        CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), v).IsOk());
        CHECK(v.ChangePassword(fs, kPath, newPw, std::strlen(newPw), Fast()).IsOk());
    }

    VaultFile withNew;
    CHECK(VaultFile::Unlock(fs, kPath, newPw, std::strlen(newPw), withNew).IsOk());
    CHECK(withNew.Doc().Entries[0].Title == "Steam");

    VaultFile withOld;
    const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), withOld);
    CHECK(!st.IsOk());
    CHECK(st.Code() == ErrorCode::WrongPassword);

    std::vector<unsigned char> afterRaw;
    CHECK(fs.Read(kPath, afterRaw).IsOk());
    CHECK(std::memcmp(beforeRaw.data() + HeaderOffset::KdfSalt,
                      afterRaw.data() + HeaderOffset::KdfSalt, Crypto::kSaltBytes) != 0);
}

TEST_CASE(Vault_RepeatedSavesDifferOnDisk)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Fast(), v).IsOk());
    v.Doc().Entries.push_back(MakeEntry(1, "Steam", "steam"));

    CHECK(v.Save(fs, kPath).IsOk());
    std::vector<unsigned char> a;
    CHECK(fs.Read(kPath, a).IsOk());

    CHECK(v.Save(fs, kPath).IsOk());
    std::vector<unsigned char> b;
    CHECK(fs.Read(kPath, b).IsOk());

    CHECK_EQ(a.size(), b.size());
    CHECK(a != b);
    CHECK(std::memcmp(a.data() + HeaderOffset::BodyNonce,
                      b.data() + HeaderOffset::BodyNonce, Crypto::kNonceBytes) != 0);
    CHECK(std::memcmp(a.data() + kFixedHeaderBytes + SlotOffset::WrapNonce,
                      b.data() + kFixedHeaderBytes + SlotOffset::WrapNonce,
                      Crypto::kNonceBytes) != 0);
}

TEST_CASE(Vault_BodySizeIsPaddedToBlocks)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, "pad.vault", kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    std::vector<unsigned char> sizes;
    std::size_t firstBody = 0;
    for (int n = 0; n < 5; ++n)
    {
        v.Doc().Entries.push_back(MakeEntry(static_cast<std::uint64_t>(n + 1),
                                            "Entry", "steam"));
        CHECK(v.Save(fs, "pad.vault").IsOk());
        std::vector<unsigned char> raw;
        CHECK(fs.Read(fs.Paths().empty() ? "pad.vault" : "pad.vault", raw).IsOk());
        const std::size_t body = raw.size() - HeaderLength(1) - Crypto::kTagBytes;
        CHECK_MSG(body % 4096 == 0, "body is not a multiple of the block size");
        if (n == 0) firstBody = body;
    }
    std::vector<unsigned char> last;
    CHECK(fs.Read("pad.vault", last).IsOk());
    CHECK_EQ(last.size() - HeaderLength(1) - Crypto::kTagBytes, firstBody);
}

TEST_CASE(Codec_UnknownRecordsSurviveARoundTrip)
{
    Document doc;
    doc.Entries.push_back(Entry{});
    doc.Entries[0].Id = 7;
    doc.Entries[0].Title = "Zukunft";
    doc.Entries[0].Unknown.push_back(UnknownRecord{ 900, { 1, 2, 3, 4 } });
    doc.Unknown.push_back(UnknownRecord{ 800, { 9, 9, 9 } });
    doc.NextId = 8;

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK(w.IsBalanced());

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].Unknown[0].Tag, std::uint16_t{ 900 });
    CHECK_EQ(back.Unknown.size(), std::size_t{ 1 });

    Body w2([&](Writer& out) {
        Encode(back, out);
    });
    CHECK(w2 == w);
}

TEST_CASE(Codec_IdCounterRepairConvergesAfterOnePass)
{
    Document stale;
    stale.Entries.push_back(Entry{});
    stale.Entries[0].Id = 7;
    stale.Entries[0].Title = "Zukunft";
    stale.NextId = 1;

    Body first([&](Writer& out) {
        Encode(stale, out);
    });

    Document once;
    CHECK(Decode(first.data(), first.size(), once));
    CHECK_EQ(once.NextId, std::uint64_t{ 8 });

    Body second([&](Writer& out) {
        Encode(once, out);
    });
    CHECK(second != first);

    Document twice;
    CHECK(Decode(second.data(), second.size(), twice));
    CHECK_EQ(twice.NextId, std::uint64_t{ 8 });

    Body third([&](Writer& out) {
        Encode(twice, out);
    });
    CHECK_MSG(third == second,
              "the repair never settles -- the file changes on every load");
}

TEST_CASE(Codec_RejectsMalformedInput)
{
    unsigned char bad[] = { 0x0A, 0x00, 0xFF, 0xFF, 0xFF, 0x7F, 0x01 };
    Reader r(bad, sizeof(bad));
    std::uint16_t tag = 0; const unsigned char* p = nullptr; std::size_t n = 0;
    CHECK(!r.Next(tag, p, n));
    CHECK(r.Failed());

    unsigned char stub[] = { 0x01, 0x00, 0x00 };
    Reader r2(stub, sizeof(stub));
    CHECK(!r2.Next(tag, p, n));
    CHECK(r2.Failed());

    unsigned char overflow[] = { 0x0A, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };
    Reader r3(overflow, sizeof(overflow));
    CHECK(!r3.Next(tag, p, n));
    CHECK(r3.Failed());

    Reader r4(nullptr, 0);
    CHECK(!r4.Next(tag, p, n));
    CHECK(!r4.Failed());
}

TEST_CASE(Codec_RejectsEmbeddedNulInStrings)
{
    const unsigned char withNul[] = { 'a', 0x00, 'b' };
    Body w([&](Writer& out) {
        out.WriteBytes(static_cast<std::uint16_t>(Tag::EntryTitle),
                       withNul, sizeof(withNul));
    });

    std::string s;
    Reader r(w.data(), w.size());
    std::uint16_t tag = 0; const unsigned char* p = nullptr; std::size_t n = 0;
    CHECK(r.Next(tag, p, n));
    CHECK(!Reader::AsString(p, n, s));
}

namespace
{
    TotpConfig SampleTotp()
    {
        TotpConfig c;
        c.SecretBase32 = "JBSWY3DPEHPK3PXP";
        c.Digits = 8;
        c.Period = 60;
        c.Algorithm = HashAlgorithm::Sha512;
        c.Issuer = "Beispielbank";
        c.AccountName = "kunde@example.de";
        return c;
    }
}

TEST_CASE(Vault_TotpAndRecoveryCodesSurviveARoundTrip)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Bank";
    e.HasTotp = true;
    e.Totp.Config = SampleTotp();
    e.RecoveryCodes.push_back(Vault::RecoveryCode{ "aaaa-bbbb", 0, {} });
    e.RecoveryCodes.push_back(Vault::RecoveryCode{ "cccc-dddd", 1700000000ull, {} });
    doc.Entries.push_back(e);
    doc.NextId = 2;

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK(w.IsBalanced());
    CHECK(!w.Failed());

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    const Entry& r = back.Entries[0];

    CHECK(r.HasTotp);
    CHECK(r.Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP");
    CHECK_EQ(r.Totp.Config.Digits, std::uint32_t{ 8 });
    CHECK_EQ(r.Totp.Config.Period, std::uint32_t{ 60 });
    CHECK(r.Totp.Config.Algorithm == HashAlgorithm::Sha512);
    CHECK(r.Totp.Config.Issuer == "Beispielbank");
    CHECK(r.Totp.Config.AccountName == "kunde@example.de");

    CHECK_EQ(r.RecoveryCodes.size(), std::size_t{ 2 });
    CHECK(r.RecoveryCodes[0].Value == "aaaa-bbbb");
    CHECK(!r.RecoveryCodes[0].IsUsed());
    CHECK(r.RecoveryCodes[1].Value == "cccc-dddd");
    CHECK(r.RecoveryCodes[1].IsUsed());
    CHECK_EQ(r.RecoveryCodes[1].UsedUtc, std::uint64_t{ 1700000000 });

    Body again([&](Writer& out) {
        Encode(back, out);
    });
    CHECK(again == w);
}

TEST_CASE(Vault_AlgorithmIsStoredByNameNotByEnumValue)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "x";
    e.HasTotp = true;
    e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    e.Totp.Config.Algorithm = HashAlgorithm::Sha256;
    doc.Entries.push_back(e);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    const std::string bytes(reinterpret_cast<const char*>(w.data()),
                            w.size());
    CHECK_MSG(bytes.find("SHA256") != std::string::npos,
              "the algorithm name is not in the file");
}

TEST_CASE(Vault_UnknownSubRecordsInsideTotpAndCodesSurvive)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Zukunft";
    e.HasTotp = true;
    e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    e.Totp.Unknown.push_back(UnknownRecord{ 306, { 1, 2, 3 } });

    Vault::RecoveryCode c;
    c.Value = "code";
    c.Unknown.push_back(UnknownRecord{ 402, { 9, 9 } });
    e.RecoveryCodes.push_back(c);
    doc.Entries.push_back(e);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;

    CHECK_EQ(back.Entries[0].Totp.Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].Totp.Unknown[0].Tag, std::uint16_t{ 306 });
    CHECK_EQ(back.Entries[0].RecoveryCodes.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].RecoveryCodes[0].Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].RecoveryCodes[0].Unknown[0].Tag, std::uint16_t{ 402 });
}

TEST_CASE(Vault_AbsentRecoveryRecordIsTheEmptyList)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Ohne";
    doc.Entries.push_back(e);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].RecoveryCodes.empty());
    CHECK(!back.Entries[0].HasTotp);
}

TEST_CASE(Vault_ASecondTotpRecordMakesTheEntryUnreadable)
{
    Body inner([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Doppelt");
        for (int i = 0; i < 2; ++i)
        {
            out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryTotp));
            out.WriteString(static_cast<std::uint16_t>(Tag::TotpSecret),
                            "JBSWY3DPEHPK3PXP");
            out.EndRecord();
        }
        out.EndRecord();
    });
    CHECK(inner.IsBalanced());

    Document back;
    CHECK_MSG(!Decode(inner.data(), inner.size(), back),
              "two TOTP records in one entry were accepted");
}

TEST_CASE(Vault_AnUnknownAlgorithmMakesTheEntryUnreadable)
{
    Body inner([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Fremd");
        out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryTotp));
        out.WriteString(static_cast<std::uint16_t>(Tag::TotpSecret), "JBSWY3DPEHPK3PXP");
        out.WriteString(static_cast<std::uint16_t>(Tag::TotpAlgorithm), "SHA3-256");
        out.EndRecord();
        out.EndRecord();
    });

    Document back;
    CHECK(!Decode(inner.data(), inner.size(), back));
}

TEST_CASE(Codec_WriteStringRefusesAnEmbeddedNul)
{
    std::string poisoned = "pre";
    poisoned.push_back('\0');
    poisoned += "post";
    CHECK_EQ(poisoned.size(), std::size_t{ 8 });

    Body w([&](Writer& out) {
        out.WriteString(1, poisoned);
    });
    CHECK_MSG(w.Failed(), "a NUL byte was written without complaint");

    Body clean([&](Writer& out) {
        out.WriteString(1, "harmlos");
    });
    CHECK(!clean.Failed());
}

TEST_CASE(Vault_PeekHeaderNeedsNoPassword)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    VaultFile::HeaderInfo info;
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK_EQ(info.VersionMajor, kVersionMajor);
    CHECK_MSG(info.Generation == v.Generation(),
              "the generation in the header differs from the one in memory");
    CHECK(info.SlotCount >= 1);
}

TEST_CASE(Vault_PeekHeaderRejectsWhatUnlockWouldReject)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile::HeaderInfo info;

    CHECK(!VaultFile::PeekHeader(fs, kPath, info).IsOk());

    fs.Put(kPath, std::vector<unsigned char>(10, 0));
    CHECK(!VaultFile::PeekHeader(fs, kPath, info).IsOk());

    fs.Put(kPath, std::vector<unsigned char>(300, 0x41));
    const Status foreign = VaultFile::PeekHeader(fs, kPath, info);
    CHECK(!foreign.IsOk());
    CHECK(foreign.GetError().Code == ErrorCode::FileTamperedOrCorrupt);
}

TEST_CASE(Vault_SaveRefusesAFileSomebodyElseWrote)
{
    Ready();
    MemoryFileSystem fs;

    VaultFile first;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(),
                            first).IsOk());

    VaultFile second;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), second).IsOk());
    CHECK_EQ(first.Generation(), second.Generation());

    second.Doc().Entries.push_back(MakeEntry(1, "From the second instance", "x"));
    CHECK(second.Save(fs, kPath).IsOk());

    first.Doc().Entries.push_back(MakeEntry(2, "From the first instance", "y"));
    const Status refused = first.Save(fs, kPath);
    CHECK_MSG(!refused.IsOk(), "the second writer silently overwrote the first");
    CHECK(refused.GetError().Code == ErrorCode::VaultChangedOnDisk);

    VaultFile check;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), check).IsOk());
    CHECK_EQ(check.Doc().Entries.size(), std::size_t{ 1 });
    CHECK(check.Doc().Entries[0].Title == "From the second instance");
}

TEST_CASE(Vault_RepeatedSavesStayInStep)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    for (int i = 0; i < 5; ++i)
    {
        v.Doc().Entries.push_back(MakeEntry(static_cast<std::uint64_t>(i + 1), "x", "y"));
        CHECK_MSG(v.Save(fs, kPath).IsOk(), "an ordinary save was refused");

        VaultFile::HeaderInfo info;
        CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
        CHECK_EQ(info.Generation, v.Generation());
    }
}

TEST_CASE(Vault_SaveRecreatesAFileThatVanished)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
    CHECK(fs.Remove(kPath).IsOk());
    CHECK(!fs.Exists(kPath));

    CHECK_MSG(v.Save(fs, kPath).IsOk(), "a vanished file could not be recreated");
    CHECK(fs.Exists(kPath));
}

TEST_CASE(Vault_SaveRefusesToOverwriteSomethingItDoesNotUnderstand)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    std::vector<unsigned char> foreign(400, 0x5A);
    fs.Put(kPath, foreign);

    const Status refused = v.Save(fs, kPath);
    CHECK_MSG(!refused.IsOk(), "a foreign file was silently overwritten");

    std::vector<unsigned char> still;
    CHECK(fs.Read(kPath, still).IsOk());
    CHECK_MSG(still == foreign, "the foreign file was changed despite the refusal");
}

TEST_CASE(Vault_SaveRefusesAnOlderFileToo)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    std::vector<unsigned char> early;
    CHECK(fs.Read(kPath, early).IsOk());

    v.Doc().Entries.push_back(MakeEntry(1, "spaeter", "x"));
    CHECK(v.Save(fs, kPath).IsOk());

    fs.Put(kPath, early);

    const Status refused = v.Save(fs, kPath);
    CHECK_MSG(!refused.IsOk(), "a restored older state was overwritten");
    CHECK(refused.GetError().Code == ErrorCode::VaultChangedOnDisk);
}

TEST_CASE(Vault_KekMatchesOnlyTheRightPassword)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    SecureBuffer right;
    CHECK(Crypto::DeriveKeyFromPassword(right, kPassword, std::strlen(kPassword),
                                        v.Salt(), v.Params()).IsOk());
    CHECK_MSG(v.KekMatches(right), "the right password was not recognised");

    const char* wrong = "a fairly long master password ";
    SecureBuffer other;
    CHECK(Crypto::DeriveKeyFromPassword(other, wrong, std::strlen(wrong),
                                        v.Salt(), v.Params()).IsOk());
    CHECK_MSG(!v.KekMatches(other), "a wrong password was accepted");

    SecureBuffer nothing;
    CHECK(!v.KekMatches(nothing));

    v.Lock();
    CHECK_MSG(!v.KekMatches(right), "a locked vault confirmed a key");
}

TEST_CASE(Vault_ChangePasswordWithKekIsTheSameAsDerivingInline)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
    v.Doc().Entries.push_back(MakeEntry(1, "Bank", "bank"));
    CHECK(v.Save(fs, kPath).IsOk());

    const char* newPw = "another, equally long password";
    unsigned char salt[Crypto::kSaltBytes];
    Crypto::RandomBytes(salt, sizeof(salt));
    SecureBuffer kek;
    CHECK(Crypto::DeriveKeyFromPassword(kek, newPw, std::strlen(newPw), salt,
                                        Cheap()).IsOk());
    CHECK(v.ChangePasswordWithKek(fs, kPath, std::move(kek), salt, Cheap()).IsOk());

    VaultFile old;
    CHECK(!VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), old).IsOk());

    VaultFile fresh;
    CHECK(VaultFile::Unlock(fs, kPath, newPw, std::strlen(newPw), fresh).IsOk());
    CHECK_EQ(fresh.Doc().Entries.size(), std::size_t{ 1 });
    CHECK(fresh.Doc().Entries[0].Title == "Bank");
}

TEST_CASE(Vault_AFailedPasswordChangeLeavesTheOldOneWorking)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    VaultFile other;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), other).IsOk());
    other.Doc().Entries.push_back(MakeEntry(1, "Fremd", "x"));
    CHECK(other.Save(fs, kPath).IsOk());

    const char* newPw = "another, equally long password";
    unsigned char salt[Crypto::kSaltBytes];
    Crypto::RandomBytes(salt, sizeof(salt));
    SecureBuffer kek;
    CHECK(Crypto::DeriveKeyFromPassword(kek, newPw, std::strlen(newPw), salt,
                                        Cheap()).IsOk());
    const Status refused = v.ChangePasswordWithKek(fs, kPath, std::move(kek), salt, Cheap());
    CHECK(!refused.IsOk());

    SecureBuffer oldKek;
    CHECK(Crypto::DeriveKeyFromPassword(oldKek, kPassword, std::strlen(kPassword),
                                        v.Salt(), v.Params()).IsOk());
    CHECK_MSG(v.KekMatches(oldKek),
              "after a failed change the vault no longer belonged to any password");

    VaultFile check;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), check).IsOk());
}

TEST_CASE(Vault_HeaderNamesTheThemeForTheGate)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());

    VaultFile::HeaderInfo info;
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK_MSG(info.GateTheme == 0, "a new vault already names a scheme");

    v.Doc().VaultSettings.Theme = "mint";
    CHECK(v.HeaderThemeStale());
    CHECK(v.Save(fs, kPath).IsOk());
    CHECK(!v.HeaderThemeStale());
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK(info.GateTheme == PaletteHeaderCode("mint"));
    CHECK(PaletteIdForHeaderCode(info.GateTheme) == "mint");

    VaultFile v2;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), v2).IsOk());
    CHECK(v2.Doc().VaultSettings.Theme == "mint");
    CHECK(!v2.HeaderThemeStale());

    v2.Doc().VaultSettings.Theme.clear();
    CHECK(v2.HeaderThemeStale());
    CHECK(v2.Save(fs, kPath).IsOk());
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK(info.GateTheme == 0);
}

TEST_CASE(Vault_TheThemeByteIsAuthenticated)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        v.Doc().VaultSettings.Theme = "mint";
        CHECK(v.Save(fs, kPath).IsOk());
    }

    std::vector<unsigned char> raw;
    CHECK(fs.Read(kPath, raw).IsOk());
    const unsigned char forged = PaletteHeaderCode("light");
    CHECK_MSG(raw[HeaderOffset::GateTheme] != forged, "the tampering would have no effect");
    raw[HeaderOffset::GateTheme] = forged;
    unsigned char sum[kChecksumBytes];
    CHECK(Crypto::Hash(sum, sizeof(sum), raw.data(), HeaderLength(1) - kChecksumBytes).IsOk());
    std::memcpy(raw.data() + HeaderLength(1) - kChecksumBytes, sum, sizeof(sum));
    fs.Put(kPath, raw);

    VaultFile::HeaderInfo info;
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK(info.GateTheme == forged);

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::WrongPassword,
              std::string("got ") + ToString(st.Code()));
}

TEST_CASE(Vault_AThemeThisBuildDoesNotKnowKeepsItsNumber)
{
    Ready();
    MemoryFileSystem fs;
    VaultFile v;
    CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
    v.Doc().VaultSettings.Theme = "mint";
    CHECK(v.Save(fs, kPath).IsOk());

    v.Doc().VaultSettings.Theme = "sepia-2031";
    CHECK_MSG(!v.HeaderThemeStale(), "an unknown scheme counted as stale");
    CHECK(v.Save(fs, kPath).IsOk());
    VaultFile::HeaderInfo info;
    CHECK(VaultFile::PeekHeader(fs, kPath, info).IsOk());
    CHECK_MSG(info.GateTheme == PaletteHeaderCode("mint"), "the header number was deleted");
}

TEST_CASE(Vault_ASymbolSurvivesARoundTrip)
{
    Ready();
    MemoryFileSystem fs;
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, kPath, kPassword, std::strlen(kPassword), Cheap(), v).IsOk());
        Entry bank = MakeEntry(1, "Sparkasse", "");
        bank.Symbol = "building-columns";
        v.Doc().Entries.push_back(bank);
        v.Doc().Entries.push_back(MakeEntry(2, "Steam", "steam"));
        CHECK(v.Save(fs, kPath).IsOk());
    }

    VaultFile v2;
    CHECK(VaultFile::Unlock(fs, kPath, kPassword, std::strlen(kPassword), v2).IsOk());
    CHECK_EQ(v2.Doc().Entries.size(), std::size_t{ 2 });
    CHECK(v2.Doc().Entries[0].Symbol == "building-columns");
    CHECK(v2.Doc().Entries[0].PlatformId.empty());
    CHECK(v2.Doc().Entries[1].Symbol.empty());
    CHECK(v2.Doc().Entries[1].PlatformId == "steam");
}
