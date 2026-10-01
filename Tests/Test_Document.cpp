#include "TestRunner.h"

#include "ArenaTestUtil.h"

#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Secret.h"

#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;

    Document FullyPopulated()
    {
        Document doc;

        doc.VaultSettings.LastExportPath = "D:\\irgendwo\\export.csv";
        doc.VaultSettings.Language = "de";
        doc.VaultSettings.Unknown.push_back(
            UnknownRecord{ 900, { 'g', 'e', 'h', 'e', 'i', 'm' } });

        doc.HistoryKey.assign(32, 0x7E);
        doc.Unknown.push_back(UnknownRecord{ 901, { 'n', 'o', 'c', 'h' } });

        Entry e;
        e.Title = "A very long title, so no short buffer applies";
        e.PlatformId = "example-platform-with-a-long-name";
        e.Folder = "A folder with a sufficiently long name";

        Field user;
        user.Key = "Username and a long key";
        user.Value = "alex.doe@example.com and some more text";
        e.Fields.push_back(std::move(user));

        Field pw;
        pw.Key = "Password";
        pw.Value = "EinSehrLangesGeheimesPassword-0123456789";
        pw.Flags = kFieldFlagSecret;
        pw.Unknown.push_back(UnknownRecord{ 902, { 'f', 'e', 'l', 'd' } });
        e.Fields.push_back(std::move(pw));

        e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP";
        e.Totp.Config.Issuer = "A provider with a long name";
        e.Totp.Config.AccountName = "alex.doe@example.com";
        e.Totp.Unknown.push_back(UnknownRecord{ 903, { 't', 'o', 't', 'p' } });
        e.HasTotp = true;

        RecoveryCode code;
        code.Value = "WIEDERHERSTELLUNGSCODE-0123456789";
        code.Unknown.push_back(UnknownRecord{ 904, { 'c', 'o', 'd', 'e' } });
        e.RecoveryCodes.push_back(std::move(code));

        ListItem item;
        item.Kind = kListKindProductKey;
        item.Label = "A game with a sufficiently long name";
        item.Secret = "AAAAA-BBBBB-CCCCC-DDDDD-EEEEE";
        item.AddedUtc = kNow;
        item.Unknown.push_back(UnknownRecord{ 907, { 'l', 'i', 's', 't' } });
        e.ListItems.push_back(std::move(item));

        e.Unknown.push_back(UnknownRecord{ 906, { 'e', 'i', 'n', 't' } });

        const auto id = AddEntry(doc, std::move(e), kNow);
        CHECK(id.IsOk());

        if (!doc.Entries.empty())
        {
            PasswordMemory mem;
            for (std::size_t i = 0; i < sizeof(mem.Mac); ++i)
                mem.Mac[i] = static_cast<unsigned char>(0xA0 + (i & 0x0F));
            mem.RetiredUtc = kNow;
            mem.Unknown.push_back(UnknownRecord{ 905, { 'h', 'i', 's', 't' } });
            doc.Entries.back().PasswordHistory.push_back(std::move(mem));
        }
        return doc;
    }

    template <class E, class Fn>
    void VisitSecrets(E& e, Fn&& fn)
    {
        for (auto& f : e.Fields)
            fn(f.Value, "Field::Value");
        fn(e.Totp.Config.SecretBase32, "TotpConfig::SecretBase32");
        for (auto& c : e.RecoveryCodes)
            fn(c.Value, "RecoveryCode::Value");
        for (auto& item : e.ListItems)
            fn(item.Secret, "ListItem::Secret");
    }

    std::vector<std::string> Leftovers(const Document& doc)
    {
        std::vector<std::string> left;

        auto str = [&left](const std::string& s, const char* what) {
            if (!s.empty())
                left.push_back(what);
        };
        auto sec = [&left](const SecureString& s, const char* what) {
            bool dirty = !s.Empty();
            const unsigned char* p = s.PayloadForTests();
            for (std::size_t i = 0; p && i < s.SlotBytesForTests(); ++i)
                if (p[i] != 0)
                {
                    dirty = true;
                    break;
                }
            if (dirty)
                left.push_back(what);
        };
        auto bytes = [&left](const std::vector<unsigned char>& v, const char* what) {
            if (!v.empty())
                left.push_back(what);
        };
        auto unknown = [&left](const std::vector<UnknownRecord>& u, const char* what) {
            for (const UnknownRecord& r : u)
                if (!r.Payload.empty())
                    left.push_back(what);
        };

        str(doc.VaultSettings.LastExportPath, "Settings::LastExportPath");
        str(doc.VaultSettings.Language, "Settings::Language");
        unknown(doc.VaultSettings.Unknown, "Settings::Unknown");
        bytes(doc.HistoryKey, "Document::HistoryKey");
        unknown(doc.Unknown, "Document::Unknown");

        for (const Entry& e : doc.Entries)
        {
            str(e.Title, "Entry::Title");
            str(e.PlatformId, "Entry::PlatformId");
            str(e.Folder, "Entry::Folder");
            unknown(e.Unknown, "Entry::Unknown");

            VisitSecrets(e, sec);

            for (const Field& f : e.Fields)
            {
                str(f.Key, "Field::Key");
                unknown(f.Unknown, "Field::Unknown");
            }

            str(e.Totp.Config.Issuer, "TotpConfig::Issuer");
            str(e.Totp.Config.AccountName, "TotpConfig::AccountName");
            unknown(e.Totp.Unknown, "StoredTotp::Unknown");

            for (const RecoveryCode& c : e.RecoveryCodes)
                unknown(c.Unknown, "RecoveryCode::Unknown");

            for (const ListItem& item : e.ListItems)
            {
                str(item.Label, "ListItem::Label");
                unknown(item.Unknown, "ListItem::Unknown");
            }

            for (const PasswordMemory& m : e.PasswordHistory)
            {
                for (unsigned char b : m.Mac)
                    if (b != 0)
                    {
                        left.push_back("PasswordMemory::Mac");
                        break;
                    }
                unknown(m.Unknown, "PasswordMemory::Unknown");
            }
        }
        return left;
    }

    std::string Join(const std::vector<std::string>& v)
    {
        std::string out;
        for (const std::string& s : v)
        {
            if (!out.empty())
                out += ", ";
            out += s;
        }
        return out;
    }
}

TEST_CASE(Document_WipeStringCoversTheWholeCapacity)
{
    std::string s;
    s.reserve(256);
    s.assign(200, 'X');
    const std::size_t cap = s.capacity();
    CHECK(cap >= 200);

    s.resize(50);
    const char* const p = s.data();

    WipeString(s);

    CHECK(s.empty());
    std::size_t nonZero = 0;
    for (std::size_t i = 0; i < cap; ++i)
        if (p[i] != '\0')
            ++nonZero;
    CHECK_MSG(nonZero == 0,
              std::to_string(nonZero) + " of " + std::to_string(cap) +
                  " bytes remained");
}

TEST_CASE(Document_WipeStringSurvivesTheEmptyCase)
{
    std::string empty;
    WipeString(empty);
    CHECK(empty.empty());

    std::string shortOne = "ab";
    WipeString(shortOne);
    CHECK(shortOne.empty());
}

TEST_CASE(Document_WipeBytesCoversTheWholeCapacity)
{
    std::vector<unsigned char> v;
    v.reserve(128);
    v.assign(100, 0xC3);
    const std::size_t cap = v.capacity();
    v.resize(10);
    const unsigned char* const p = v.data();

    WipeBytes(v);

    CHECK(v.empty());
    std::size_t nonZero = 0;
    for (std::size_t i = 0; i < cap; ++i)
        if (p[i] != 0)
            ++nonZero;
    CHECK_MSG(nonZero == 0,
              std::to_string(nonZero) + " of " + std::to_string(cap) +
                  " bytes remained");

    std::vector<unsigned char> none;
    WipeBytes(none);
    CHECK(none.empty());
}

TEST_CASE(Document_TheCheckWorksAtAll)
{
    const Document doc = FullyPopulated();
    const std::vector<std::string> left = Leftovers(doc);

    CHECK_MSG(!left.empty(),
              "the checker finds nothing in a FULL document -- so "
              "it measures nothing after wiping either");

    CHECK_MSG(left.size() >= 25,
              "only " + std::to_string(left.size()) +
                  " hits in the full document: " + Join(left));
}

TEST_CASE(Document_WipeFieldClearsKeyValueAndUnknown)
{
    Field f;
    f.Key = "Ein ausreichend langer Feldname";
    f.Value = "EinSehrLangesGeheimesPassword-0123456789";
    f.Unknown.push_back(UnknownRecord{ 900, { 'g', 'e', 'h', 'e', 'i', 'm' } });

    const char* const keyBuf = f.Key.data();
    const std::size_t keyCap = f.Key.capacity();
    const unsigned char* const valSlot = f.Value.PayloadForTests();
    const std::size_t valSlotBytes = f.Value.SlotBytesForTests();

    CHECK(!f.Key.empty());
    CHECK(!f.Value.Empty());
    CHECK_MSG(f.Value.InArena() && valSlotBytes > f.Value.Size(),
              "the value is not in the arena -- the case measures nothing");
    CHECK(!f.Unknown.empty() && !f.Unknown[0].Payload.empty());

    WipeField(f);

    CHECK(f.Key.empty());
    CHECK(f.Value.Empty());
    CHECK_MSG(f.Value.PayloadForTests() == valSlot,
              "WipeField freed the slot instead of wiping it");
    CHECK_MSG(f.Unknown.size() == 1,
              "WipeField removed the unknown record instead of wiping it");
    if (!f.Unknown.empty())
        CHECK_MSG(f.Unknown[0].Payload.empty(), "the payload remained");

    std::size_t left = 0;
    for (std::size_t i = 0; i < keyCap; ++i)
        if (keyBuf[i] != 0) ++left;
    for (std::size_t i = 0; valSlot && i < valSlotBytes; ++i)
        if (valSlot[i] != 0) ++left;
    CHECK_MSG(left == 0,
              std::to_string(left) + " bytes remained in the buffers");
}

TEST_CASE(Document_WipeReachesEveryField)
{
    Document doc = FullyPopulated();
    WipeDocument(doc);

    const std::vector<std::string> left = Leftovers(doc);
    CHECK_MSG(left.empty(), "not wiped: " + Join(left));

    CHECK_EQ(doc.Entries.size(), std::size_t{ 1 });
    if (!doc.Entries.empty())
        CHECK_EQ(doc.Entries[0].Fields.size(), std::size_t{ 2 });
}

TEST_CASE(Document_CopyEntryNoticesALossOfEverySecret)
{
    std::size_t total = 0;
    {
        Document doc = FullyPopulated();
        CHECK(!doc.Entries.empty());
        if (doc.Entries.empty())
            return;
        doc.Entries.back().RecoveryCodes.push_back(RecoveryCode{});
        doc.Entries.back().RecoveryCodes.back().Value = "ZWEITER-CODE-0123456789";
        VisitSecrets(doc.Entries.back(), [&total](SecureString&, const char*) { ++total; });
    }
    CHECK_MSG(total >= 6, "too few values in the enumeration: " + std::to_string(total));

    for (std::size_t lose = 0; lose < total; ++lose)
    {
        Document doc = FullyPopulated();
        if (doc.Entries.empty())
            return;
        Entry& e = doc.Entries.back();
        e.RecoveryCodes.push_back(RecoveryCode{});
        e.RecoveryCodes.back().Value = "ZWEITER-CODE-0123456789";

        std::string which;
        std::size_t at = 0;
        VisitSecrets(e, [&](SecureString& s, const char* what) {
            if (at++ == lose)
            {
                s = std::string(9000, 'v');
                which = what;
            }
        });

        Entry out;
        out.Title = "unberuehrt";
        {
            const NoHeapFallbackForTest noHeap;
            CHECK_MSG(!CopyEntry(e, out),
                      "loss of " + which + " (number " + std::to_string(lose) +
                          ") went unnoticed");
        }
        CHECK_MSG(out.Title == "unberuehrt", "the target was overwritten despite a loss");

        CHECK(CopyEntry(e, out));
        CHECK(out.Title == e.Title);
    }
}
