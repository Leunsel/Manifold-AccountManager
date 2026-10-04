#include "TestRunner.h"

#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/PasswordStrength.h"
#include "Core/VaultAudit.h"

#include <cstring>
#include <string>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    const char* const kStrong = "Xk7#mQ2!vR9$wL4&zP6@";
    const char* const kStrongToo = "Bt3%nJ8^cF5*hD1?uY0+";
    const char* const kWeak = "abc";

    Field Password(const char* value)
    {
        Field f;
        f.Key = "Password";
        f.Value = value;
        f.Role = FieldRole::Password;
        f.Flags |= kFieldFlagSecret;
        return f;
    }

    std::uint64_t Add(Document& doc, const char* title, const char* password,
                      bool withTotp = false)
    {
        Entry e;
        e.Title = title;
        if (password)
            e.Fields.push_back(Password(password));
        if (withTotp)
        {
            e.HasTotp = true;
            e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
        }
        const auto id = AddEntry(doc, e, 1000);
        CHECK(id.IsOk());
        return id.IsOk() ? id.Value() : 0;
    }

    const AuditFinding* Find(const AuditReport& r, std::uint64_t id)
    {
        for (const AuditFinding& f : r.Findings)
            if (f.EntryId == id)
                return &f;
        return nullptr;
    }
}

TEST_CASE(Audit_ACleanVaultReportsNothing)
{
    Document doc;
    Add(doc, "Erster", kStrong, true);
    Add(doc, "Zweiter", kStrongToo, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.Entries, std::size_t{ 2 });
    CHECK_EQ(r.WithPassword, std::size_t{ 2 });
    CHECK_MSG(r.Clean(), "a clean vault produced a finding");
    CHECK_EQ(r.WeakPasswords, std::size_t{ 0 });
    CHECK_EQ(r.ReuseGroups, std::size_t{ 0 });
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 0 });
}

TEST_CASE(Audit_FindsAWebsiteWithoutHttps)
{
    Document doc;
    auto withSite = [&](const char* title, const char* site) {
        Entry e;
        e.Title = title;
        e.HasTotp = true;
        e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
        Field f;
        f.Key = "Website";
        f.Value = site;
        f.Role = FieldRole::Website;
        e.Fields.push_back(f);
        const auto id = AddEntry(doc, e, 1000);
        CHECK(id.IsOk());
        return id.IsOk() ? id.Value() : 0;
    };
    const std::uint64_t shop = withSite("Shop", "http://shop.example.de/login");
    const std::uint64_t bank = withSite("Bank", "https://bank.example.de");
    const std::uint64_t router = withSite("Router", "http://192.168.178.1");

    Entry notes;
    notes.Title = "Notiz";
    notes.HasTotp = true;
    notes.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    Field custom;
    custom.Key = "Link";
    custom.Value = "http://example.de";
    notes.Fields.push_back(custom);
    CHECK(AddEntry(doc, notes, 1000).IsOk());

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.InsecureLinks, std::size_t{ 1 });
    const AuditFinding* f = Find(r, shop);
    CHECK_MSG(f && f->InsecureLink, "the http:// login was not reported");
    CHECK_MSG(!Find(r, bank), "an https:// address was reported");
    CHECK_MSG(!Find(r, router), "the router at home was reported");
    CHECK_EQ(r.Findings.size(), std::size_t{ 1 });
}

TEST_CASE(Audit_FindsAReusedPassword)
{
    Document doc;
    const std::uint64_t a = Add(doc, "Erster", kStrong, true);
    const std::uint64_t b = Add(doc, "Zweiter", kStrong, true);
    const std::uint64_t c = Add(doc, "Dritter", kStrongToo, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.ReuseGroups, std::size_t{ 1 });
    CHECK_EQ(r.ReusedEntries, std::size_t{ 2 });

    CHECK(Find(r, a) && Find(r, a)->ReusedPassword);
    CHECK(Find(r, b) && Find(r, b)->ReusedPassword);
    CHECK_MSG(Find(r, c) == nullptr,
              "a password used only once was reported");
}

TEST_CASE(Audit_TwoGroupsAreCountedSeparately)
{
    Document doc;
    Add(doc, "A1", kStrong, true);
    Add(doc, "A2", kStrong, true);
    Add(doc, "B1", kStrongToo, true);
    Add(doc, "B2", kStrongToo, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.ReuseGroups, std::size_t{ 2 });
    CHECK_EQ(r.ReusedEntries, std::size_t{ 4 });
    CHECK_EQ(r.Findings.size(), std::size_t{ 4 });
}

TEST_CASE(Audit_ThreeTimesTheSamePasswordIsOneGroup)
{
    Document doc;
    Add(doc, "A", kStrong, true);
    Add(doc, "B", kStrong, true);
    Add(doc, "C", kStrong, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.ReuseGroups, std::size_t{ 1 });
    CHECK_MSG(r.ReusedEntries == 3, "the third use was not counted");
}

TEST_CASE(Audit_OnlyThePasswordRoleCounts)
{
    Document doc;
    Add(doc, "Echt", kStrong, true);

    Entry other;
    other.Title = "Notiz";
    Field custom;
    custom.Key = "Password";
    custom.Value = kStrong;
    other.Fields.push_back(custom);
    other.HasTotp = true;
    other.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    CHECK(AddEntry(doc, other, 1000).IsOk());

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithPassword, std::size_t{ 1 });
    CHECK_MSG(r.ReuseGroups == 0, "a custom field was taken for a password");
}

TEST_CASE(Audit_AnEmptyPasswordIsNoPassword)
{
    Document doc;
    Entry e;
    e.Title = "Leer";
    e.Fields.push_back(Password(""));
    e.HasTotp = true;
    e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    CHECK(AddEntry(doc, e, 1000).IsOk());

    const AuditReport r = Audit(doc, 0);
    CHECK_MSG(r.WithPassword == 0, "an empty field was counted as a password");
    CHECK_EQ(r.WeakPasswords, std::size_t{ 0 });
    CHECK(r.Clean());
}

TEST_CASE(Audit_ShortPasswordsAreWeak)
{
    Document doc;
    const std::uint64_t weak = Add(doc, "Kurz", kWeak, true);
    const std::uint64_t fine = Add(doc, "Lang", kStrong, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WeakPasswords, std::size_t{ 1 });
    CHECK(Find(r, weak) && Find(r, weak)->WeakPassword);
    CHECK_MSG(Find(r, fine) == nullptr, "a strong password was reported");
}

TEST_CASE(Audit_EntriesWithoutASecondFactorAreCounted)
{
    Document doc;
    const std::uint64_t bare = Add(doc, "Ohne", kStrong, false);
    Add(doc, "Mit", kStrongToo, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 1 });
    CHECK(Find(r, bare) && Find(r, bare)->NoSecondFactor);
}

TEST_CASE(Audit_AnEntryWithoutAPasswordIsStillCounted)
{
    Document doc;
    Add(doc, "Notiz", nullptr, true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.Entries, std::size_t{ 1 });
    CHECK_EQ(r.WithPassword, std::size_t{ 0 });
    CHECK(r.Clean());
}

TEST_CASE(Audit_TheSaltDoesNotChangeTheAnswer)
{
    Document doc;
    Add(doc, "A", kStrong, true);
    Add(doc, "B", kStrong, true);
    Add(doc, "C", kStrongToo, false);
    Add(doc, "D", kWeak, true);

    const AuditReport first = Audit(doc, 0);
    for (int run = 0; run < 8; ++run)
    {
        const AuditReport again = Audit(doc, 0);
        CHECK_EQ(again.ReuseGroups, first.ReuseGroups);
        CHECK_EQ(again.ReusedEntries, first.ReusedEntries);
        CHECK_EQ(again.WeakPasswords, first.WeakPasswords);
        CHECK_EQ(again.WithoutSecondFactor, first.WithoutSecondFactor);
        CHECK_EQ(again.Findings.size(), first.Findings.size());
        for (std::size_t i = 0; i < again.Findings.size(); ++i)
        {
            CHECK(again.Findings[i].EntryId == first.Findings[i].EntryId);
            CHECK(again.Findings[i].ReusedPassword == first.Findings[i].ReusedPassword);
            CHECK(again.Findings[i].WeakPassword == first.Findings[i].WeakPassword);
        }
    }
}

TEST_CASE(Audit_AnEmptyVaultIsClean)
{
    Document doc;
    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.Entries, std::size_t{ 0 });
    CHECK(r.Clean());
}

namespace
{
    bool Common(const char* p)
    {
        return IsCommonPassword(p, std::strlen(p));
    }
}

TEST_CASE(Audit_TheObviousOnesAreOnTheList)
{
    CHECK(Common("123456"));
    CHECK(Common("password"));
    CHECK(Common("qwertz"));
    CHECK(Common("letmein"));
    CHECK(Common("passwort"));
}

TEST_CASE(Audit_TheListCatchesWhatEntropyMisses)
{
    const char* const sneaky = "Passwort2024!";
    const PasswordStrength s =
        EstimateStrength(sneaky, std::strlen(sneaky), Crypto::KdfParams::Moderate());
    CHECK_MSG(s.EntropyBits >= kWeakEntropyBits,
              "the test checks nothing: entropy already catches it");
    CHECK_MSG(Common(sneaky), "the obvious password was not on the list");
}

TEST_CASE(Audit_CaseDoesNotMatterOnTheList)
{
    CHECK(Common("PASSWORT"));
    CHECK(Common("PaSsWoRt"));
    CHECK(Common("QwErTz"));
}

TEST_CASE(Audit_AGoodPasswordIsNotOnTheList)
{
    CHECK(!Common(kStrong));
    CHECK(!Common(kStrongToo));
    CHECK(!Common("Xk7#mQ2!vR9$"));
    CHECK(!IsCommonPassword(nullptr, 0));
    CHECK(!IsCommonPassword("", 0));
}

TEST_CASE(Audit_AKnownPasswordIsItsOwnFinding)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Passwort2024!", true);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.CommonPasswords, std::size_t{ 1 });

    const AuditFinding* f = Find(r, id);
    CHECK(f != nullptr);
    if (!f)
        return;
    CHECK_MSG(f->CommonPassword, "the known password was not reported");
    CHECK_MSG(!f->WeakPassword,
              "it also counted as weak -- then the test does not show "
              "that the list finds something on its own");
    CHECK_MSG(f->Any(), "a known password does not count as a finding");
}

TEST_CASE(Audit_WithoutAClockThereIsNoAge)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", kStrong, true);
    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.OldPasswords, std::size_t{ 0 });
    const AuditFinding* f = Find(r, id);
    CHECK_MSG(f == nullptr, "a clean entry got a finding");
}

TEST_CASE(Audit_AnOldPasswordIsMentioned)
{
    Document doc;
    Add(doc, "Account", kStrong, true);

    const std::uint64_t now = 1000 + kOldPasswordSeconds + 86400;
    const AuditReport r = Audit(doc, now);
    CHECK_EQ(r.OldPasswords, std::size_t{ 1 });
}

TEST_CASE(Audit_JustUnderTheLineIsNotOld)
{
    Document doc;
    Add(doc, "Account", kStrong, true);

    const std::uint64_t now = 1000 + kOldPasswordSeconds - 86400;
    CHECK_EQ(Audit(doc, now).OldPasswords, std::size_t{ 0 });

    CHECK_EQ(Audit(doc, 1000 + kOldPasswordSeconds).OldPasswords, std::size_t{ 1 });
}

TEST_CASE(Audit_AgeAloneIsNoFinding)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", kStrong, true);

    const AuditReport r = Audit(doc, 1000 + kOldPasswordSeconds * 2);
    CHECK_EQ(r.OldPasswords, std::size_t{ 1 });
    CHECK_MSG(r.Clean(), "age alone triggered a finding");
    CHECK_MSG(Find(r, id) == nullptr, "the entry is in the findings list");
}

TEST_CASE(Audit_AgeRidesAlongWithARealFinding)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", kWeak, true);

    const AuditReport r = Audit(doc, 1000 + kOldPasswordSeconds * 2);
    const AuditFinding* f = Find(r, id);
    CHECK(f != nullptr);
    if (!f)
        return;
    CHECK(f->WeakPassword);
    CHECK_MSG(f->OldPassword, "the age is missing on an entry that has one");
}

TEST_CASE(Audit_AChangedPasswordCountsFromTheChange)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "erstes-passwort", true);

    const std::uint64_t changedAt = 1000 + kOldPasswordSeconds * 2;
    Entry replacement;
    replacement.Title = "Account";
    replacement.Fields.push_back(Password(kStrong));
    CHECK(UpdateEntry(doc, id, replacement, changedAt).IsOk());

    CHECK_EQ(Audit(doc, changedAt + 86400).OldPasswords, std::size_t{ 0 });
    CHECK_EQ(Audit(doc, changedAt + kOldPasswordSeconds).OldPasswords,
             std::size_t{ 1 });
}

namespace
{
    std::uint64_t AddWithState(Document& doc, const char* title,
                               const char* password, std::uint32_t state)
    {
        const std::uint64_t id = Add(doc, title, password);
        CHECK(SetSecondFactorState(doc, id, state, 2000).IsOk());
        return id;
    }
}

TEST_CASE(Audit_AProviderWithoutTwoFactorIsNoFinding)
{
    Document doc;
    const std::uint64_t id =
        AddWithState(doc, "Forum", kStrong, SecondFactorNotOffered);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 0 });
    CHECK_EQ(r.SecondFactorNotOffered, std::size_t{ 1 });
    CHECK_MSG(Find(r, id) == nullptr, "the entry is in the list anyway");
    CHECK(r.Clean());
}

TEST_CASE(Audit_ASecondFactorElsewhereCountsAsHandled)
{
    Document doc;
    const std::uint64_t id =
        AddWithState(doc, "Steam", kStrong, SecondFactorElsewhere);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 0 });
    CHECK_EQ(r.SecondFactorElsewhere, std::size_t{ 1 });
    CHECK_MSG(Find(r, id) == nullptr, "the entry is in the list anyway");
}

TEST_CASE(Audit_SayingNothingStillAsks)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", kStrong);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 1 });
    const AuditFinding* f = Find(r, id);
    CHECK(f != nullptr);
    if (f)
        CHECK(f->NoSecondFactor);
}

TEST_CASE(Audit_AnUnknownStateStillAsks)
{
    Document doc;
    const std::uint64_t id = AddWithState(doc, "Account", kStrong, 99);

    const AuditReport r = Audit(doc, 0);
    CHECK_EQ(r.WithoutSecondFactor, std::size_t{ 1 });
    CHECK_EQ(r.SecondFactorNotOffered, std::size_t{ 0 });
    CHECK_EQ(r.SecondFactorElsewhere, std::size_t{ 0 });
    const AuditFinding* f = Find(r, id);
    CHECK(f != nullptr);
    if (f)
        CHECK_MSG(f->NoSecondFactor,
                  "an unknown state counted as handled");
}

TEST_CASE(Audit_AnExplainedAccountKeepsItsOtherFindings)
{
    Document doc;
    const std::uint64_t id =
        AddWithState(doc, "Forum", kWeak, SecondFactorNotOffered);

    const AuditReport r = Audit(doc, 0);
    const AuditFinding* f = Find(r, id);
    CHECK(f != nullptr);
    if (!f)
        return;
    CHECK_MSG(f->WeakPassword, "the weak password was swallowed as well");
    CHECK_MSG(!f->NoSecondFactor, "the second factor was flagged anyway");
}
