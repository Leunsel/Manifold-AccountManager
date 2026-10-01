#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/PasswordHistory.h"

#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    Entry Draft(const std::string& title)
    {
        Entry e;
        e.Title = title;
        return e;
    }

    Field Plain(const std::string& key, const std::string& value)
    {
        Field f;
        f.Key = key;
        f.Value = value;
        return f;
    }

    Field SecretField(const std::string& key, const std::string& value)
    {
        Field f = Plain(key, value);
        f.Flags |= kFieldFlagSecret;
        return f;
    }
}

TEST_CASE(Mutate_AddAssignsIdsAndTimestamps)
{
    Document doc;
    const auto a = AddEntry(doc, Draft("Steam"), 1000);
    const auto b = AddEntry(doc, Draft("Google"), 1001);
    CHECK(a.IsOk() && b.IsOk());
    CHECK(a.Value() != b.Value());
    CHECK_EQ(doc.Entries.size(), std::size_t{ 2 });

    const Entry* e = FindEntry(doc, a.Value());
    CHECK(e != nullptr);
    CHECK_EQ(e->CreatedUtc, std::uint64_t{ 1000 });
    CHECK_EQ(e->ModifiedUtc, std::uint64_t{ 1000 });
}

TEST_CASE(Mutate_IdsAreNeverReusedAfterDeleting)
{
    Document doc;
    const auto first = AddEntry(doc, Draft("Alt"), 100);
    CHECK(first.IsOk());
    CHECK(DeleteEntry(doc, first.Value()).IsOk());

    const auto second = AddEntry(doc, Draft("Neu"), 200);
    CHECK(second.IsOk());
    CHECK_MSG(second.Value() != first.Value(),
              "the deleted id was handed out again");
}

TEST_CASE(Mutate_IdCounterSurvivesARoundTripThroughTheFile)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Steam"), 100);
    CHECK(id.IsOk());
    CHECK(DeleteEntry(doc, id.Value()).IsOk());
    const std::uint64_t expected = doc.NextId;

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document reloaded;
    CHECK(Decode(w.data(), w.Size(), reloaded));
    CHECK_EQ(reloaded.NextId, expected);

    const auto again = AddEntry(reloaded, Draft("Neu"), 300);
    CHECK(again.IsOk());
    CHECK(again.Value() != id.Value());
}

TEST_CASE(Mutate_AStaleIdCounterInAFileIsRepairedUpwards)
{
    Document doc;
    doc.NextId = 1;
    Entry e = Draft("Vorhanden");
    e.Id = 77;
    doc.Entries.push_back(e);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document reloaded;
    CHECK(Decode(w.data(), w.Size(), reloaded));
    CHECK_EQ(reloaded.NextId, std::uint64_t{ 78 });

    const auto added = AddEntry(reloaded, Draft("Neu"), 1);
    CHECK(added.IsOk());
    CHECK_MSG(added.Value() == 78,
              "the new id collides with an existing entry");
}

TEST_CASE(Mutate_UpdateMovesModifiedButNotCreated)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Steam"), 1000);
    CHECK(id.IsOk());

    Entry draft = *FindEntry(doc, id.Value());
    draft.Title = "Steam (Zweitkonto)";
    CHECK(UpdateEntry(doc, id.Value(), draft, 2000).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    CHECK_EQ(stored->CreatedUtc, std::uint64_t{ 1000 });
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 2000 });
    CHECK(stored->Title == "Steam (Zweitkonto)");
}

TEST_CASE(Mutate_UpdateCannotRewriteIdentityOrBackdate)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Steam"), 1000);
    CHECK(id.IsOk());

    Entry hostile = *FindEntry(doc, id.Value());
    hostile.Id = 999;
    hostile.CreatedUtc = 1;
    hostile.ModifiedUtc = 1;
    hostile.Title = "Umbenannt";
    CHECK(UpdateEntry(doc, id.Value(), hostile, 2000).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    CHECK_EQ(stored->Id, id.Value());
    CHECK_EQ(stored->CreatedUtc, std::uint64_t{ 1000 });
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 2000 });
    CHECK(FindEntry(doc, 999) == nullptr);
}

TEST_CASE(Mutate_UpdateKeepsUnknownRecordsTheCallerNeverSaw)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Steam"), 1000);
    CHECK(id.IsOk());
    FindEntry(doc, id.Value())->Unknown.push_back(
        UnknownRecord{ 4242, { 0xDE, 0xAD, 0xBE, 0xEF } });

    Entry rebuilt;
    rebuilt.Title = "Steam";
    CHECK(UpdateEntry(doc, id.Value(), rebuilt, 2000).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    CHECK_EQ(stored->Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(stored->Unknown[0].Tag, std::uint16_t{ 4242 });
    CHECK_EQ(stored->Unknown[0].Payload.size(), std::size_t{ 4 });
}

TEST_CASE(Mutate_NormaliseTrimsTitlesButNeverValues)
{
    Entry e = Draft("   Steam   ");
    e.PlatformId = "  steam ";
    e.Fields.push_back(Plain("  Benutzer  ", "  peter  "));
    Normalise(e);

    CHECK(e.Title == "Steam");
    CHECK(e.PlatformId == "steam");
    CHECK(e.Fields[0].Key == "Benutzer");
    CHECK_MSG(e.Fields[0].Value == "  peter  ",
              "the field content was truncated: [" + e.Fields[0].Value.ToStdString() + "]");
}

TEST_CASE(Mutate_NormaliseDropsEmptyRowsButKeepsUnknownOnlyFields)
{
    Entry e = Draft("Steam");
    e.Fields.push_back(Plain("", ""));
    e.Fields.push_back(Plain("Benutzer", "peter"));
    Field future;
    future.Unknown.push_back(UnknownRecord{ 7, { 1 } });
    e.Fields.push_back(future);
    Normalise(e);

    CHECK_EQ(e.Fields.size(), std::size_t{ 2 });
    CHECK(e.Fields[0].Key == "Benutzer");
    CHECK_EQ(e.Fields[1].Unknown.size(), std::size_t{ 1 });
}

TEST_CASE(Mutate_ValidateRejectsTheThingsAnEditorCanProduce)
{
    CHECK(!Validate(Draft("")).IsOk());
    CHECK(!Validate(Draft(std::string(kMaxTitleBytes + 1, 'x'))).IsOk());

    Entry noKey = Draft("Steam");
    noKey.Fields.push_back(Plain("", "secret"));
    CHECK_MSG(!Validate(noKey).IsOk(), "a field without a name was accepted");

    Entry tooMany = Draft("Steam");
    for (std::size_t i = 0; i <= kMaxFieldsPerEntry; ++i)
        tooMany.Fields.push_back(Plain("k", "v"));
    CHECK(!Validate(tooMany).IsOk());

    Entry longValue = Draft("Steam");
    longValue.Fields.push_back(Plain("k", std::string(kMaxFieldValueBytes + 1, 'x')));
    CHECK(!Validate(longValue).IsOk());

    Entry fine = Draft("Steam");
    fine.Fields.push_back(SecretField("Password", "hunter2"));
    CHECK(Validate(fine).IsOk());
}

TEST_CASE(Mutate_AddRejectsAnInvalidDraftWithoutTouchingTheDocument)
{
    Document doc;
    CHECK(AddEntry(doc, Draft("Steam"), 100).IsOk());
    const std::uint64_t before = doc.NextId;

    CHECK(!AddEntry(doc, Draft("   "), 200).IsOk());
    CHECK_EQ(doc.Entries.size(), std::size_t{ 1 });
    CHECK_MSG(doc.NextId == before,
              "an id was used up for a rejected draft");
}

TEST_CASE(Mutate_DeleteAndFindHandleMissingIds)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Steam"), 100);
    CHECK(id.IsOk());
    CHECK(!DeleteEntry(doc, id.Value() + 500).IsOk());
    CHECK_EQ(doc.Entries.size(), std::size_t{ 1 });
    CHECK(DeleteEntry(doc, id.Value()).IsOk());
    CHECK(doc.Entries.empty());
    CHECK(FindEntry(doc, id.Value()) == nullptr);
    CHECK(!UpdateEntry(doc, id.Value(), Draft("Weg"), 200).IsOk());
}

TEST_CASE(Mutate_MoveFieldReordersAndRefusesBadIndices)
{
    Document doc;
    Entry e = Draft("Steam");
    e.Fields.push_back(Plain("a", "1"));
    e.Fields.push_back(Plain("b", "2"));
    e.Fields.push_back(Plain("c", "3"));
    const auto id = AddEntry(doc, e, 100);
    CHECK(id.IsOk());

    CHECK(MoveField(doc, id.Value(), 2, 0, 200).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored->Fields[0].Key == "c");
    CHECK(stored->Fields[1].Key == "a");
    CHECK(stored->Fields[2].Key == "b");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });

    CHECK(!MoveField(doc, id.Value(), 0, 3, 300).IsOk());
    CHECK(!MoveField(doc, id.Value(), 9, 0, 300).IsOk());
    CHECK_MSG(FindEntry(doc, id.Value())->ModifiedUtc == 200,
              "a rejected move moved the timestamp");
}

TEST_CASE(Mutate_SystemClockLooksLikeANowAndNotLikeZero)
{
    const std::uint64_t now = SystemUtcNow();
    CHECK_MSG(now > 1750000000ull,
              "the system clock returns a value from the past");
    CHECK(now < 4000000000ull);
}

namespace
{
    TotpConfig GoodTotp()
    {
        TotpConfig c;
        c.SecretBase32 = "JBSWY3DPEHPK3PXP";
        return c;
    }
}

TEST_CASE(Mutate_UpdateEntryCannotDropTotpOrRecoveryCodes)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 1000);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 1000).IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "aaaa", "bbbb" }, 1000).IsOk());

    Entry rebuilt;
    rebuilt.Title = "Bank (neu)";
    CHECK(UpdateEntry(doc, id.Value(), rebuilt, 2000).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK(stored->Title == "Bank (neu)");
    CHECK_MSG(stored->HasTotp, "the TOTP secret was deleted by a rename");
    CHECK(stored->Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP");
    CHECK_MSG(stored->RecoveryCodes.size() == 2,
              "the recovery codes were deleted by a rename");
}

TEST_CASE(Mutate_AddEntryKeepsWhatTheDraftBrings)
{
    Document doc;
    Entry draft = Draft("Importiert");
    draft.HasTotp = true;
    draft.Totp.Config = GoodTotp();
    draft.RecoveryCodes.push_back(Vault::RecoveryCode{ "xxxx", 0, {} });

    const auto id = AddEntry(doc, draft, 100);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK(stored->HasTotp);
    CHECK_EQ(stored->RecoveryCodes.size(), std::size_t{ 1 });
}

TEST_CASE(Mutate_SetTotpKeepsUnknownRecordsAcrossAReplacement)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());
    FindEntry(doc, id.Value())->Totp.Unknown.push_back(UnknownRecord{ 306, { 7 } });

    TotpConfig changed = GoodTotp();
    changed.Digits = 8;
    CHECK(SetTotp(doc, id.Value(), changed, 200).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK_EQ(stored->Totp.Config.Digits, std::uint32_t{ 8 });
    CHECK_MSG(stored->Totp.Unknown.size() == 1,
              "unknown records were lost on replacement");
}

TEST_CASE(Mutate_ClearTotpIsIdempotent)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());

    CHECK(ClearTotp(doc, id.Value(), 200).IsOk());
    CHECK(!FindEntry(doc, id.Value())->HasTotp);
    CHECK_EQ(FindEntry(doc, id.Value())->ModifiedUtc, std::uint64_t{ 200 });

    CHECK(ClearTotp(doc, id.Value(), 300).IsOk());
    CHECK_MSG(FindEntry(doc, id.Value())->ModifiedUtc == 200,
              "a delete without effect moved the timestamp");
}

TEST_CASE(Mutate_MarkingACodeUsedDoesNotMoveModifiedUtc)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 1000);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "aaaa", "bbbb" }, 1000).IsOk());
    const std::uint64_t before = FindEntry(doc, id.Value())->ModifiedUtc;

    CHECK(SetRecoveryCodeUsed(doc, id.Value(), 0, true, 5000).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK(stored->RecoveryCodes[0].IsUsed());
    CHECK_EQ(stored->RecoveryCodes[0].UsedUtc, std::uint64_t{ 5000 });
    CHECK_MSG(stored->ModifiedUtc == before,
              "ticking off moved the modification time");
}

TEST_CASE(Mutate_MarkingACodeUsedIsReversible)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 1000);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "aaaa" }, 1000).IsOk());

    CHECK(SetRecoveryCodeUsed(doc, id.Value(), 0, true, 5000).IsOk());
    CHECK(FindEntry(doc, id.Value())->RecoveryCodes[0].IsUsed());
    CHECK(SetRecoveryCodeUsed(doc, id.Value(), 0, false, 6000).IsOk());
    CHECK(!FindEntry(doc, id.Value())->RecoveryCodes[0].IsUsed());
    CHECK_EQ(FindEntry(doc, id.Value())->RecoveryCodes[0].UsedUtc, std::uint64_t{ 0 });
}

TEST_CASE(Mutate_RecoveryCodesAppendRatherThanReplace)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "alt1", "alt2" }, 100).IsOk());
    CHECK(SetRecoveryCodeUsed(doc, id.Value(), 0, true, 200).IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "neu1" }, 300).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK_EQ(stored->RecoveryCodes.size(), std::size_t{ 3 });
    CHECK(stored->RecoveryCodes[0].Value == "alt1");
    CHECK_MSG(stored->RecoveryCodes[0].IsUsed(),
              "the ticked state was lost when appending");
    CHECK(stored->RecoveryCodes[2].Value == "neu1");
}

TEST_CASE(Mutate_ARecoveryCodeThatCannotBeStoredIsRefused)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "alt1" }, 100).IsOk());

    SecureArena& arena = SecureArena::Instance();
    const ChunkLimitForTest noNewChunks(arena.ChunkCount());
    const NoHeapFallbackForTest noHeap;

    std::vector<SecureString> filler;
    const std::size_t lostBefore = arena.LostCount();
    while (arena.LostCount() == lostBefore)
    {
        SecureString s;
        s = "fuell";
        filler.push_back(std::move(s));
    }

    const Status st = AddRecoveryCodes(doc, id.Value(), { "neu1", "neu2" }, 300);
    CHECK_MSG(!st.IsOk(), "a code that could not be stored was accepted");

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK_EQ(stored->RecoveryCodes.size(), std::size_t{ 1 });
    CHECK(stored->RecoveryCodes[0].Value == "alt1");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 100 });
}

TEST_CASE(Mutate_DeleteRecoveryCodeByIndex)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "a", "b", "c" }, 100).IsOk());

    CHECK(DeleteRecoveryCode(doc, id.Value(), 1, 200).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK_EQ(stored->RecoveryCodes.size(), std::size_t{ 2 });
    CHECK(stored->RecoveryCodes[0].Value == "a");
    CHECK(stored->RecoveryCodes[1].Value == "c");

    CHECK(!DeleteRecoveryCode(doc, id.Value(), 5, 300).IsOk());
    CHECK(!SetRecoveryCodeUsed(doc, id.Value(), 5, true, 300).IsOk());
    CHECK(!DeleteRecoveryCode(doc, 9999, 0, 300).IsOk());
}

TEST_CASE(Mutate_ClearRecoveryCodesEmptiesTheWholeList)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "a", "b", "c" }, 100).IsOk());
    CHECK(SetRecoveryCodeUsed(doc, id.Value(), 1, true, 150).IsOk());

    CHECK(ClearRecoveryCodes(doc, id.Value(), 200).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK_MSG(stored->RecoveryCodes.empty(), "the list was not cleared");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });

    CHECK(ClearRecoveryCodes(doc, id.Value(), 900).IsOk());
    CHECK_MSG(FindEntry(doc, id.Value())->ModifiedUtc == 200,
              "a clear without effect moved the timestamp");

    CHECK(!ClearRecoveryCodes(doc, 9999, 300).IsOk());
}

TEST_CASE(Mutate_ClearRecoveryCodesLeavesTheRestOfTheEntryAlone)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());
    CHECK(AddRecoveryCodes(doc, id.Value(), { "a" }, 100).IsOk());
    FindEntry(doc, id.Value())->Unknown.push_back(UnknownRecord{ 199, { 4 } });

    CHECK(ClearRecoveryCodes(doc, id.Value(), 200).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK_MSG(stored->HasTotp, "clearing the code list removed the TOTP too");
    CHECK_EQ(stored->Unknown.size(), std::size_t{ 1 });
    CHECK(stored->Title == "Bank");
}

TEST_CASE(Mutate_RejectsAnUnusableTotpSecret)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());

    TotpConfig bad = GoodTotp();
    bad.SecretBase32 = "not base32!";
    CHECK(!SetTotp(doc, id.Value(), bad, 200).IsOk());

    bad = GoodTotp();
    bad.SecretBase32 = "JBSWY3DP";
    const auto shortOne = SetTotp(doc, id.Value(), bad, 200);
    CHECK_MSG(!shortOne.IsOk(), "a five-byte secret was accepted");

    CHECK_MSG(SetTotp(doc, id.Value(), GoodTotp(), 200).IsOk(),
              "a ten-byte secret was rejected");

    bad = GoodTotp();
    bad.Digits = 5;
    CHECK(!SetTotp(doc, id.Value(), bad, 200).IsOk());
    bad = GoodTotp();
    bad.Period = 0;
    CHECK(!SetTotp(doc, id.Value(), bad, 200).IsOk());

    CHECK(!SetTotp(doc, 9999, GoodTotp(), 200).IsOk());
}

TEST_CASE(Mutate_RejectsSillyRecoveryCodes)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());

    CHECK(!AddRecoveryCodes(doc, id.Value(), {}, 200).IsOk());
    CHECK(!AddRecoveryCodes(doc, id.Value(), { "" }, 200).IsOk());
    CHECK(!AddRecoveryCodes(doc, id.Value(),
                            { std::string(kMaxRecoveryCodeBytes + 1, 'x') }, 200).IsOk());

    const auto partial = AddRecoveryCodes(doc, id.Value(), { "gut", "" }, 200);
    CHECK(!partial.IsOk());
    CHECK_MSG(FindEntry(doc, id.Value())->RecoveryCodes.empty(),
              "a rejected batch stored something anyway");

    std::vector<std::string> tooMany(kMaxRecoveryCodesPerEntry + 1, "x");
    CHECK(!AddRecoveryCodes(doc, id.Value(), tooMany, 200).IsOk());
}

TEST_CASE(Mutate_ValidateCoversTheNewMembers)
{
    Entry e = Draft("Bank");
    e.HasTotp = true;
    e.Totp.Config = GoodTotp();
    CHECK(Validate(e).IsOk());

    e.Totp.Config.SecretBase32 = "!!!";
    CHECK_MSG(!Validate(e).IsOk(), "an unusable secret got through Validate");

    e.Totp.Config = GoodTotp();
    e.RecoveryCodes.push_back(Vault::RecoveryCode{ "", 0, {} });
    CHECK(!Validate(e).IsOk());
}

TEST_CASE(Mutate_RejectsAnOverlongTotpSecret)
{
    TotpConfig atLimit;
    atLimit.SecretBase32 = std::string(kMaxTotpSecretBytes, 'A');
    CHECK_MSG(ValidateTotp(atLimit).IsOk(), "exactly at the limit was rejected");

    TotpConfig overLimit;
    overLimit.SecretBase32 = std::string(kMaxTotpSecretBytes, 'A') + "        ";
    CHECK_MSG(!ValidateTotp(overLimit).IsOk(),
              "a secret over the character limit was accepted");
}

TEST_CASE(Mutate_ClearTotpWipesTheSecret)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    const unsigned char* slot = stored->Totp.Config.SecretBase32.PayloadForTests();
    const std::size_t slotBytes = stored->Totp.Config.SecretBase32.SlotBytesForTests();
    CHECK_MSG(slot != nullptr && std::memcmp(slot, "JBSW", 4) == 0,
              "the secret is not in its slot -- the case measures nothing");
    if (!slot)
        return;

    CHECK(ClearTotp(doc, id.Value(), 200).IsOk());

    CHECK(!stored->HasTotp);
    CHECK(stored->Totp.Config.SecretBase32.Empty());
    std::size_t left = 0;
    for (std::size_t i = 0; i < slotBytes; ++i)
        if (slot[i] != 0)
            ++left;
    CHECK_MSG(left == 0, std::to_string(left) + " bytes of the secret remained");
}

TEST_CASE(Mutate_SetTotpWipesTheOldSecret)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    const unsigned char* old = stored->Totp.Config.SecretBase32.PayloadForTests();
    const std::size_t oldBytes = stored->Totp.Config.SecretBase32.SlotBytesForTests();

    TotpConfig next;
    next.SecretBase32 = "MZXW6YTBOIQWY3DPEHPK3PXP";
    CHECK(SetTotp(doc, id.Value(), next, 200).IsOk());

    CHECK(stored->Totp.Config.SecretBase32 == "MZXW6YTBOIQWY3DPEHPK3PXP");
    std::size_t left = 0;
    for (std::size_t i = 0; old && i < oldBytes; ++i)
        if (old[i] != 0)
            ++left;
    CHECK_MSG(left == 0, "the old secret remained in its slot");
}

TEST_CASE(Mutate_ATotpSecretThatCannotBeStoredIsRefused)
{
    Document doc;
    const auto id = AddEntry(doc, Draft("Bank"), 100);
    CHECK(id.IsOk());
    CHECK(SetTotp(doc, id.Value(), GoodTotp(), 100).IsOk());

    TotpConfig next;
    next.SecretBase32 = "MZXW6YTBOIQWY3DPEHPK3PXP";

    {
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;

        std::vector<SecureString> filler;
        const std::size_t lostBefore = arena.LostCount();
        while (arena.LostCount() == lostBefore)
            filler.push_back(SecureString("zwanzig-zeichen-lang"));

        const Status st = SetTotp(doc, id.Value(), next, 300);
        CHECK_MSG(!st.IsOk(), "a secret that could not be stored "
                              "was reported as saved");
    }

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored != nullptr);
    if (!stored)
        return;
    CHECK(stored->HasTotp);
    CHECK_MSG(stored->Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP",
              "the stored secret was lost in the failed replacement");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 100 });

    CHECK(SetTotp(doc, id.Value(), next, 400).IsOk());
    CHECK(stored->Totp.Config.SecretBase32 == "MZXW6YTBOIQWY3DPEHPK3PXP");
}

TEST_CASE(Mutate_RenamingTrimsChecksAndStampsOnlyAChange)
{
    Document doc;
    Entry e = Draft("Steam");
    e.Fields.push_back(SecretField("Password", "stays-as-it-is"));
    const auto id = AddEntry(doc, std::move(e), 100);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;
    const Entry* stored = FindEntry(doc, id.Value());

    CHECK(SetEntryTitle(doc, id.Value(), "  Steam (Zweitkonto)  ", 200).IsOk());
    CHECK(stored->Title == "Steam (Zweitkonto)");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });
    CHECK(stored->Fields.size() == 1 && stored->Fields[0].Value == "stays-as-it-is");

    CHECK(SetEntryTitle(doc, id.Value(), "Steam (Zweitkonto)", 300).IsOk());
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });

    CHECK(!SetEntryTitle(doc, id.Value(), "   ", 400).IsOk());
    CHECK(!SetEntryTitle(doc, id.Value(), std::string(kMaxTitleBytes + 1, 't'), 400).IsOk());
    CHECK(SetEntryTitle(doc, id.Value(), std::string(kMaxTitleBytes, 't'), 400).IsOk());
    CHECK(!SetEntryTitle(doc, id.Value() + 99, "does not exist", 400).IsOk());
}

TEST_CASE(Mutate_ASymbolIsTrimmedCheckedAndStampedOnlyOnAChange)
{
    Document doc;
    Entry e = Draft("Sparkasse");
    e.PlatformId = "sparkasse";
    e.Fields.push_back(SecretField("Password", "stays-as-it-is"));
    const auto id = AddEntry(doc, std::move(e), 100);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;
    const Entry* stored = FindEntry(doc, id.Value());

    CHECK(SetSymbol(doc, id.Value(), "  building-columns  ", 200).IsOk());
    CHECK(stored->Symbol == "building-columns");
    CHECK(stored->PlatformId == "sparkasse");
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });
    CHECK(stored->Fields.size() == 1 && stored->Fields[0].Value == "stays-as-it-is");

    CHECK(SetSymbol(doc, id.Value(), "building-columns", 300).IsOk());
    CHECK_EQ(stored->ModifiedUtc, std::uint64_t{ 200 });

    CHECK(SetSymbol(doc, id.Value(), "vault-2031", 400).IsOk());
    CHECK(stored->Symbol == "vault-2031");

    CHECK(!SetSymbol(doc, id.Value(), std::string(kMaxSymbolBytes + 1, 's'), 500).IsOk());
    CHECK(stored->Symbol == "vault-2031");
    CHECK(SetSymbol(doc, id.Value(), "", 600).IsOk());
    CHECK(stored->Symbol.empty());
    CHECK(!SetSymbol(doc, id.Value() + 99, "euro-sign", 700).IsOk());
}

TEST_CASE(Mutate_TakeAndRestoreKeepIdPlaceAndHistory)
{
    Document doc;
    const auto a = AddEntry(doc, Draft("Alpha"), 1000);
    Entry withPw = Draft("Beta");
    Field pw = SecretField("Password", "erstes-passwort");
    pw.Role = FieldRole::Password;
    withPw.Fields.push_back(std::move(pw));
    const auto b = AddEntry(doc, std::move(withPw), 1000);
    const auto c = AddEntry(doc, Draft("Gamma"), 1000);
    CHECK(a.IsOk() && b.IsOk() && c.IsOk());
    if (!a.IsOk() || !b.IsOk() || !c.IsOk())
        return;

    Entry changed;
    CHECK(CopyEntry(*FindEntry(doc, b.Value()), changed));
    changed.Fields[0].Value = std::string("zweites-passwort");
    CHECK(UpdateEntry(doc, b.Value(), std::move(changed), 2000).IsOk());
    const std::string old = "erstes-passwort";
    const auto* oldBytes = reinterpret_cast<const unsigned char*>(old.data());
    CHECK(WasUsedBefore(doc, *FindEntry(doc, b.Value()), oldBytes, old.size()));

    Entry taken;
    std::size_t index = 99;
    CHECK(TakeEntry(doc, b.Value(), taken, index).IsOk());
    CHECK_EQ(index, std::size_t{ 1 });
    CHECK(FindEntry(doc, b.Value()) == nullptr);
    CHECK_EQ(doc.Entries.size(), std::size_t{ 2 });

    CHECK(RestoreEntry(doc, std::move(taken), index).IsOk());
    const Entry* back = FindEntry(doc, b.Value());
    CHECK(back != nullptr);
    if (!back)
        return;
    CHECK_MSG(doc.Entries[1].Id == b.Value(), "not in its old place");
    CHECK_EQ(back->ModifiedUtc, std::uint64_t{ 2000 });
    CHECK_MSG(WasUsedBefore(doc, *back, oldBytes, old.size()),
              "the password history was lost when restoring");
    CHECK(back->Fields.size() == 1 && back->Fields[0].Value.View() == "zweites-passwort");

    const auto d = AddEntry(doc, Draft("Delta"), 3000);
    CHECK(d.IsOk() && d.Value() > c.Value());
}

TEST_CASE(Mutate_RestoreRefusesAForeignOrTakenId)
{
    Document doc;
    const auto a = AddEntry(doc, Draft("Alpha"), 1000);
    CHECK(a.IsOk());
    if (!a.IsOk())
        return;

    Entry never = Draft("Nie vergeben");
    never.Id = doc.NextId;
    CHECK_MSG(!RestoreEntry(doc, std::move(never), 0).IsOk(),
              "an id the counter never handed out got in");

    Entry twin = Draft("Zwilling");
    twin.Id = a.Value();
    CHECK_MSG(!RestoreEntry(doc, std::move(twin), 0).IsOk(),
              "an id already in use got in a second time");

    Entry zero = Draft("Null");
    CHECK(!RestoreEntry(doc, std::move(zero), 0).IsOk());
    CHECK_EQ(doc.Entries.size(), std::size_t{ 1 });

    Entry taken;
    std::size_t index = 0;
    CHECK(TakeEntry(doc, a.Value(), taken, index).IsOk());
    CHECK(RestoreEntry(doc, std::move(taken), 500).IsOk());
    CHECK_EQ(doc.Entries.size(), std::size_t{ 1 });
}
