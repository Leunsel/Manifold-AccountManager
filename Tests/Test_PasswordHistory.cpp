#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/PasswordHistory.h"

#include <cstring>
#include <string>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;

    Field Password(const char* value)
    {
        Field f;
        f.Key = "Password";
        f.Value = value;
        f.Role = FieldRole::Password;
        f.Flags |= kFieldFlagSecret;
        return f;
    }

    std::uint64_t Add(Document& doc, const char* title, const char* password)
    {
        Entry e;
        e.Title = title;
        if (password)
            e.Fields.push_back(Password(password));
        const auto id = AddEntry(doc, e, kNow);
        CHECK(id.IsOk());
        return id.IsOk() ? id.Value() : 0;
    }

    Status ChangePassword(Document& doc, std::uint64_t id, const char* to,
                          std::uint64_t when)
    {
        const Entry* stored = FindEntry(doc, id);
        CHECK(stored != nullptr);
        if (!stored)
            return Error{ ErrorCode::InvalidArgument, "missing" };
        Entry replacement;
        replacement.Title = stored->Title;
        if (to)
            replacement.Fields.push_back(Password(to));
        return UpdateEntry(doc, id, replacement, when);
    }

    bool Knows(const Document& doc, std::uint64_t id, const char* password)
    {
        const Entry* e = FindEntry(doc, id);
        if (!e)
            return false;
        return WasUsedBefore(doc, *e,
                             reinterpret_cast<const unsigned char*>(password),
                             std::strlen(password));
    }

    std::size_t HistorySize(const Document& doc, std::uint64_t id)
    {
        const Entry* e = FindEntry(doc, id);
        return e ? e->PasswordHistory.size() : 0;
    }
}

TEST_CASE(History_RecognisesAPasswordThatWasUsedBefore)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "erstes-passwort");
    CHECK(ChangePassword(doc, id, "zweites-passwort", kNow + 100).IsOk());

    CHECK_MSG(Knows(doc, id, "erstes-passwort"),
              "the retired password was not recognised");
    CHECK_MSG(!Knows(doc, id, "drittes-passwort"),
              "a never-used password was reported as known");
}

TEST_CASE(History_HoldsNoPasswordAnywhere)
{
    const char* const secret = "CANARYVOGEL-4711";

    Document doc;
    const std::uint64_t id = Add(doc, "Account", secret);
    CHECK(ChangePassword(doc, id, "etwas-anderes", kNow + 100).IsOk());
    CHECK_EQ(HistorySize(doc, id), std::size_t{ 1 });

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    const std::string bytes(reinterpret_cast<const char*>(w.data()),
                            w.size());
    CHECK_MSG(bytes.find(secret) == std::string::npos,
              "the retired password is in the file in plain text");

    CHECK_MSG(bytes.find("etwas-anderes") != std::string::npos,
              "the control check does not trigger, the test says nothing");
}

TEST_CASE(History_TheSameEntryDoesNotRememberItTwice)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "A");
    CHECK(ChangePassword(doc, id, "B", kNow + 1).IsOk());
    CHECK(ChangePassword(doc, id, "A", kNow + 2).IsOk());
    CHECK(ChangePassword(doc, id, "B", kNow + 3).IsOk());

    CHECK_EQ(HistorySize(doc, id), std::size_t{ 2 });
    CHECK(Knows(doc, id, "A"));
    CHECK(Knows(doc, id, "B"));
}

TEST_CASE(History_IsCappedAndTheOldestFallsOut)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "pw-000");

    char buffer[16];
    for (std::size_t i = 1; i <= kMaxPasswordHistory + 3; ++i)
    {
        std::snprintf(buffer, sizeof(buffer), "pw-%03zu", i);
        CHECK(ChangePassword(doc, id, buffer, kNow + i).IsOk());
    }

    CHECK_EQ(HistorySize(doc, id), kMaxPasswordHistory);
    CHECK_MSG(!Knows(doc, id, "pw-000"),
              "the oldest password should have dropped out");
    CHECK_MSG(Knows(doc, id, "pw-003"), "a newer password was lost");
}

TEST_CASE(History_ACopyStartsWithoutOne)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "erstes-passwort");
    CHECK(ChangePassword(doc, id, "zweites-passwort", kNow + 100).IsOk());
    CHECK_EQ(HistorySize(doc, id), std::size_t{ 1 });

    const Entry* original = FindEntry(doc, id);
    CHECK(original != nullptr);
    if (!original)
        return;
    Entry copy = *original;
    copy.Title += " (Kopie)";
    const auto added = AddEntry(doc, std::move(copy), kNow + 200);
    CHECK(added.IsOk());
    if (!added.IsOk())
        return;

    CHECK_MSG(HistorySize(doc, added.Value()) == 0,
              "the copy inherited the original's history");
}

TEST_CASE(History_IsPerEntryAndNotPerVault)
{
    Document doc;
    const std::uint64_t a = Add(doc, "Erster", "geteilt");
    const std::uint64_t b = Add(doc, "Zweiter", "geteilt");
    CHECK(ChangePassword(doc, a, "neu-a", kNow + 1).IsOk());
    CHECK(ChangePassword(doc, b, "neu-b", kNow + 2).IsOk());

    CHECK(Knows(doc, a, "geteilt"));
    CHECK(Knows(doc, b, "geteilt"));

    const Entry* ea = FindEntry(doc, a);
    const Entry* eb = FindEntry(doc, b);
    CHECK(ea && eb && ea->PasswordHistory.size() == 1 && eb->PasswordHistory.size() == 1);
    if (!ea || !eb || ea->PasswordHistory.empty() || eb->PasswordHistory.empty())
        return;
    CHECK_MSG(std::memcmp(ea->PasswordHistory[0].Mac, eb->PasswordHistory[0].Mac,
                          Crypto::kHashBytes) != 0,
              "two entries with the same password have the same fingerprint");
}

TEST_CASE(History_TwoVaultsProduceDifferentPrints)
{
    Document one, two;
    const std::uint64_t a = Add(one, "Account", "dasselbe");
    const std::uint64_t b = Add(two, "Account", "dasselbe");
    CHECK(ChangePassword(one, a, "x", kNow + 1).IsOk());
    CHECK(ChangePassword(two, b, "x", kNow + 1).IsOk());

    CHECK(one.HistoryKey.size() == kHistoryKeyBytes);
    CHECK_MSG(one.HistoryKey != two.HistoryKey, "both vaults share a key");
    CHECK(std::memcmp(FindEntry(one, a)->PasswordHistory[0].Mac,
                      FindEntry(two, b)->PasswordHistory[0].Mac,
                      Crypto::kHashBytes) != 0);
}

TEST_CASE(History_SurvivesTheFile)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "alt");
    CHECK(ChangePassword(doc, id, "neu", kNow + 60).IsOk());

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));

    CHECK(back.HistoryKey == doc.HistoryKey);
    CHECK_EQ(HistorySize(back, id), std::size_t{ 1 });
    CHECK_MSG(Knows(back, id, "alt"),
              "after the round trip the old password is no longer recognised");
    CHECK_EQ(FindEntry(back, id)->PasswordHistory[0].RetiredUtc, kNow + 60);
}

TEST_CASE(History_CostsNothingWhileNoPasswordEverChanged)
{
    Document doc;
    Add(doc, "Account", "unveraendert");
    CHECK_MSG(doc.HistoryKey.empty(), "the key was created without need");

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK(back.HistoryKey.empty());
    CHECK(back.Entries[0].PasswordHistory.empty());
}

TEST_CASE(History_AFirstPasswordRetiresNothing)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Notiz", nullptr);
    CHECK(ChangePassword(doc, id, "erstes", kNow + 1).IsOk());

    CHECK_MSG(HistorySize(doc, id) == 0,
              "a password set for the first time retired something");
    CHECK_MSG(doc.HistoryKey.empty(), "the key was created without need");
}

TEST_CASE(History_RemovingThePasswordIsNoChange)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "vorhanden");
    CHECK(ChangePassword(doc, id, nullptr, kNow + 1).IsOk());
    CHECK_EQ(HistorySize(doc, id), std::size_t{ 0 });
}

TEST_CASE(History_GivesTheAgeOfTheCurrentPassword)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "alt");
    CHECK_MSG(CurrentPasswordSince(*FindEntry(doc, id)) == 0,
              "a never-changed password has no date");

    CHECK(ChangePassword(doc, id, "mittel", kNow + 100).IsOk());
    CHECK(ChangePassword(doc, id, "neu", kNow + 500).IsOk());
    CHECK_MSG(CurrentPasswordSince(*FindEntry(doc, id)) == kNow + 500,
              "the age does not come from the latest change");
}

TEST_CASE(History_WithoutAKeyNothingIsClaimed)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "alt");
    CHECK(ChangePassword(doc, id, "neu", kNow + 1).IsOk());
    CHECK(Knows(doc, id, "alt"));

    doc.HistoryKey.clear();
    CHECK_MSG(!Knows(doc, id, "alt"),
              "a match was reported without a key");
}

TEST_CASE(History_AnUnknownRecordInsideAMemorySurvives)
{
    unsigned char mac[Crypto::kHashBytes] = {};
    mac[0] = 0x42;

    Body w([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Account");
        out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryPasswordHistory));
        out.WriteBytes(static_cast<std::uint16_t>(Tag::HistoryMacBytes),
                       mac, sizeof(mac));
        out.WriteU64(static_cast<std::uint16_t>(Tag::HistoryRetiredUtc), 99);
        out.WriteString(510, "from a later version");
        out.EndRecord();
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty() || back.Entries[0].PasswordHistory.empty())
    {
        CHECK_MSG(false, "the history entry was not read");
        return;
    }
    CHECK_EQ(back.Entries[0].PasswordHistory[0].Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(back.Entries[0].PasswordHistory[0].RetiredUtc, std::uint64_t{ 99 });
}

TEST_CASE(History_AMacOfTheWrongLengthMakesTheEntryUnreadable)
{
    const unsigned char tooShort[4] = { 1, 2, 3, 4 };

    Body w([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryPasswordHistory));
        out.WriteBytes(static_cast<std::uint16_t>(Tag::HistoryMacBytes),
                       tooShort, sizeof(tooShort));
        out.EndRecord();
        out.EndRecord();
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a fingerprint of the wrong length was accepted");
}

TEST_CASE(History_TheMacRefusesWithoutAKey)
{
    Document doc;
    unsigned char mac[Crypto::kHashBytes] = {};
    const char* const pw = "irgendwas";

    CHECK_MSG(!HistoryMac(doc, 1, reinterpret_cast<const unsigned char*>(pw),
                          std::strlen(pw), mac).IsOk(),
              "a fingerprint was computed without a key");

    CHECK(EnsureHistoryKey(doc));
    CHECK(HistoryMac(doc, 1, reinterpret_cast<const unsigned char*>(pw),
                     std::strlen(pw), mac).IsOk());
}

TEST_CASE(History_SavingWithoutChangingThePasswordRemembersNothing)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "stays-the-same");

    for (int i = 0; i < 3; ++i)
        CHECK(ChangePassword(doc, id, "stays-the-same", kNow + 10 + i).IsOk());

    CHECK_MSG(HistorySize(doc, id) == 0,
              "a save without a change added something to the history");
    CHECK_MSG(!Knows(doc, id, "stays-the-same"),
              "the current password is in its own history");
    CHECK_MSG(doc.HistoryKey.empty(), "the key was created without need");
}

TEST_CASE(History_AMemoryWithoutAMacIsRefused)
{
    Body w([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Account");
        out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryPasswordHistory));
        out.WriteU64(static_cast<std::uint16_t>(Tag::HistoryRetiredUtc), 99);
        out.EndRecord();
        out.EndRecord();
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a history entry without a fingerprint was accepted");
}

TEST_CASE(History_AKeyOfTheWrongLengthMakesTheVaultUnreadable)
{
    const unsigned char tooShort[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Body w([&](Writer& out) {
        out.WriteBytes(static_cast<std::uint16_t>(Tag::VaultHistoryKey),
                       tooShort, sizeof(tooShort));
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a key of the wrong length was accepted");
}
