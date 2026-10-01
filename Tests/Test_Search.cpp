#include "TestRunner.h"

#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Search.h"

#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
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

    std::uint64_t Add(Document& doc, const std::string& title,
                      const std::string& platform = "",
                      std::vector<Field> fields = {})
    {
        Entry e;
        e.Title = title;
        e.PlatformId = platform;
        e.Fields = std::move(fields);
        const auto id = AddEntry(doc, e, 1000);
        return id.IsOk() ? id.Value() : 0;
    }

    bool Has(const std::vector<SearchHit>& hits, std::uint64_t id)
    {
        for (const SearchHit& h : hits)
            if (h.EntryId == id)
                return true;
        return false;
    }
}

TEST_CASE(Search_NeverMatchesASecretValue)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Steam", "steam",
                                 { SecretField("Password", "correcthorsebatterystaple") });

    CHECK_MSG(Search(doc, "correcthorse").empty(),
              "a secret field value was searched");
    CHECK(Search(doc, "batterystaple").empty());

    const auto byName = Search(doc, "password");
    CHECK(Has(byName, id));
    CHECK(byName[0].Where == MatchKind::FieldName);
}

TEST_CASE(Search_MatchesNonSecretValues)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Steam", "steam",
                                 { Plain("Benutzer", "peter.mueller"),
                                   SecretField("Password", "hunter2") });

    const auto hits = Search(doc, "peter.mueller");
    CHECK(Has(hits, id));
    CHECK(hits[0].Where == MatchKind::FieldValue);
}

TEST_CASE(Search_FoldsUmlautsInBothDirections)
{
    Document doc;
    const std::uint64_t umlaut = Add(doc, "M\xC3\xBCller");
    const std::uint64_t spelled = Add(doc, "Mueller Zweitkonto");

    CHECK(Has(Search(doc, "M\xC3\xBCller"), spelled));
    CHECK(Has(Search(doc, "mueller"), umlaut));
    CHECK(FoldForSearch("Stra\xC3\x9F" "e") == "strasse");
    CHECK(FoldForSearch("STRASSE") == "strasse");
}

TEST_CASE(Search_IsCaseInsensitive)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Ubisoft Connect");
    CHECK(Has(Search(doc, "UBISOFT"), id));
    CHECK(Has(Search(doc, "ubisoft"), id));
    CHECK(Has(Search(doc, "UbIsOfT cOnNeCt"), id));
}

TEST_CASE(Search_FoldingLeavesOtherScriptsIntact)
{
    const std::string cyrillic = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    CHECK(FoldForSearch(cyrillic) == cyrillic);

    const std::string emoji = "\xF0\x9F\x94\x92";
    CHECK(FoldForSearch(emoji) == emoji);

    const std::string truncated = "abc\xC3";
    CHECK(FoldForSearch(truncated).size() == 4);

    CHECK(FoldForSearch("\xC3\x97") == "\xC3\x97");
    CHECK(FoldForSearch("\xC3\xB7") == "\xC3\xB7");
}

TEST_CASE(Search_FoldingPinsEveryLatin1Letter)
{
    const char* expected[64] = {
        "a", "a", "a", "a", "ae", "a", "ae", "c",
        "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "oe", "\xC3\x97",
        "o", "u", "u", "u", "ue", "y", "th", "ss",
        "a", "a", "a", "a", "ae", "a", "ae", "c",
        "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "oe", "\xC3\xB7",
        "o", "u", "u", "u", "ue", "y", "th", "y",
    };
    std::size_t wrong = 0;
    for (int i = 0; i < 64; ++i)
    {
        const char letter[3] = { '\xC3', static_cast<char>(0x80 + i), 0 };
        if (FoldForSearch(letter) != expected[i])
            ++wrong;
        const std::string framed = std::string("X") + letter + "Y";
        if (FoldForSearch(framed) != std::string("x") + expected[i] + "y")
            ++wrong;
    }
    CHECK_MSG(wrong == 0, std::to_string(wrong) + " Latin-1 letters fold wrongly");

    CHECK(FoldForSearch("\xC3\x7F") == "\xC3\x7F");
    CHECK(FoldForSearch("\xC3\xC0") == "\xC3\xC0");
}

TEST_CASE(Search_RanksTitleAboveEverythingElse)
{
    Document doc;
    const std::uint64_t byTitle = Add(doc, "Rockstar Games");
    const std::uint64_t byPlatform = Add(doc, "Zweitkonto", "rockstar");
    const std::uint64_t byFieldName = Add(doc, "Sonstiges", "",
                                          { Plain("Rockstar-Kennung", "abc") });

    const auto hits = Search(doc, "rockstar");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    CHECK(hits[0].EntryId == byTitle);
    CHECK(hits[1].EntryId == byPlatform);
    CHECK(hits[2].EntryId == byFieldName);
    CHECK(hits[0].Where == MatchKind::Title);
}

TEST_CASE(Search_PrefersAnExactTitleThenAPrefix)
{
    Document doc;
    const std::uint64_t contains = Add(doc, "Mein Steam");
    const std::uint64_t prefix = Add(doc, "Steam Zweitkonto");
    const std::uint64_t exact = Add(doc, "Steam");

    const auto hits = Search(doc, "steam");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    CHECK(hits[0].EntryId == exact);
    CHECK(hits[1].EntryId == prefix);
    CHECK(hits[2].EntryId == contains);
}

TEST_CASE(Search_OrderingIsTotalSoTheListNeverReshuffles)
{
    Document doc;
    std::vector<std::uint64_t> exact;
    std::vector<std::uint64_t> prefix;
    for (int i = 0; i < 100; ++i)
    {
        exact.push_back(Add(doc, "Steam"));
        prefix.push_back(Add(doc, "Steam Zweit"));
    }

    std::vector<std::uint64_t> expected = exact;
    expected.insert(expected.end(), prefix.begin(), prefix.end());

    for (int run = 0; run < 3; ++run)
    {
        const auto hits = Search(doc, "steam");
        CHECK_EQ(hits.size(), expected.size());
        for (std::size_t i = 0; i < hits.size() && i < expected.size(); ++i)
            CHECK_MSG(hits[i].EntryId == expected[i],
                      "position " + std::to_string(i) + " has id " +
                      std::to_string(hits[i].EntryId) + " instead of " +
                      std::to_string(expected[i]));
    }
}

TEST_CASE(Search_EmptyQueryReturnsEverythingByTitle)
{
    Document doc;
    Add(doc, "Ubisoft");
    Add(doc, "Google");
    Add(doc, "Steam");

    const auto hits = Search(doc, "");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    CHECK(FindEntry(doc, hits[0].EntryId)->Title == "Google");
    CHECK(FindEntry(doc, hits[1].EntryId)->Title == "Steam");
    CHECK(FindEntry(doc, hits[2].EntryId)->Title == "Ubisoft");

    CHECK(Search(doc, "   ").empty());
}

TEST_CASE(Search_MissAndEmptyVaultAreQuietNotSurprising)
{
    Document empty;
    CHECK(Search(empty, "irgendwas").empty());
    CHECK(Search(empty, "").empty());

    Document doc;
    Add(doc, "Steam");
    CHECK(Search(doc, "gibtsnicht").empty());

    Entry lonely;
    lonely.Title = "Steam";
    const SearchHit miss = ScoreEntry(lonely, "gibtsnicht");
    CHECK_EQ(miss.Score, 0);
    CHECK(miss.Where == MatchKind::None);
}

TEST_CASE(Search_HitsResolveThroughIdsNotPointers)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Steam");
    const auto hits = Search(doc, "steam");
    CHECK_EQ(hits.size(), std::size_t{ 1 });

    for (int i = 0; i < 200; ++i)
        Add(doc, "Fueller " + std::to_string(i));

    const Entry* still = FindEntry(doc, hits[0].EntryId);
    CHECK(still != nullptr);
    CHECK(still->Title == "Steam");

    CHECK(DeleteEntry(doc, id).IsOk());
    CHECK(FindEntry(doc, hits[0].EntryId) == nullptr);
}

TEST_CASE(Search_NeverMatchesATotpSecretOrARecoveryCode)
{
    Document doc;
    Entry e;
    e.Title = "Bank";
    e.HasTotp = true;
    e.Totp.Config.SecretBase32 = "MZXW6YTBOIQWY3DPEHPK3PXP";
    e.Totp.Config.Issuer = "Beispielbank";
    e.Totp.Config.AccountName = "kunde@example.de";
    e.RecoveryCodes.push_back(Vault::RecoveryCode{ "zzzz-wiederherstellung", 0, {} });
    const auto id = AddEntry(doc, e, 1000);
    CHECK(id.IsOk());

    CHECK_MSG(Search(doc, "MZXW6YTBOIQWY3DP").empty(),
              "the TOTP secret was searched");
    CHECK_MSG(Search(doc, "zzzz-wiederherstellung").empty(),
              "a recovery code was searched");

    CHECK(Search(doc, "Beispielbank").empty());
    CHECK(Search(doc, "kunde@example.de").empty());

    CHECK_EQ(Search(doc, "Bank").size(), std::size_t{ 1 });
}

TEST_CASE(Search_FindsAListDescriptionButNeverItsValue)
{
    Document doc;
    Entry e;
    e.Title = "Spielekonto";
    ListItem key;
    key.Kind = kListKindProductKey;
    key.Label = "Half-Life 2";
    key.Secret = "QQQQQ-WWWWW-EEEEE";
    e.ListItems.push_back(std::move(key));
    ListItem future;
    future.Kind = 77;
    future.Label = "From the future";
    future.Secret = "FUTURE-HIDDEN-9";
    e.ListItems.push_back(std::move(future));
    CHECK(AddEntry(doc, std::move(e), 1000).IsOk());

    const auto hits = Search(doc, "half-life");
    CHECK_EQ(hits.size(), std::size_t{ 1 });
    if (hits.size() == 1)
        CHECK(hits[0].Where == MatchKind::ListLabel);
    CHECK_EQ(Search(doc, "future").size(), std::size_t{ 1 });

    CHECK_MSG(Search(doc, "QQQQQ-WWWWW").empty(), "a list value was searched");
    CHECK_MSG(Search(doc, "HIDDEN").empty(),
              "the value of an unknown list kind was searched");
}

TEST_CASE(Search_FindsTheFolderName)
{
    Document doc;
    Entry e;
    e.Title = "Irgendein Account";
    e.Folder = "Arbeit";
    const auto id = AddEntry(doc, e, 1000);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;

    const std::vector<SearchHit> hits = Search(doc, "arbeit");
    CHECK_EQ(hits.size(), std::size_t{ 1 });
    if (hits.empty())
        return;
    CHECK(hits[0].EntryId == id.Value());
    CHECK_MSG(hits[0].Where == MatchKind::Folder,
              "the match was attributed to another place");
}

TEST_CASE(Search_AFolderHitRanksBelowATitleAndAboveAFieldName)
{
    Document doc;
    Entry titled;
    titled.Title = "Arbeit";
    const auto byTitle = AddEntry(doc, titled, 1000);

    Entry foldered;
    foldered.Title = "Zzz Account";
    foldered.Folder = "Arbeit";
    const auto byFolder = AddEntry(doc, foldered, 1000);

    Entry named;
    named.Title = "Yyy Account";
    named.Fields.push_back(Plain("Arbeit", "egal"));
    const auto byField = AddEntry(doc, named, 1000);

    CHECK(byTitle.IsOk() && byFolder.IsOk() && byField.IsOk());
    if (!byTitle.IsOk() || !byFolder.IsOk() || !byField.IsOk())
        return;

    const std::vector<SearchHit> hits = Search(doc, "arbeit");
    CHECK_EQ(hits.size(), std::size_t{ 3 });
    if (hits.size() != 3)
        return;
    CHECK(hits[0].EntryId == byTitle.Value());
    CHECK(hits[1].EntryId == byFolder.Value());
    CHECK(hits[2].EntryId == byField.Value());
}

TEST_CASE(Search_TheFolderIsNotSearchedWhenItIsEmpty)
{
    Document doc;
    Entry e;
    e.Title = "Account";
    CHECK(AddEntry(doc, e, 1000).IsOk());

    CHECK_MSG(Search(doc, "arbeit").empty(),
              "an entry without a folder was found through the folder");
}

namespace
{
    std::uint64_t AddAt(Document& doc, const std::string& title, std::uint64_t when,
                        std::vector<Field> fields = {})
    {
        Entry e;
        e.Title = title;
        e.Fields = std::move(fields);
        const auto id = AddEntry(doc, e, when);
        return id.IsOk() ? id.Value() : 0;
    }
}

TEST_CASE(Search_OrderByNameIsTheDefault)
{
    Document doc;
    const std::uint64_t c = AddAt(doc, "Cheat Engine", 3000);
    const std::uint64_t a = AddAt(doc, "Amazon", 1000);
    const std::uint64_t b = AddAt(doc, "battle.net", 2000);
    const auto hits = Search(doc, "");
    CHECK(hits.size() == 3);
    if (hits.size() != 3)
        return;
    CHECK_EQ(hits[0].EntryId, a);
    CHECK_EQ(hits[1].EntryId, b);
    CHECK_EQ(hits[2].EntryId, c);
    CHECK(Search(doc, "", SortOrder::Title).size() == 3);
}

TEST_CASE(Search_OrderByModifiedPutsTheNewestFirst)
{
    Document doc;
    const std::uint64_t old = AddAt(doc, "Amazon", 1000);
    const std::uint64_t mid = AddAt(doc, "Zalando", 2000);
    const std::uint64_t young = AddAt(doc, "Mastodon", 3000);
    const std::uint64_t twin = AddAt(doc, "Bahn", 2000);
    const auto hits = Search(doc, "", SortOrder::Modified);
    CHECK(hits.size() == 4);
    if (hits.size() != 4)
        return;
    CHECK_EQ(hits[0].EntryId, young);
    CHECK_EQ(hits[1].EntryId, twin);
    CHECK_EQ(hits[2].EntryId, mid);
    CHECK_EQ(hits[3].EntryId, old);
}

TEST_CASE(Search_ABetterHitStaysAheadWhateverTheOrder)
{
    Document doc;
    const std::uint64_t title = AddAt(doc, "Steam", 1000);
    const std::uint64_t field = AddAt(doc, "Notizen", 5000, { Plain("Hinweis", "steam guard") });
    const auto hits = Search(doc, "stea", SortOrder::Modified);
    CHECK(hits.size() == 2);
    if (hits.size() != 2)
        return;
    CHECK_EQ(hits[0].EntryId, title);
    CHECK_EQ(hits[1].EntryId, field);
}

TEST_CASE(Search_AnUnknownStoredOrderIsName)
{
    CHECK(SortOrderFrom(0) == SortOrder::Title);
    CHECK(SortOrderFrom(1) == SortOrder::Modified);
    CHECK(SortOrderFrom(2) == SortOrder::Title);
    CHECK(SortOrderFrom(0xFFFFFFFFu) == SortOrder::Title);
}

TEST_CASE(Search_FindFoldedMatchNamesTheOriginalBytes)
{
    std::size_t b = 0, e = 0;
    CHECK(FindFoldedMatch("Steam", "tea", b, e));
    CHECK(b == 1 && e == 4);
    CHECK(FindFoldedMatch("STEAM", "st", b, e));
    CHECK(b == 0 && e == 2);

    const std::string mueller = "M\xc3\xbcller";
    CHECK(FindFoldedMatch(mueller, "ue", b, e));
    CHECK_MSG(b == 1 && e == 3, "the match was not on the umlaut");

    const std::string baer = "B\xc3\xa4r";
    CHECK(FindFoldedMatch(baer, "e", b, e));
    CHECK(b == 1 && e == 3);

    CHECK(!FindFoldedMatch("Steam", "xyz", b, e));
    CHECK(!FindFoldedMatch("Steam", "", b, e));
}

TEST_CASE(Search_FindLikelyDuplicateNeedsPlaceAndLogin)
{
    Document doc;
    Field user;
    user.Role = FieldRole::Username;
    user.Key = "Benutzername";
    user.Value = std::string("Alex.Doe");
    const std::uint64_t steam = Add(doc, "Steam", "steam", {});
    {
        Entry* e = FindEntry(doc, steam);
        CHECK(e != nullptr);
        if (!e)
            return;
        e->Fields.push_back(std::move(user));
    }

    CHECK_EQ(FindLikelyDuplicate(doc, 0, "Steam", "Mein Steam", "alex.doe"), steam);
    CHECK_EQ(FindLikelyDuplicate(doc, steam, "steam", "", "alex.doe"), std::uint64_t{ 0 });
    CHECK_EQ(FindLikelyDuplicate(doc, 0, "steam", "", "zweitkonto"), std::uint64_t{ 0 });
    CHECK_EQ(FindLikelyDuplicate(doc, 0, "epic-games", "", "alex.doe"), std::uint64_t{ 0 });
    CHECK_EQ(FindLikelyDuplicate(doc, 0, "steam", "", ""), std::uint64_t{ 0 });

    Field mail;
    mail.Role = FieldRole::Email;
    mail.Key = "E-Mail";
    mail.Value = std::string("alex@example.com");
    const std::uint64_t forum = Add(doc, "Forum", "", {});
    {
        Entry* e = FindEntry(doc, forum);
        CHECK(e != nullptr);
        if (!e)
            return;
        e->Fields.push_back(std::move(mail));
    }
    CHECK_EQ(FindLikelyDuplicate(doc, 0, "", "forum", "ALEX@example.com"), forum);
    CHECK_EQ(FindLikelyDuplicate(doc, 0, "", "Anderes Forum", "alex@example.com"),
             std::uint64_t{ 0 });
}
