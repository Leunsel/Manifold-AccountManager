#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Folder.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/VaultFormat.h"

#include <string>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;
    constexpr std::uint64_t kLater = 1'700'009'999;

    Entry Draft(const char* title, const char* folder = "")
    {
        Entry e;
        e.Title = title;
        e.Folder = folder;
        return e;
    }

    std::uint64_t Add(Document& doc, const char* title, const char* folder = "",
                      std::uint64_t when = kNow)
    {
        const auto id = AddEntry(doc, Draft(title, folder), when);
        CHECK(id.IsOk());
        return id.IsOk() ? id.Value() : 0;
    }

    std::string FolderOf(const Document& doc, std::uint64_t id)
    {
        const Entry* e = FindEntry(doc, id);
        return e ? e->Folder : std::string("<missing>");
    }

    std::uint64_t StampOf(const Document& doc, std::uint64_t id)
    {
        const Entry* e = FindEntry(doc, id);
        return e ? e->ModifiedUtc : 0;
    }

    std::size_t CountTagInFirstEntry(const Body& w, Tag want)
    {
        Reader top(w.data(), w.size());
        std::uint16_t tag = 0;
        const unsigned char* p = nullptr;
        std::size_t n = 0;
        while (top.Next(tag, p, n))
        {
            if (static_cast<Tag>(tag) != Tag::Entry)
                continue;
            std::size_t found = 0;
            Reader inner(p, n);
            std::uint16_t innerTag = 0;
            const unsigned char* ip = nullptr;
            std::size_t in = 0;
            while (inner.Next(innerTag, ip, in))
                if (static_cast<Tag>(innerTag) == want)
                    ++found;
            return found;
        }
        return 0;
    }
}

TEST_CASE(Folder_SurvivesTheFile)
{
    Document doc;
    Add(doc, "Account", "Arbeit");

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].Folder == "Arbeit");
}

TEST_CASE(Folder_NoFolderCostsNoRecord)
{
    Document with;
    Add(with, "Account", "Arbeit");
    Document without;
    Add(without, "Account");

    Body a([&](Writer& out) {
        Encode(with, out);
    });
    Body b([&](Writer& out) {
        Encode(without, out);
    });
    CHECK_EQ(CountTagInFirstEntry(a, Tag::EntryFolder), std::size_t{ 1 });
    CHECK_MSG(CountTagInFirstEntry(b, Tag::EntryFolder) == 0,
              "an entry without a folder still got a record");
}

TEST_CASE(Folder_AnEmptyRecordIsTheSameAsNoRecord)
{
    Body w([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Account");
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryFolder), "");
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].Folder.empty());
    CHECK_MSG(FolderNames(back).empty(),
              "an empty record created a folder without a name");
}

TEST_CASE(Folder_ARecordFromALaterVersionSurvivesUntouched)
{
    Body w([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Account");
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryFolder), "Arbeit");
        out.WriteString(111, "a later property");
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].Folder == "Arbeit");
    CHECK_EQ(back.Entries[0].Unknown.size(), std::size_t{ 1 });
}

TEST_CASE(Folder_UpdateEntryCannotDropIt)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");

    Entry rebuilt;
    rebuilt.Title = "Account, umbenannt";
    CHECK(UpdateEntry(doc, id, rebuilt, kLater).IsOk());

    CHECK_MSG(FolderOf(doc, id) == "Arbeit",
              "UpdateEntry took the folder from the candidate");
    const Entry* e = FindEntry(doc, id);
    CHECK(e && e->Title == "Account, umbenannt");
}

TEST_CASE(Folder_UpdateEntryCannotSetItEither)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");

    Entry rebuilt;
    rebuilt.Title = "Account";
    rebuilt.Folder = "Privat";
    CHECK(UpdateEntry(doc, id, rebuilt, kLater).IsOk());
    CHECK(FolderOf(doc, id) == "Arbeit");
}

TEST_CASE(Folder_NormaliseTrimsItAndBlankMeansNoFolder)
{
    Entry e = Draft("Account", "  Arbeit  ");
    Normalise(e);
    CHECK_MSG(e.Folder == "Arbeit", "a space would create two folders");

    Entry blank = Draft("Account", "   ");
    Normalise(blank);
    CHECK(blank.Folder.empty());
}

TEST_CASE(Folder_ValidateRejectsAnOverlongNameAndControlBytes)
{
    CHECK(ValidateFolderName("Arbeit").IsOk());
    CHECK_MSG(!ValidateFolderName("").IsOk(), "the empty name got through");
    CHECK(!ValidateFolderName(std::string(kMaxFolderNameBytes + 1, 'x')).IsOk());

    CHECK(!ValidateFolderName("Ar\tbeit").IsOk());
    CHECK(!ValidateFolderName("Ar\nbeit").IsOk());

    CHECK(!Validate(Draft("Account", "Ar\tbeit")).IsOk());
    CHECK_MSG(Validate(Draft("Account")).IsOk(),
              "no folder was treated as an error");
}

TEST_CASE(Folder_FoldingIsCaseOnlyAndNotDiacritic)
{
    CHECK(SameFolder("Arbeit", "arbeit"));
    CHECK(SameFolder("B\xC3\x9CRO", "b\xC3\xBCro"));

    CHECK_MSG(!SameFolder("B\xC3\xBC" "ro", "Buero"),
              "search folding is used as the folder key");
    CHECK_MSG(!SameFolder("Stra\xC3\x9F" "e", "Strasse"),
              "the sharp s was folded together");
    CHECK(!SameFolder("Arbeit", "Privat"));
}

TEST_CASE(Folder_SetAdoptsTheExistingSpelling)
{
    Document doc;
    Add(doc, "Erster", "Arbeit");
    const std::uint64_t id = Add(doc, "Zweiter");

    CHECK(SetEntryFolder(doc, id, "  ARBEIT ", kLater).IsOk());
    CHECK_MSG(FolderOf(doc, id) == "Arbeit",
              "the typed spelling displaced the stored one");
    CHECK_EQ(FolderNames(doc).size(), std::size_t{ 1 });
}

TEST_CASE(Folder_AddEntryAdoptsTheExistingSpelling)
{
    Document doc;
    Add(doc, "Erster", "Arbeit");
    const std::uint64_t id = Add(doc, "Zweiter", "arbeit");

    CHECK(FolderOf(doc, id) == "Arbeit");
    CHECK_EQ(FolderNames(doc).size(), std::size_t{ 1 });
}

TEST_CASE(Folder_SettingTheSameFolderDoesNotMoveModifiedUtc)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");
    CHECK_EQ(StampOf(doc, id), kNow);

    CHECK(SetEntryFolder(doc, id, "Arbeit", kLater).IsOk());
    CHECK_MSG(StampOf(doc, id) == kNow,
              "a no-op moved the timestamp");

    CHECK(SetEntryFolder(doc, id, "Privat", kLater).IsOk());
    CHECK_EQ(StampOf(doc, id), kLater);
}

TEST_CASE(Folder_AnEmptyNameTakesTheEntryOut)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");
    CHECK(SetEntryFolder(doc, id, "", kLater).IsOk());
    CHECK(FolderOf(doc, id).empty());
    CHECK(FolderNames(doc).empty());
}

TEST_CASE(Folder_RenameStampsOnlyTheEntriesItTouched)
{
    Document doc;
    const std::uint64_t a = Add(doc, "Erster", "Arbeit");
    const std::uint64_t b = Add(doc, "Zweiter", "Arbeit");
    const std::uint64_t outside = Add(doc, "Dritter", "Privat");

    const auto moved = RenameFolder(doc, "Arbeit", "Beruf", kLater);
    CHECK(moved.IsOk());
    CHECK_EQ(moved.Value(), std::size_t{ 2 });

    CHECK(FolderOf(doc, a) == "Beruf");
    CHECK(FolderOf(doc, b) == "Beruf");
    CHECK_EQ(StampOf(doc, a), kLater);
    CHECK_MSG(StampOf(doc, outside) == kNow,
              "an entry outside the folder was stamped");
    CHECK(FolderOf(doc, outside) == "Privat");
}

TEST_CASE(Folder_RenameOfAnAbsentFolderChangesNothing)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");

    const auto moved = RenameFolder(doc, "GibtsNicht", "Beruf", kLater);
    CHECK(moved.IsOk());
    CHECK_EQ(moved.Value(), std::size_t{ 0 });
    CHECK(FolderOf(doc, id) == "Arbeit");
    CHECK_EQ(StampOf(doc, id), kNow);
}

TEST_CASE(Folder_RenameKeepsTheRestOfTheEntry)
{
    Document doc;
    Entry draft = Draft("Account", "Arbeit");
    draft.Unknown.push_back(UnknownRecord{ 159, { 1, 2, 3 } });
    RecoveryCode code;
    code.Value = "aaaa-1111";
    draft.RecoveryCodes.push_back(code);
    const auto id = AddEntry(doc, draft, kNow);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;

    CHECK(RenameFolder(doc, "Arbeit", "Beruf", kLater).IsOk());
    const Entry* e = FindEntry(doc, id.Value());
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK(e->Folder == "Beruf");
    CHECK_EQ(e->Unknown.size(), std::size_t{ 1 });
    CHECK_EQ(e->RecoveryCodes.size(), std::size_t{ 1 });
}

TEST_CASE(Folder_RenameToAnEmptyNameIsRefused)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");

    CHECK(!RenameFolder(doc, "Arbeit", "", kLater).IsOk());
    CHECK(!RenameFolder(doc, "Arbeit", "   ", kLater).IsOk());
    CHECK(!RenameFolder(doc, "Arbeit", "Be\truf", kLater).IsOk());
    CHECK_MSG(FolderOf(doc, id) == "Arbeit", "a rejected rename took effect");
}

TEST_CASE(Folder_RenameMergesIntoAnExistingFolder)
{
    Document doc;
    const std::uint64_t a = Add(doc, "Erster", "Arbeit");
    const std::uint64_t b = Add(doc, "Zweiter", "Beruf");

    const auto moved = RenameFolder(doc, "Arbeit", "beruf", kLater);
    CHECK(moved.IsOk());
    CHECK_EQ(moved.Value(), std::size_t{ 1 });

    CHECK(FolderOf(doc, a) == "Beruf");
    CHECK(FolderOf(doc, b) == "Beruf");
    CHECK_EQ(FolderNames(doc).size(), std::size_t{ 1 });
}

TEST_CASE(Folder_RenameCanChangeTheSpellingOfItsOwnFolder)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "arbeit");

    const auto moved = RenameFolder(doc, "arbeit", "Arbeit", kLater);
    CHECK(moved.IsOk());
    CHECK_EQ(moved.Value(), std::size_t{ 1 });
    CHECK(FolderOf(doc, id) == "Arbeit");
}

TEST_CASE(Folder_DissolveTakesEntriesOutWithoutDeletingThem)
{
    Document doc;
    const std::uint64_t a = Add(doc, "Erster", "Arbeit");
    const std::uint64_t b = Add(doc, "Zweiter", "Privat");

    const auto moved = DissolveFolder(doc, "ARBEIT", kLater);
    CHECK(moved.IsOk());
    CHECK_EQ(moved.Value(), std::size_t{ 1 });

    CHECK_MSG(doc.Entries.size() == 2, "dissolving deleted an entry");
    CHECK(FolderOf(doc, a).empty());
    CHECK(FolderOf(doc, b) == "Privat");
    CHECK_EQ(StampOf(doc, a), kLater);
    CHECK_EQ(StampOf(doc, b), kNow);
}

TEST_CASE(Folder_DeletingTheLastEntryMakesTheFolderDisappear)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account", "Arbeit");
    CHECK_EQ(FolderNames(doc).size(), std::size_t{ 1 });

    CHECK(DeleteEntry(doc, id).IsOk());
    CHECK_MSG(FolderNames(doc).empty(), "a folder without entries was left over");
}

TEST_CASE(Folder_NamesAreOrderedByTheFoldedName)
{
    Document doc;
    Add(doc, "A", "Spiele");
    Add(doc, "B", "arbeitsam");
    Add(doc, "C", "Arbeit");

    const std::vector<std::string> names = FolderNames(doc);
    CHECK_EQ(names.size(), std::size_t{ 3 });
    if (names.size() != 3)
        return;
    CHECK(names[0] == "Arbeit");
    CHECK(names[1] == "arbeitsam");
    CHECK(names[2] == "Spiele");
}

TEST_CASE(Folder_CountAndSpellingSeeFoldedEqualNames)
{
    Document doc;
    Entry a = Draft("Erster", "Arbeit");
    Entry b = Draft("Zweiter", "arbeit");
    doc.Entries.push_back(a);
    doc.Entries.push_back(b);

    CHECK_EQ(CountEntriesInFolder(doc, "ARBEIT"), std::size_t{ 2 });
    CHECK_EQ(FolderNames(doc).size(), std::size_t{ 1 });
    CHECK(ExistingSpelling(doc, "arbeit") == "Arbeit");
    CHECK(ExistingSpelling(doc, "Privat").empty());
    CHECK_EQ(CountEntriesInFolder(doc, ""), std::size_t{ 0 });
}

TEST_CASE(Folder_SurveyCountsEveryEntryExactlyOnce)
{
    Document doc;
    Add(doc, "A", "Arbeit");
    Add(doc, "B", "Arbeit");
    Add(doc, "C", "Spiele");
    Add(doc, "D");
    Add(doc, "E");

    const FolderSurvey survey = SurveyFolders(doc);
    CHECK_EQ(survey.Total, std::size_t{ 5 });
    CHECK_EQ(survey.Unfiled, std::size_t{ 2 });
    CHECK_EQ(survey.Folders.size(), std::size_t{ 2 });

    std::size_t inFolders = 0;
    for (const FolderTally& row : survey.Folders)
        inFolders += row.Count;
    CHECK_MSG(inFolders + survey.Unfiled == survey.Total,
              "in folders " + std::to_string(inFolders) + " + unfiled " +
                  std::to_string(survey.Unfiled) + " is not " +
                  std::to_string(survey.Total));
}

TEST_CASE(Folder_SurveyReportsNoUnfiledWhenEveryEntryHasAFolder)
{
    Document doc;
    Add(doc, "A", "Arbeit");
    Add(doc, "B", "Spiele");

    const FolderSurvey full = SurveyFolders(doc);
    CHECK_EQ(full.Unfiled, std::size_t{ 0 });
    CHECK_EQ(full.Total, std::size_t{ 2 });

    Add(doc, "C");
    CHECK_EQ(SurveyFolders(doc).Unfiled, std::size_t{ 1 });

    const Document empty;
    CHECK_EQ(SurveyFolders(empty).Unfiled, std::size_t{ 0 });
    CHECK_EQ(SurveyFolders(empty).Total, std::size_t{ 0 });
    CHECK(SurveyFolders(empty).Folders.empty());
}

TEST_CASE(Folder_SurveyTreatsFoldedEqualSpellingsAsOneFolder)
{
    Document doc;
    Entry a = Draft("Erster", "Arbeit");
    Entry b = Draft("Zweiter", "arbeit");
    Entry c = Draft("Dritter", "ARBEIT");
    doc.Entries.push_back(a);
    doc.Entries.push_back(b);
    doc.Entries.push_back(c);

    const FolderSurvey survey = SurveyFolders(doc);
    CHECK_EQ(survey.Folders.size(), std::size_t{ 1 });
    if (survey.Folders.empty())
        return;
    CHECK_EQ(survey.Folders[0].Count, std::size_t{ 3 });
    CHECK(survey.Folders[0].Name == "Arbeit");
    CHECK(survey.Folders[0].Folded == FoldFolderName("Arbeit"));
}

TEST_CASE(Folder_SurveyIsOrderedByTheFoldedName)
{
    Document doc;
    Add(doc, "A", "Spiele");
    Add(doc, "B", "arbeitsam");
    Add(doc, "C", "Arbeit");

    const FolderSurvey survey = SurveyFolders(doc);
    CHECK_EQ(survey.Folders.size(), std::size_t{ 3 });
    if (survey.Folders.size() != 3)
        return;
    CHECK(survey.Folders[0].Name == "Arbeit");
    CHECK(survey.Folders[1].Name == "arbeitsam");
    CHECK(survey.Folders[2].Name == "Spiele");
}

TEST_CASE(Folder_SurveyAgreesWithTheSingleAnswers)
{
    Document doc;
    Add(doc, "A", "Arbeit");
    Add(doc, "B", "arbeit");
    Add(doc, "C", "Spiele");
    Add(doc, "D", "Zoo");
    Add(doc, "E");
    Add(doc, "F");
    Add(doc, "G", "Spiele");

    const FolderSurvey survey = SurveyFolders(doc);
    const std::vector<std::string> names = FolderNames(doc);

    CHECK_EQ(survey.Folders.size(), names.size());
    for (std::size_t i = 0; i < survey.Folders.size() && i < names.size(); ++i)
    {
        CHECK_MSG(survey.Folders[i].Name == names[i],
                  "row " + std::to_string(i) + ": \"" + survey.Folders[i].Name +
                      "\" vs \"" + names[i] + "\"");
        const std::size_t single = CountEntriesInFolder(doc, names[i]);
        CHECK_MSG(survey.Folders[i].Count == single,
                  "\"" + names[i] + "\": overview says " +
                      std::to_string(survey.Folders[i].Count) + ", single query " +
                      std::to_string(single));
    }
    CHECK_EQ(survey.Total, doc.Entries.size());
}

namespace
{
    void ExpectMonograms(const std::vector<std::string>& names,
                         const std::vector<std::string>& expected)
    {
        const std::vector<std::string> got = FolderMonograms(names);
        CHECK_EQ(got.size(), expected.size());
        for (std::size_t i = 0; i < got.size() && i < expected.size(); ++i)
            CHECK_MSG(got[i] == expected[i],
                      "\"" + names[i] + "\": \"" + got[i] + "\" instead of \"" + expected[i] + "\"");
    }
}

TEST_CASE(Folder_MonogramOfOneWordIsCapitalAndSmall)
{
    ExpectMonograms({ "Arbeit", "gaming", "X" }, { "Ar", "Ga", "X" });
}

TEST_CASE(Folder_MonogramOfSeveralWordsTakesTheInitials)
{
    ExpectMonograms({ "Online Banking", "steam-keys", "2FA Codes", "a b c" },
                    { "OB", "SK", "2C", "AB" });
}

TEST_CASE(Folder_MonogramCollisionMovesToTheNextFreeLetter)
{
    ExpectMonograms({ "Arbeit", "Archiv", "Arbeitsamt" }, { "Ar", "Ac", "Ab" });
    ExpectMonograms({ "Online Banking", "Online Brokerage" }, { "OB", "On" });
}

TEST_CASE(Folder_MonogramStaysDoubleWhenNoLetterIsLeft)
{
    ExpectMonograms({ "Aa", "AA" }, { "Aa", "Aa" });
}

TEST_CASE(Folder_MonogramKeepsUmlautsWhole)
{
    ExpectMonograms({ "\xC3\xBC" "bersee", "\xC3\x96" "ffentlich Rechtlich" },
                    { "\xC3\x9C" "b", "\xC3\x96" "R" });
    ExpectMonograms({ "\xC3\x9F" "x" }, { "\xC3\x9F" "x" });
}

TEST_CASE(Folder_MonogramOfPunctuationIsItsFirstCharacter)
{
    ExpectMonograms({ "#", "!!" }, { "#", "!" });
}

namespace
{
    FolderSurvey SurveyOf(const std::vector<std::string>& names)
    {
        FolderSurvey s;
        for (const std::string& n : names)
            s.Folders.push_back(FolderTally{ n, FoldFolderName(n), 1 });
        return s;
    }
}

TEST_CASE(Folder_MatchPutsPrefixesBeforeInnerHits)
{
    const FolderSurvey s = SurveyOf({ "Arbeit", "Garten", "Online Banking", "Karte" });
    const std::vector<std::size_t> hits = MatchFolders(s, "ar");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    if (hits.size() == 3)
    {
        CHECK_EQ(hits[0], std::size_t{ 0 });
        CHECK_EQ(hits[1], std::size_t{ 1 });
        CHECK_EQ(hits[2], std::size_t{ 3 });
    }
}

TEST_CASE(Folder_MatchIgnoresCaseUmlautsAndSurroundingSpace)
{
    const FolderSurvey s = SurveyOf({ "B\xC3\xBC" "ro", "Spiele" });
    const std::vector<std::size_t> hits = MatchFolders(s, "  BUERO ");
    CHECK_EQ(hits.size(), std::size_t{ 1 });
    if (!hits.empty())
        CHECK_EQ(hits[0], std::size_t{ 0 });
}

TEST_CASE(Folder_AnEmptyMatchKeepsEveryFolderInOrder)
{
    const FolderSurvey s = SurveyOf({ "A", "B", "C" });
    const std::vector<std::size_t> hits = MatchFolders(s, "");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    for (std::size_t i = 0; i < hits.size(); ++i)
        CHECK_EQ(hits[i], i);
    CHECK(MatchFolders(s, "zz").empty());
}
