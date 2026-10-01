#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "HeapWatch.h"

#include "Core/CsvExport.h"
#include "Core/CsvImport.h"
#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/LinkSafety.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Random.h"
#include "Core/Redact.h"
#include "Core/Search.h"
#include "Core/VaultAudit.h"
#include "Core/VaultFile.h"

#include <sodium.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;
using Test::HeapCopiesDuring;

static_assert(!std::is_convertible_v<SecureString, std::string>,
              "SecureString must not silently turn into a std::string");
static_assert(!std::is_convertible_v<SecureString, std::string_view>,
              "SecureString must not silently turn into a view");
static_assert(!std::is_constructible_v<std::string, const SecureString&>,
              "a std::string must not be constructible from a SecureString");

namespace
{
    constexpr std::string_view kPassword = "Pw-with-twenty-chars";

    void FillUntilLoss(std::vector<SecureString>& filler, std::size_t length)
    {
        SecureArena& arena = SecureArena::Instance();
        const std::size_t before = arena.LostCount();
        const std::string value(length, 'f');
        while (arena.LostCount() == before)
        {
            SecureString s;
            s = value;
            filler.push_back(std::move(s));
        }
    }

    Field MakeField(FieldRole role, const char* key, std::string_view value, bool secret)
    {
        Field f;
        f.Role = role;
        f.Key = key;
        f.Value = value;
        if (secret)
            f.Flags |= kFieldFlagSecret;
        return f;
    }

    Entry PasswordEntry(const char* title, std::string_view password)
    {
        Entry e;
        e.Title = title;
        e.Fields.push_back(MakeField(FieldRole::Password,
                                     CanonicalFieldName(FieldRole::Password), password, true));
        return e;
    }

    const Field* PasswordOf(const Entry& e)
    {
        for (const Field& f : e.Fields)
            if (f.Role == FieldRole::Password)
                return &f;
        return nullptr;
    }
}

TEST_CASE(FieldValue_FoldedCompareMatchesFoldForSearch)
{
    const char* corpus[] = {
        "", "a", "b", "e", "ae", "abc", "ABC", "M\xC3\xBCller", "MUELLER", "mueller",
        "Stra\xC3\x9F" "e", "STRASSE", "\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F",
        "B\xC3\xA4r", "abc\xC3", "\xC3", "\xC3\x97\xC3\xB7", "x\xC3\x97y",
        "\xC3\x86r\xC3\xB8", "Cr\xC3\xA8me", "\xC3\xBF", "\xC3\xBEorn", "\xE6\x97\xA5",
    };
    std::size_t wrong = 0;
    for (const char* a : corpus)
        for (const char* b : corpus)
        {
            const std::string fa = FoldForSearch(a);
            const std::string fb = FoldForSearch(b);
            if (FoldedEquals(a, b) != (fa == fb))
                ++wrong;
            if (FoldedContains(a, fb) != (fa.find(fb) != std::string::npos))
                ++wrong;
        }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " mismatches in the corpus");

    const unsigned char alphabet[] = { 'a', 'A', 'e', 'E', 's', 'S', 0xC3, 0xA4,
                                       0x84, 0x9F, 0xBC, 0x80, 0xBF, 0x97, 0xFF };
    DeterministicRandom r(4711);
    auto draw = [&](std::size_t maxLength) {
        unsigned char n = 0;
        r.Fill(&n, 1);
        std::string s;
        for (std::size_t i = 0; i < n % (maxLength + 1); ++i)
        {
            unsigned char pick = 0;
            r.Fill(&pick, 1);
            s.push_back(static_cast<char>(alphabet[pick % sizeof(alphabet)]));
        }
        return s;
    };
    for (int i = 0; i < 4000; ++i)
    {
        const std::string text = draw(12);
        const std::string other = draw(4);
        const std::string ft = FoldForSearch(text);
        const std::string fo = FoldForSearch(other);
        if (FoldedEquals(text, other) != (ft == fo))
            ++wrong;
        if (FoldedContains(text, fo) != (ft.find(fo) != std::string::npos))
            ++wrong;
    }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " mismatches on random inputs");
}

TEST_CASE(FieldValue_SearchingCopiesNoValueIntoTheHeap)
{
    Document doc;
    Entry e;
    e.Title = "Account";
    e.Fields.push_back(MakeField(FieldRole::Notes, "Notiz",
                                 "canary-notiz-0123456789-abcdef", false));
    doc.Entries.push_back(std::move(e));

    bool none = false;
    const std::size_t hits = HeapCopiesDuring({ "canary-notiz-0123456789-abcdef" }, [&] {
        none = Search(doc, "matches-nothing").empty();
    });
    CHECK(none);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the field value");
}

TEST_CASE(FieldValue_RedactingCopiesNoHiddenPartIntoTheHeap)
{
    const std::vector<std::string_view> values = {
        "canary-a.hidden.0123456789@example.de",
        "canary-b.other.9876543210@example.de",
    };
    bool shaped = false;
    const std::size_t hits = HeapCopiesDuring({ "hidden.0123456789", "other.9876543210" }, [&] {
        std::vector<std::string> shown = RedactAccountHints(values);
        shaped = shown.size() == 2 && shown[0] != shown[1];
        for (std::string& s : shown)
            WipeString(s);
    });
    CHECK(shaped);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the hidden part");
}

TEST_CASE(FieldValue_RedactingWipesTheRoundsItThrowsAway)
{
    const std::vector<std::string_view> values = {
        "canary-a.hidden.0123456789@canary-domain.example",
        "canary-b.other.9876543210@canary-domain.example",
    };
    bool shaped = false;
    const std::size_t hits = HeapCopiesDuring({ "@canary-domain.example" }, [&] {
        std::vector<std::string> shown = RedactAccountHints(values);
        shaped = shown.size() == 2 && shown[0] != shown[1];
        for (std::string& s : shown)
            WipeString(s);
    });
    CHECK(shaped);
    CHECK_MSG(hits == 0, std::to_string(hits) + " discarded rounds lay unwiped on the heap");
}

TEST_CASE(FieldValue_MarkingDuplicatesCopiesNoLogin)
{
    constexpr std::string_view kLogin = "canary-login-0123456789";
    Document doc;
    Entry stored = PasswordEntry("Account", kPassword);
    stored.Fields.push_back(MakeField(FieldRole::Username,
                                      CanonicalFieldName(FieldRole::Username), kLogin, false));
    CHECK(AddEntry(doc, std::move(stored), 100).IsOk());

    const std::string file = "title,username,password\r\nAccount,canary-login-0123456789,"
                             "Pw-with-twenty-chars\r\n";
    ImportReport report;
    CHECK(ParseCsv(file.c_str(), file.size(), report).IsOk());
    CHECK_EQ(report.Ready, std::size_t{ 1 });

    std::size_t marked = 0;
    const std::size_t hits = HeapCopiesDuring({ kLogin }, [&] {
        marked = MarkDuplicates(doc, report);
    });
    CHECK_EQ(marked, std::size_t{ 1 });
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the login name");
}

TEST_CASE(FieldValue_JudgingALinkCopiesNothing)
{
    const char value[] = "  https://canary.example/pfad-0123456789  ";
    bool openable = false;
    const std::size_t hits = HeapCopiesDuring({ "canary.example/pfad-0123456789" }, [&] {
        openable = IsOpenableWebLink(value) &&
                   LinkToOpen(value) == "https://canary.example/pfad-0123456789";
    });
    CHECK(openable);
    CHECK_EQ(hits, std::size_t{ 0 });
}

TEST_CASE(FieldValue_ALinkWithANulIsRefusedAsAView)
{
    std::string poisoned = "https://beispiel";
    poisoned.push_back('\0');
    poisoned += ".de/weiter";
    SecureString value;
    value = poisoned;
    CHECK_EQ(value.Size(), poisoned.size());
    CHECK_MSG(!IsOpenableWebLink(value.View()), "a value with a NUL byte became a link");
    CHECK(IsOpenableWebLink(value.CStr()));
}

TEST_CASE(FieldValue_AuditingCopiesNoPasswordIntoTheHeap)
{
    const std::string_view shared = "Audit-Canary-0123456789";
    Document doc;
    doc.Entries.push_back(PasswordEntry("Eins", shared));
    doc.Entries.push_back(PasswordEntry("Zwei", shared));
    doc.Entries[0].Id = 1;
    doc.Entries[1].Id = 2;

    bool reused = false;
    const std::size_t hits = HeapCopiesDuring({ shared }, [&] {
        const AuditReport report = Audit(doc, 0);
        reused = report.ReuseGroups == 1;
    });
    CHECK(reused);
    CHECK_EQ(hits, std::size_t{ 0 });
}

TEST_CASE(FieldValue_TheAuditCountsValuesOutsideTheArena)
{
    Document doc;
    CHECK(AddEntry(doc, PasswordEntry("Geschuetzt", kPassword), 100).IsOk());
    CHECK_EQ(Audit(doc, 0).UnprotectedValues, std::size_t{ 0 });

    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const std::size_t before = arena.OverflowCount();
        const std::string value(kPassword.size(), 'f');
        while (arena.OverflowCount() == before)
        {
            SecureString s;
            s = value;
            filler.push_back(std::move(s));
        }
        CHECK(AddEntry(doc, PasswordEntry("Ausgewichen", "Pw-im-Heap-zwanzig-Z"), 100).IsOk());
        Entry listed;
        listed.Title = "With list";
        ListItem item;
        item.Kind = kListKindProductKey;
        item.Label = "Spiel";
        item.Secret = "KEY-im-Heap-zwanzigZ";
        listed.ListItems.push_back(std::move(item));
        CHECK(AddEntry(doc, std::move(listed), 100).IsOk());
    }
    const AuditReport report = Audit(doc, 0);
    CHECK_EQ(report.UnprotectedValues, std::size_t{ 2 });
    const Entry* moved = doc.Entries.size() == 3 ? &doc.Entries[1] : nullptr;
    CHECK(moved && PasswordOf(*moved) && PasswordOf(*moved)->Value == "Pw-im-Heap-zwanzig-Z");
}

TEST_CASE(Audit_TheReusePrintIsSaltThenValue)
{
    unsigned char salt[kAuditSaltBytes];
    for (std::size_t i = 0; i < sizeof(salt); ++i)
        salt[i] = static_cast<unsigned char>(0x11 * (i + 1));
    SecureString value;
    value = "Abdruck-Canary-0123456789";

    unsigned char print[kReusePrintBytes] = {};
    CHECK(ReusePrint(salt, value, print));

    std::vector<unsigned char> joined(salt, salt + sizeof(salt));
    joined.insert(joined.end(), value.Bytes(), value.Bytes() + value.Size());
    unsigned char expected[kReusePrintBytes] = {};
    CHECK(crypto_generichash(expected, sizeof(expected), joined.data(), joined.size(),
                             nullptr, 0) == 0);
    CHECK_MSG(std::memcmp(print, expected, sizeof(print)) == 0,
              "the fingerprint is not BLAKE2b(salt || value)");
}

TEST_CASE(FieldValue_AuditingLeavesNoReusePrintBehind)
{
    unsigned char salt[kAuditSaltBytes];
    for (std::size_t i = 0; i < sizeof(salt); ++i)
        salt[i] = static_cast<unsigned char>(0xA5 ^ i);
    const std::string_view shared = "Orakel-Canary-0123456789";
    Document doc;
    doc.Entries.push_back(PasswordEntry("Eins", shared));
    doc.Entries.push_back(PasswordEntry("Zwei", shared));
    doc.Entries[0].Id = 1;
    doc.Entries[1].Id = 2;

    unsigned char print[kReusePrintBytes] = {};
    CHECK(ReusePrint(salt, doc.Entries[0].Fields[0].Value, print));
    const std::string_view needle(reinterpret_cast<const char*>(print), sizeof(print));

    bool reused = false;
    const std::size_t hits = HeapCopiesDuring({ needle }, [&] {
        const AuditReport report = AuditWithSalt(doc, 0, salt);
        reused = report.ReuseGroups == 1;
    });
    CHECK(reused);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held a fingerprint");
}

TEST_CASE(FieldValue_SavingAndOpeningCopiesNoValue)
{
    Document doc;
    Entry e;
    e.Title = "Account";
    e.Fields.push_back(MakeField(FieldRole::Username, "User",
                                 "canary-user-0123456789", false));
    e.Fields.push_back(MakeField(FieldRole::Password, "Pass",
                                 "Canary-Password-0123456789!", true));
    e.Fields.push_back(MakeField(FieldRole::Notes, "Notiz",
                                 "canary-notiz-0123456789-abcdef", false));
    CHECK(AddEntry(doc, std::move(e), 100).IsOk());

    bool ok = false;
    const std::size_t hits = HeapCopiesDuring(
        { "canary-user-0123456789", "Canary-Password-0123456789!",
          "canary-notiz-0123456789-abcdef" },
        [&] {
            SecureBuffer body;
            std::size_t len = 0;
            if (!EncodeToSecure(doc, body, len, 0, 1u << 20).IsOk())
                return;
            Document back;
            ok = Decode(body.Data(), len, back) && back.Entries.size() == 1 &&
                 back.Entries[0].Fields.size() == 3;
        });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held a field value");
}

TEST_CASE(FieldValue_ExportingCopiesNoValue)
{
    Document doc;
    Entry e;
    e.Title = "Account";
    e.Fields.push_back(MakeField(FieldRole::Username, "User",
                                 "canary-user-0123456789", false));
    e.Fields.push_back(MakeField(FieldRole::Password, "Pass",
                                 "Canary-Password-0123456789!", true));
    e.Fields.push_back(MakeField(FieldRole::Password, "Pass2",
                                 "Canary-Zweitpasswort-01234", true));
    e.Fields.push_back(MakeField(FieldRole::Custom, "CustomerNo",
                                 "canary-kundennummer-01234", false));
    ListItem listed;
    listed.Kind = kListKindProductKey;
    listed.Label = "Spiel";
    listed.Secret = "canary-produktschluessel-9";
    e.ListItems.push_back(std::move(listed));
    CHECK(AddEntry(doc, std::move(e), 100).IsOk());

    CsvOptions options;
    bool ok = false;
    const std::size_t hits = HeapCopiesDuring(
        { "canary-user-0123456789", "Canary-Password-0123456789!",
          "Canary-Zweitpasswort-01234", "canary-kundennummer-01234",
          "canary-produktschluessel-9" },
        [&] {
            SecureBuffer out;
            std::size_t length = 0;
            ok = WriteCsv(doc, options, out, length).IsOk() && length > 0;
        });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held a field value");
}

TEST_CASE(FieldValue_ExportingWithAFullArenaLosesNoCell)
{
    Document doc;
    CHECK(AddEntry(doc, PasswordEntry("Account", kPassword), 100).IsOk());

    bool ok = false;
    bool found = false;
    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, kPassword.size());

        CsvOptions options;
        SecureBuffer out;
        std::size_t length = 0;
        ok = WriteCsv(doc, options, out, length).IsOk();
        if (ok && out.IsValid())
            found = std::string_view(reinterpret_cast<const char*>(out.Data()), length)
                        .find(kPassword) != std::string_view::npos;
    }
    CHECK(ok);
    CHECK_MSG(found, "with a full arena the password is missing from the output");
}

TEST_CASE(FieldValue_UpdatingWipesTheReplacedFields)
{
    Document doc;
    Entry e;
    e.Title = "Account";
    e.Fields.push_back(MakeField(FieldRole::Custom, "Canary-Feldname-0123456789",
                                 "canary-alter-wert-0123456789", false));
    const auto id = AddEntry(doc, std::move(e), 100);
    CHECK(id.IsOk());

    Entry replacement;
    replacement.Title = "Account";
    replacement.Fields.push_back(MakeField(FieldRole::Custom, "Other field",
                                           "a completely different value", false));

    bool ok = false;
    const std::size_t hits = HeapCopiesDuring(
        { "Canary-Feldname-0123456789", "canary-alter-wert-0123456789" }, [&] {
            ok = UpdateEntry(doc, id.Value(), std::move(replacement), 200).IsOk();
        });
    CHECK(ok);
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the old field");
}

TEST_CASE(FieldValue_CopyEntryNoticesEveryLostValue)
{
    for (int kind = 0; kind < 3; ++kind)
    {
        Entry e;
        e.Title = "Quelle";
        if (kind == 0)
            e.Fields.push_back(MakeField(FieldRole::Password, "Pass",
                                         std::string(9000, 'v'), true));
        else if (kind == 1)
        {
            e.RecoveryCodes.push_back(RecoveryCode{});
            e.RecoveryCodes.back().Value = std::string(9000, 'c');
        }
        else
        {
            e.HasTotp = true;
            e.Totp.Config.SecretBase32 = std::string(9000, 'A');
        }

        Entry out;
        out.Title = "unberuehrt";
        {
            const NoHeapFallbackForTest noHeap;
            CHECK_MSG(!CopyEntry(e, out),
                      "a lost value of kind " + std::to_string(kind) +
                          " went unnoticed");
        }
        CHECK_MSG(out.Title == "unberuehrt", "the target was overwritten despite a loss");

        CHECK(CopyEntry(e, out));
        CHECK(out.Title == "Quelle");
    }
}

TEST_CASE(FieldValue_CopyEntryHandsOverWithoutASecondCopy)
{
    const Entry source = PasswordEntry("Bank", kPassword);
    Entry out;
    out.Title = "unberuehrt";
    bool copied = false;
    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, kPassword.size());
        CHECK(filler.size() >= 2);
        if (filler.size() >= 2)
        {
            filler.pop_back();
            filler.pop_back();
        }
        copied = CopyEntry(source, out);
    }
    CHECK(copied);
    const Field* pw = PasswordOf(out);
    CHECK_MSG(pw && pw->Value == kPassword,
              "CopyEntry reported a complete copy, but the target lacks the password");
}

TEST_CASE(FieldValue_AnLvalueDraftThatLosesAValueIsRefused)
{
    Document doc;
    Entry draft = PasswordEntry("Bank", kPassword);
    Entry forUpdate = PasswordEntry("Bank", "Ein-ganz-neues-Passw");
    Entry movedUpdate = PasswordEntry("Bank", "Yet-another-new-pass");

    std::vector<SecureString> filler;
    std::uint64_t id = 0;
    {
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, kPassword.size());

        const auto copied = AddEntry(doc, draft, 100);
        CHECK_MSG(!copied.IsOk(), "a draft with a lost password was accepted");
        CHECK(copied.Code() == ErrorCode::OutOfMemory);
        CHECK(doc.Entries.empty());

        const auto moved = AddEntry(doc, std::move(draft), 100);
        CHECK_MSG(moved.IsOk(), "something was lost despite moving");
        if (moved.IsOk())
            id = moved.Value();

        if (id != 0)
        {
            const Status copiedUpdate = UpdateEntry(doc, id, forUpdate, 200);
            CHECK_MSG(!copiedUpdate.IsOk(),
                      "a replacement with a lost password was accepted");
            const Entry* stored = FindEntry(doc, id);
            CHECK(stored && PasswordOf(*stored) && PasswordOf(*stored)->Value == kPassword);

            CHECK(UpdateEntry(doc, id, std::move(movedUpdate), 300).IsOk());
        }
    }

    const Entry* stored = FindEntry(doc, id);
    CHECK(stored != nullptr);
    if (stored)
    {
        const Field* pw = PasswordOf(*stored);
        CHECK(pw && pw->Value == "Yet-another-new-pass");
    }
}

TEST_CASE(FieldValue_ACsvRowWhoseValueIsLostIsNotReady)
{
    struct Case
    {
        std::string File;
        std::size_t Length;
    };
    const Case cases[] = {
        { "title,password\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,username\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,email\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,url\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,notes\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,extra_fields\r\nAccount,password=Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,CustomerNo\r\nAccount,Pw-with-twenty-chars\r\n", kPassword.size() },
        { "title,recovery_codes\r\nAccount,Code-with-twenty-chr\r\n", 20 },
        { "title,totp\r\nAccount,JBSWY3DPEHPK3PXP\r\n", 16 },
        { "title,list_items\r\nAccount,\"product_key\tGame\tKEY-with-twenty-char\"\r\n", 20 },
    };
    for (const Case& c : cases)
    {
        const std::string& file = c.File;
        ImportReport lost;
        {
            std::vector<SecureString> filler;
            SecureArena& arena = SecureArena::Instance();
            const ChunkLimitForTest noNewChunks(arena.ChunkCount());
            const NoHeapFallbackForTest noHeap;
            FillUntilLoss(filler, c.Length);
            CHECK(ParseCsv(file.c_str(), file.size(), lost).IsOk());
        }
        CHECK_MSG(lost.Ready == 0, "a row with a lost value was taken over: " + file);
        if (lost.Rows.size() == 1)
        {
            CHECK_MSG(lost.Rows[0].Detail == "value could not be stored",
                      "wrong reason: " + lost.Rows[0].Detail + " for " + file);
            CHECK(!lost.Rows[0].SecondFactorDropped);
        }

        ImportReport fine;
        CHECK(ParseCsv(file.c_str(), file.size(), fine).IsOk());
        CHECK_EQ(fine.Ready, std::size_t{ 1 });
    }
}

TEST_CASE(FieldValue_CommittingAnImportLosesNothing)
{
    const std::string file = "title,password\r\nAccount,Pw-with-twenty-chars\r\n";
    ImportReport report;
    CHECK(ParseCsv(file.c_str(), file.size(), report).IsOk());
    CHECK_EQ(report.Ready, std::size_t{ 1 });

    Document doc;
    ImportCommit commit;
    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, kPassword.size());
        commit = CommitImport(doc, report, 100);
    }
    CHECK_EQ(commit.Added, std::size_t{ 1 });
    CHECK_EQ(commit.Refused, std::size_t{ 0 });
    if (doc.Entries.size() == 1)
    {
        const Field* pw = PasswordOf(doc.Entries[0]);
        CHECK_MSG(pw && pw->Value == kPassword, "the password did not arrive");
    }
}

TEST_CASE(FieldValue_ALossWhileOpeningIsNotTampering)
{
    CHECK(Crypto::Initialise().IsOk());
    MemoryFileSystem fs;
    const char path[] = "verlust.vault";
    const char master[] = "a fairly long master password";
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, master, std::strlen(master),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        CHECK(AddEntry(v.Doc(), PasswordEntry("Bank", kPassword), 100).IsOk());
        CHECK(v.Save(fs, path).IsOk());
    }

    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, kPassword.size());

        VaultFile out;
        const Status st = VaultFile::Unlock(fs, path, master, std::strlen(master), out);
        CHECK(!st.IsOk());
        CHECK_MSG(st.Code() == ErrorCode::OutOfMemory,
                  "a loss while opening was reported as damage");
    }

    VaultFile out;
    CHECK(VaultFile::Unlock(fs, path, master, std::strlen(master), out).IsOk());
}

TEST_CASE(FieldValue_AnUnreadableBodyWithoutALossIsStillCorrupt)
{
    CHECK(Crypto::Initialise().IsOk());
    MemoryFileSystem fs;
    const char path[] = "unlesbar.vault";
    const char master[] = "a fairly long master password";
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, master, std::strlen(master),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        CHECK(AddEntry(v.Doc(), PasswordEntry("Bank", kPassword), 100).IsOk());
        v.Doc().HistoryKey.assign(3, 0x42);
        CHECK(v.Save(fs, path).IsOk());
    }

    VaultFile out;
    const Status st = VaultFile::Unlock(fs, path, master, std::strlen(master), out);
    CHECK(!st.IsOk());
    CHECK_MSG(st.Code() == ErrorCode::FileTamperedOrCorrupt,
              "an unreadable file without loss was not reported as unreadable");
    CHECK(st.GetError().Detail && std::string(st.GetError().Detail) == "content not readable");
}
