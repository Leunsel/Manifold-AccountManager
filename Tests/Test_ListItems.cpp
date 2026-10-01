#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "HeapWatch.h"

#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/ListKind.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Secret.h"
#include "Core/VaultFile.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;
using Test::HeapCopiesDuring;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;

    ListItem Item(std::uint32_t kind, std::string_view label, std::string_view secret,
                  std::uint64_t added = 0)
    {
        ListItem item;
        item.Kind = kind;
        item.Label = std::string(label);
        item.Secret = secret;
        item.AddedUtc = added;
        return item;
    }

    Entry SampleEntry()
    {
        Entry e;
        e.Title = "Steam";
        e.PlatformId = "steam";
        return e;
    }

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

    const char kMaster[] = "a fairly long master password";
}

TEST_CASE(ListItems_ComeBackFromTheFileWhole)
{
    CHECK(Crypto::Initialise().IsOk());
    MemoryFileSystem fs;
    const char path[] = "listen.vault";
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, kMaster, std::strlen(kMaster),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        Entry e = SampleEntry();
        e.ListItems.push_back(Item(kListKindProductKey, "Half-Life 2", "AAAAA-BBBBB-CCCCC", kNow));
        e.ListItems.push_back(Item(kListKindSecurityQuestion, "Name of your first pet?",
                                   "Ein erfundenes Wort"));
        e.ListItems.push_back(Item(kListKindOther, "", "only a value", kNow + 1));
        CHECK(AddEntry(v.Doc(), std::move(e), kNow).IsOk());
        CHECK(v.Save(fs, path).IsOk());
    }

    VaultFile out;
    CHECK(VaultFile::Unlock(fs, path, kMaster, std::strlen(kMaster), out).IsOk());
    CHECK_EQ(out.Doc().Entries.size(), std::size_t{ 1 });
    if (out.Doc().Entries.size() != 1)
        return;
    const std::vector<ListItem>& items = out.Doc().Entries[0].ListItems;
    CHECK_EQ(items.size(), std::size_t{ 3 });
    if (items.size() != 3)
        return;

    CHECK_EQ(items[0].Kind, kListKindProductKey);
    CHECK(items[0].Label == "Half-Life 2");
    CHECK(items[0].Secret == "AAAAA-BBBBB-CCCCC");
    CHECK_EQ(items[0].AddedUtc, kNow);

    CHECK_EQ(items[1].Kind, kListKindSecurityQuestion);
    CHECK(items[1].Label == "Name of your first pet?");
    CHECK(items[1].Secret == "Ein erfundenes Wort");
    CHECK_EQ(items[1].AddedUtc, std::uint64_t{ 0 });

    CHECK_EQ(items[2].Kind, kListKindOther);
    CHECK(items[2].Label.empty());
    CHECK(items[2].Secret == "only a value");

    for (const ListItem& item : items)
        CHECK(item.Secret.InArena());
}

TEST_CASE(ListItems_AnUnknownKindAndRecordSurvive)
{
    CHECK(Crypto::Initialise().IsOk());
    MemoryFileSystem fs;
    const char path[] = "zukunft.vault";
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, kMaster, std::strlen(kMaster),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        Entry e = SampleEntry();
        ListItem item = Item(77, "from the future", "wert-77");
        item.Unknown.push_back(UnknownRecord{ 650, { 1, 2, 3, 4 } });
        e.ListItems.push_back(std::move(item));
        CHECK(AddEntry(v.Doc(), std::move(e), kNow).IsOk());
        CHECK(v.Save(fs, path).IsOk());
    }
    VaultFile out;
    CHECK(VaultFile::Unlock(fs, path, kMaster, std::strlen(kMaster), out).IsOk());
    const bool one = out.Doc().Entries.size() == 1 &&
                     out.Doc().Entries[0].ListItems.size() == 1;
    CHECK(one);
    if (!one)
        return;
    const ListItem& item = out.Doc().Entries[0].ListItems[0];
    CHECK_EQ(item.Kind, std::uint32_t{ 77 });
    CHECK(item.Secret == "wert-77");
    CHECK_EQ(item.Unknown.size(), std::size_t{ 1 });
    if (item.Unknown.size() == 1)
    {
        CHECK_EQ(item.Unknown[0].Tag, std::uint16_t{ 650 });
        CHECK(item.Unknown[0].Payload == std::vector<unsigned char>({ 1, 2, 3, 4 }));
    }
}

TEST_CASE(ListItems_ALostValueFailsTheOpen)
{
    CHECK(Crypto::Initialise().IsOk());
    MemoryFileSystem fs;
    const char path[] = "verlust.vault";
    const std::string_view key = "KEY-with-twenty-char";
    {
        VaultFile v;
        CHECK(VaultFile::Create(fs, path, kMaster, std::strlen(kMaster),
                                Crypto::KdfParams::TestOnlyMinimum(), v).IsOk());
        Entry e = SampleEntry();
        e.ListItems.push_back(Item(kListKindProductKey, "Spiel", key));
        CHECK(AddEntry(v.Doc(), std::move(e), kNow).IsOk());
        CHECK(v.Save(fs, path).IsOk());
    }
    {
        std::vector<SecureString> filler;
        SecureArena& arena = SecureArena::Instance();
        const ChunkLimitForTest noNewChunks(arena.ChunkCount());
        const NoHeapFallbackForTest noHeap;
        FillUntilLoss(filler, key.size());

        VaultFile out;
        const Status st = VaultFile::Unlock(fs, path, kMaster, std::strlen(kMaster), out);
        CHECK(!st.IsOk());
        CHECK(st.Code() == ErrorCode::OutOfMemory);
    }
    VaultFile out;
    CHECK(VaultFile::Unlock(fs, path, kMaster, std::strlen(kMaster), out).IsOk());
}

TEST_CASE(ListItems_AddEntryKeepsThemUpdateEntryDoesNot)
{
    Document doc;
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindProductKey, "Portal", "PPPPP-QQQQQ"));
    const auto id = AddEntry(doc, std::move(e), kNow);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;
    CHECK_EQ(FindEntry(doc, id.Value())->ListItems.size(), std::size_t{ 1 });

    Entry replacement = SampleEntry();
    replacement.Title = "Steam, umbenannt";
    CHECK(UpdateEntry(doc, id.Value(), std::move(replacement), kNow + 5).IsOk());
    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored && stored->Title == "Steam, umbenannt");
    CHECK_MSG(stored && stored->ListItems.size() == 1 &&
                  stored->ListItems[0].Secret == "PPPPP-QQQQQ",
              "UpdateEntry overwrote the lists");
}

TEST_CASE(ListItems_SetReplacesStampsAndWipesTheOld)
{
    Document doc;
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindProductKey,
                               "An old title, long enough for the heap", "ALT-ALT-ALT"));
    const auto id = AddEntry(doc, std::move(e), kNow);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;

    std::vector<ListItem> fresh;
    fresh.push_back(Item(kListKindProductKey, "Neu", "NEU-NEU-NEU"));
    fresh.push_back(Item(kListKindOrderNumber, "Bestellung", "ORD-12345"));

    Status st;
    const std::size_t hits = HeapCopiesDuring({ "An old title, long enough for the heap" }, [&] {
        st = SetListItems(doc, id.Value(), std::move(fresh), kNow + 10);
    });
    CHECK(st.IsOk());
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the old label");

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored && stored->ListItems.size() == 2);
    CHECK(stored && stored->ModifiedUtc == kNow + 10);
    if (stored && stored->ListItems.size() == 2)
    {
        CHECK(stored->ListItems[0].Secret == "NEU-NEU-NEU");
        CHECK_EQ(stored->ListItems[1].Kind, kListKindOrderNumber);
    }
}

TEST_CASE(ListItems_SetWithNothingChangedKeepsTheStamp)
{
    Document doc;
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindGiftCode, "Guthaben 20", "GIFT-0001", kNow));
    const auto id = AddEntry(doc, std::move(e), kNow);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;

    std::vector<ListItem> same;
    CHECK(CopyListItems(FindEntry(doc, id.Value())->ListItems, same));
    CHECK(SetListItems(doc, id.Value(), std::move(same), kNow + 99).IsOk());
    CHECK_EQ(FindEntry(doc, id.Value())->ModifiedUtc, kNow);
}

TEST_CASE(ListItems_ARefusedSetChangesNothing)
{
    Document doc;
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindProductKey, "Bleibt", "BLEIBT-1"));
    const auto id = AddEntry(doc, std::move(e), kNow);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;

    std::vector<ListItem> bad;
    bad.push_back(Item(kListKindProductKey, "Neu", "NEU-1"));
    bad.push_back(Item(kListKindProductKey, "Without value", ""));
    CHECK(!SetListItems(doc, id.Value(), std::move(bad), kNow + 1).IsOk());

    const Entry* stored = FindEntry(doc, id.Value());
    CHECK(stored && stored->ListItems.size() == 1 && stored->ListItems[0].Secret == "BLEIBT-1");
    CHECK(stored && stored->ModifiedUtc == kNow);
}

TEST_CASE(ListItems_TheLimitsHold)
{
    auto valid = [](std::vector<ListItem> items) { return ValidateListItems(items).IsOk(); };

    CHECK(valid({ Item(kListKindProductKey, "ok", "WERT") }));
    CHECK(valid({ Item(kListKindProductKey, std::string(kMaxListLabelBytes, 'b'), "WERT") }));
    CHECK(!valid({ Item(kListKindProductKey, std::string(kMaxListLabelBytes + 1, 'b'), "WERT") }));
    CHECK(valid({ Item(kListKindProductKey, "ok", std::string(kMaxListSecretBytes, 'w')) }));
    CHECK(!valid({ Item(kListKindProductKey, "ok", std::string(kMaxListSecretBytes + 1, 'w')) }));
    CHECK(!valid({ Item(kListKindProductKey, "ok", "") }));

    CHECK(!valid({ Item(kListKindProductKey, "with\tTab", "WERT") }));
    CHECK(!valid({ Item(kListKindProductKey, "ok", "WE\tRT") }));
    CHECK(!valid({ Item(kListKindProductKey, "ok", "WE\nRT") }));
    CHECK(!valid({ Item(kListKindProductKey, "ok", std::string("W\x7F")) }));

    ListItem carried = Item(kListKindProductKey, "from the future", "");
    carried.Unknown.push_back(UnknownRecord{ 604, { 9 } });
    CHECK(valid({ carried }));

    std::vector<ListItem> many(kMaxListItemsPerEntry, Item(kListKindProductKey, "x", "K"));
    CHECK(ValidateListItems(many).IsOk());
    many.push_back(Item(kListKindProductKey, "x", "K"));
    CHECK(!ValidateListItems(many).IsOk());
}

TEST_CASE(ListItems_NormaliseTrimsLabelsNeverValues)
{
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindProductKey, "  Half-Life  ", "  KEY with margin  "));
    e.ListItems.push_back(Item(kListKindProductKey, "", ""));
    Normalise(e);
    CHECK_EQ(e.ListItems.size(), std::size_t{ 1 });
    if (e.ListItems.size() != 1)
        return;
    CHECK(e.ListItems[0].Label == "Half-Life");
    CHECK(e.ListItems[0].Secret == "  KEY with margin  ");
}

TEST_CASE(ListItems_DropWipesAndClears)
{
    Entry e = SampleEntry();
    e.ListItems.push_back(Item(kListKindProductKey,
                               "A description, long enough for the heap", "DROP-DROP"));
    const std::size_t hits = HeapCopiesDuring({ "A description, long enough for the heap" }, [&] {
        DropListItems(e);
    });
    CHECK(e.ListItems.empty());
    CHECK_MSG(hits == 0, std::to_string(hits) + " heap blocks held the label");
}

TEST_CASE(ListItems_CopyNoticesALossAndSameSeesEveryPart)
{
    std::vector<ListItem> from;
    from.push_back(Item(kListKindProductKey, "a", "EINS", kNow));
    from.push_back(Item(kListKindSecurityQuestion, "b", std::string(9000, 'v')));

    std::vector<ListItem> out;
    out.push_back(Item(kListKindOther, "unberuehrt", "X"));
    {
        const NoHeapFallbackForTest noHeap;
        CHECK(!CopyListItems(from, out));
    }
    CHECK(out.size() == 1 && out[0].Label == "unberuehrt");

    CHECK(CopyListItems(from, out));
    CHECK(SameListItems(from, out));

    auto differs = [&](auto change) {
        std::vector<ListItem> copy;
        if (!CopyListItems(from, copy))
            return false;
        change(copy);
        return !SameListItems(from, copy);
    };
    CHECK(differs([](std::vector<ListItem>& c) { c[0].Kind = kListKindGiftCode; }));
    CHECK(differs([](std::vector<ListItem>& c) { c[0].Label = "other"; }));
    CHECK(differs([](std::vector<ListItem>& c) { c[0].Secret = "ZWEI"; }));
    CHECK(differs([](std::vector<ListItem>& c) { c[0].AddedUtc = 1; }));
    CHECK(differs([](std::vector<ListItem>& c) { c[0].Unknown.push_back(UnknownRecord{ 650, { 1 } }); }));
    CHECK(differs([](std::vector<ListItem>& c) { c.pop_back(); }));
}

TEST_CASE(ListKind_EveryKindHasTextsAndACsvNameThatComesBack)
{
    for (std::size_t i = 0; i < kListKindCount; ++i)
    {
        const std::uint32_t kind = kListKindOrder[i];
        CHECK(IsKnownListKind(kind));
        const ListKindTexts& t = ListKindTextsFor(kind);
        CHECK(t.CsvName && *t.CsvName);
        char scratch[12];
        std::uint32_t back = 0xFFFFFFFFu;
        CHECK(ListKindFromCsvName(ListKindCsvName(kind, scratch), back));
        CHECK_EQ(back, kind);

        for (std::size_t k = i + 1; k < kListKindCount; ++k)
            CHECK(std::string_view(t.CsvName) !=
                  std::string_view(ListKindTextsFor(kListKindOrder[k]).CsvName));
    }

    CHECK(!IsKnownListKind(77));
    CHECK(std::string_view(ListKindTextsFor(77).CsvName) == "other");
    char scratch[12];
    CHECK(ListKindCsvName(77, scratch) == "77");
    CHECK(ListKindCsvName(4294967295u, scratch) == "4294967295");
    std::uint32_t back = 0;
    CHECK(ListKindFromCsvName("77", back) && back == 77);
    CHECK(!ListKindFromCsvName("4294967296", back));
    CHECK(!ListKindFromCsvName("", back));
    CHECK(!ListKindFromCsvName("steam_key", back));
    CHECK(!ListKindFromCsvName("7a", back));
}

TEST_CASE(ListKind_PlatformsSuggestWhatTheyAreFor)
{
    auto offers = [](const char* platform, std::uint32_t kind) {
        const ListKindOffer o = ListKindsFor(platform);
        for (std::size_t i = 0; i < o.Count; ++i)
            if (o.Kinds[i] == kind)
                return true;
        return false;
    };
    CHECK(offers("steam", kListKindProductKey));
    CHECK(offers(" Steam ", kListKindProductKey));
    CHECK(offers("SQUARE-STEAM", kListKindProductKey));
    CHECK(offers("steam", kListKindOrderNumber));
    CHECK(offers("gog", kListKindProductKey));
    CHECK(offers("epic-games", kListKindProductKey));
    CHECK(offers("ubisoft", kListKindProductKey));
    CHECK(offers("ea", kListKindProductKey));
    CHECK(offers("microsoft", kListKindSecurityQuestion));
    CHECK(offers("xbox", kListKindProductKey));
    CHECK(offers("windows", kListKindProductKey));

    CHECK_EQ(ListKindsFor("").Count, std::size_t{ 0 });
    CHECK_EQ(ListKindsFor(nullptr).Count, std::size_t{ 0 });
    CHECK_EQ(ListKindsFor("gibtsnicht").Count, std::size_t{ 0 });
    CHECK_EQ(ListKindsFor("steamx").Count, std::size_t{ 0 });
    CHECK_EQ(ListKindsFor("stea").Count, std::size_t{ 0 });
}
