#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/VaultFormat.h"

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;
    constexpr std::uint64_t kLater = 1'700'009'999;
    constexpr std::uint64_t kLatest = 1'700'099'999;

    std::uint64_t Add(Document& doc, const char* title)
    {
        Entry e;
        e.Title = title;
        const auto id = AddEntry(doc, e, kNow);
        CHECK(id.IsOk());
        return id.IsOk() ? id.Value() : 0;
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

    void BeginHandwrittenEntry(Writer& w)
    {
        w.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        w.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        w.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Account");
    }
}

TEST_CASE(Favourite_SurvivesTheFile)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetFavourite(doc, id, true, kLater).IsOk());

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].Favourite);
}

TEST_CASE(Favourite_NotAFavouriteCostsNoRecord)
{
    Document doc;
    Add(doc, "Account");

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK_MSG(CountTagInFirstEntry(w, Tag::EntryFavourite) == 0,
              "a non-favourite wrote a record");
}

TEST_CASE(Favourite_ARecordWithTheWrongShapeIsRefused)
{
    Body w([&](Writer& out) {
        BeginHandwrittenEntry(out);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryFavourite), "ja");
        out.EndRecord();
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a favourite record with the wrong length was accepted");
}

TEST_CASE(Accent_SurvivesTheFile)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x3FA9DDu, kLater).IsOk());

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK_EQ(back.Entries[0].Accent, std::uint32_t{ 0x3FA9DDu });
}

TEST_CASE(Accent_AutomaticCostsNoRecord)
{
    Document doc;
    Add(doc, "Account");

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK_MSG(CountTagInFirstEntry(w, Tag::EntryAccent) == 0,
              "an automatic colour wrote a record");
}

TEST_CASE(Accent_AHigherByteIsNotAColour)
{
    Body w([&](Writer& out) {
        BeginHandwrittenEntry(out);
        out.WriteU32(static_cast<std::uint16_t>(Tag::EntryAccent), 0xFF112233u);
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK_EQ(back.Entries[0].Accent, std::uint32_t{ 0x112233u });
}

TEST_CASE(Accent_ARecordWithTheWrongShapeIsRefused)
{
    Body w([&](Writer& out) {
        BeginHandwrittenEntry(out);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryAccent), "rot");
        out.EndRecord();
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a colour record with the wrong length was accepted");
}

TEST_CASE(Favourite_SettingItMovesTheStamp)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetFavourite(doc, id, true, kLater).IsOk());
    CHECK_EQ(StampOf(doc, id), kLater);
}

TEST_CASE(Favourite_SettingItTwiceLeavesTheStampAlone)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetFavourite(doc, id, true, kLater).IsOk());
    CHECK(SetFavourite(doc, id, true, kLatest).IsOk());
    CHECK_MSG(StampOf(doc, id) == kLater,
              "setting the same value again moved the timestamp");
}

TEST_CASE(Favourite_AMissingEntryFails)
{
    Document doc;
    Add(doc, "Account");
    CHECK(!SetFavourite(doc, 9999, true, kLater).IsOk());
}

TEST_CASE(Accent_SettingItMovesTheStamp)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());
    CHECK_EQ(StampOf(doc, id), kLater);
}

TEST_CASE(Accent_TheSameColourLeavesTheStampAlone)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());
    CHECK(SetAccent(doc, id, 0x112233u, kLatest).IsOk());
    CHECK_MSG(StampOf(doc, id) == kLater,
              "setting the same colour again moved the timestamp");
}

TEST_CASE(Accent_TheMutatorDropsTheHigherByteBeforeComparing)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());
    const Entry* e = FindEntry(doc, id);
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK_EQ(e->Accent, std::uint32_t{ 0x112233u });

    CHECK(SetAccent(doc, id, 0xFF112233u, kLatest).IsOk());
    CHECK_EQ(e->Accent, std::uint32_t{ 0x112233u });
    CHECK_MSG(StampOf(doc, id) == kLater,
              "the mutator masked only after comparing");
}

TEST_CASE(Accent_ZeroTakesTheColourBack)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());
    CHECK(SetAccent(doc, id, 0, kLatest).IsOk());
    const Entry* e = FindEntry(doc, id);
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK_EQ(e->Accent, std::uint32_t{ 0 });
    CHECK_EQ(e->ModifiedUtc, kLatest);
}

TEST_CASE(Accent_AMissingEntryFails)
{
    Document doc;
    Add(doc, "Account");
    CHECK(!SetAccent(doc, 9999, 0x112233u, kLater).IsOk());
}

TEST_CASE(Favourite_UpdateEntryCannotDropIt)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetFavourite(doc, id, true, kLater).IsOk());

    Entry rebuilt;
    rebuilt.Title = "Account, umbenannt";
    CHECK(UpdateEntry(doc, id, rebuilt, kLatest).IsOk());

    const Entry* e = FindEntry(doc, id);
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK_MSG(e->Favourite,
              "UpdateEntry took the star from the candidate");
}

TEST_CASE(Accent_UpdateEntryCannotDropIt)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());

    Entry rebuilt;
    rebuilt.Title = "Account, umbenannt";
    CHECK(UpdateEntry(doc, id, rebuilt, kLatest).IsOk());

    const Entry* e = FindEntry(doc, id);
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK_MSG(e->Accent == 0x112233u,
              "UpdateEntry took the colour from the candidate");
}

TEST_CASE(Favourite_ACopyKeepsTheStarAndTheColour)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetFavourite(doc, id, true, kLater).IsOk());
    CHECK(SetAccent(doc, id, 0x112233u, kLater).IsOk());

    const Entry* original = FindEntry(doc, id);
    CHECK(original != nullptr);
    if (!original)
        return;
    Entry copy = *original;
    copy.Title += " (Kopie)";
    const auto added = AddEntry(doc, std::move(copy), kLatest);
    CHECK(added.IsOk());
    if (!added.IsOk())
        return;

    const Entry* made = FindEntry(doc, added.Value());
    CHECK(made != nullptr);
    if (!made)
        return;
    CHECK(made->Favourite);
    CHECK_EQ(made->Accent, std::uint32_t{ 0x112233u });
}

TEST_CASE(SecondFactor_NothingSaidCostsNoRecord)
{
    Document doc;
    Add(doc, "Account");

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK_MSG(CountTagInFirstEntry(w, Tag::EntrySecondFactorState) == 0,
              "the default case wrote a record");
}

TEST_CASE(SecondFactor_SurvivesTheFile)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetSecondFactorState(doc, id, SecondFactorNotOffered, kLater).IsOk());

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK_EQ(back.Entries[0].SecondFactorState,
             std::uint32_t{ SecondFactorNotOffered });
}

TEST_CASE(SecondFactor_AValueFromALaterVersionSurvivesUnchanged)
{
    Body w([&](Writer& out) {
        BeginHandwrittenEntry(out);
        out.WriteU32(static_cast<std::uint16_t>(Tag::EntrySecondFactorState), 99);
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK_EQ(back.Entries[0].SecondFactorState, std::uint32_t{ 99 });

    Body again([&](Writer& out) {
        Encode(back, out);
    });
    Document twice;
    CHECK(Decode(again.data(), again.size(), twice));
    CHECK(!twice.Entries.empty());
    if (!twice.Entries.empty())
        CHECK_EQ(twice.Entries[0].SecondFactorState, std::uint32_t{ 99 });
}

TEST_CASE(SecondFactor_AnUnknownValueIsNotTreatedAsProtected)
{
    CHECK(!SecondFactorIsHandled(99));
    CHECK(!SecondFactorIsHandled(SecondFactorUnknown));
    CHECK(SecondFactorIsHandled(SecondFactorNotOffered));
    CHECK(SecondFactorIsHandled(SecondFactorElsewhere));
}

TEST_CASE(SecondFactor_ARecordWithTheWrongShapeIsRefused)
{
    Body w([&](Writer& out) {
        BeginHandwrittenEntry(out);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntrySecondFactorState), "ja");
        out.EndRecord();
    });

    Document back;
    CHECK_MSG(!Decode(w.data(), w.size(), back),
              "a record with the wrong length was accepted");
}

TEST_CASE(SecondFactor_TheSameValueLeavesTheStampAlone)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetSecondFactorState(doc, id, SecondFactorElsewhere, kLater).IsOk());
    CHECK(SetSecondFactorState(doc, id, SecondFactorElsewhere, kLatest).IsOk());
    CHECK_MSG(StampOf(doc, id) == kLater,
              "setting the same value again moved the timestamp");
}

TEST_CASE(SecondFactor_UpdateEntryCannotDropIt)
{
    Document doc;
    const std::uint64_t id = Add(doc, "Account");
    CHECK(SetSecondFactorState(doc, id, SecondFactorNotOffered, kLater).IsOk());

    Entry rebuilt;
    rebuilt.Title = "Account, umbenannt";
    CHECK(UpdateEntry(doc, id, rebuilt, kLatest).IsOk());

    const Entry* e = FindEntry(doc, id);
    CHECK(e != nullptr);
    if (!e)
        return;
    CHECK_MSG(e->SecondFactorState == SecondFactorNotOffered,
              "UpdateEntry took the state from the candidate");
}

TEST_CASE(SecondFactor_AMissingEntryFails)
{
    Document doc;
    Add(doc, "Account");
    CHECK(!SetSecondFactorState(doc, 9999, SecondFactorNotOffered, kLater).IsOk());
}
