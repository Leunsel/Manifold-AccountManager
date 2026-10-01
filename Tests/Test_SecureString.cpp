#include "TestRunner.h"

#include "ArenaTestUtil.h"
#include "Core/SecureString.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Bunker;

namespace
{
    const char* kLong = "CANARYVOGEL-SECURESTRING-0123456789-ABCDEFGHIJKLMNOP";

    std::size_t NonZero(const unsigned char* p, std::size_t n)
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (p[i] != 0)
                ++count;
        return count;
    }

    struct Range
    {
        const unsigned char* b;
        const unsigned char* e;
    };

    void AddSlot(std::vector<Range>& out, const SecureString& s)
    {
        if (s.InArena())
            out.push_back({ s.PayloadForTests(), s.PayloadForTests() + s.SlotBytesForTests() });
    }

    std::size_t CountOverlaps(std::vector<Range> r)
    {
        std::sort(r.begin(), r.end(), [](const Range& x, const Range& y) {
            return std::less<const unsigned char*>()(x.b, y.b);
        });
        std::size_t overlaps = 0;
        const unsigned char* reach = nullptr;
        for (const Range& x : r)
        {
            if (reach && x.b < reach)
                ++overlaps;
            if (!reach || x.e > reach)
                reach = x.e;
        }
        return overlaps;
    }

    SecureString Filled(std::size_t n, char c)
    {
        SecureString s;
        s = std::string(n, c);
        return s;
    }

    const std::size_t kLargeValue = 8000;
    const std::size_t kLargeSlotsPerChunk = SecureArena::kChunkBytes / 8448;
    const std::size_t kMoreThanTheBudget =
        SecureArena::kDefaultChunkLimit * kLargeSlotsPerChunk + 64;
}

TEST_CASE(Arena_AFreshSlotIsZeroBeyondThePayload)
{
    std::vector<SecureString> many;
    for (int i = 0; i < 6000; ++i)
    {
        SecureString v;
        v = "abc";
        many.push_back(std::move(v));
    }
    for (const SecureString& v : many)
    {
        const unsigned char* p = v.PayloadForTests();
        const std::size_t tail = v.SlotBytesForTests() - (v.Size() + 1);
        if (NonZero(p + v.Size() + 1, tail) != 0)
        {
            CHECK_MSG(false, "no NUL after the payload -- the "
                             "chunk was not zeroed");
            return;
        }
    }
}

TEST_CASE(Arena_FreedSlotIsZeroed)
{
    std::vector<std::unique_ptr<SecureString>> values;
    for (int i = 0; i < 200; ++i)
        values.push_back(std::make_unique<SecureString>(kLong));

    for (const auto& v : values)
        if (std::memcmp(v->PayloadForTests(), kLong, std::strlen(kLong)) != 0)
        {
            CHECK_MSG(false, "a live value is not in its slot -- "
                             "the case below measures nothing");
            return;
        }

    std::vector<std::pair<const unsigned char*, std::size_t>> freed;
    for (std::size_t i = 0; i < values.size(); i += 2)
    {
        freed.emplace_back(values[i]->PayloadForTests(), values[i]->SlotBytesForTests());
        values[i].reset();
    }

    std::size_t dirty = 0;
    for (const auto& slot : freed)
        if (NonZero(slot.first, slot.second) != 0)
            ++dirty;
    CHECK_MSG(dirty == 0, std::to_string(dirty) + " of " + std::to_string(freed.size()) +
                              " freed slots were not empty");
}

TEST_CASE(Arena_ReassigningLeavesNoTailInTheOldSlot)
{
    SecureString s = Filled(30, 'L');
    const unsigned char* old = s.PayloadForTests();
    const std::size_t oldSlot = s.SlotBytesForTests();

    s = std::string(20, 'k');

    CHECK_EQ(s.SlotBytesForTests(), oldSlot);
    CHECK(s == std::string(20, 'k'));
    CHECK_MSG(NonZero(old, oldSlot) == 0,
              "a remnant of the longer value remained in the old slot");
}

TEST_CASE(Arena_EverySlotHoldsTheTerminator)
{
    std::vector<std::size_t> lengths;
    for (std::size_t cls : SecureArena::kClasses)
        for (std::size_t n : { cls - 2, cls - 1, cls, cls + 1 })
            lengths.push_back(n);
    lengths.push_back(8192);
    lengths.push_back(SecureArena::kClasses[SecureArena::kClassCount - 1] - 1);

    const std::size_t top = SecureArena::kClasses[SecureArena::kClassCount - 1];
    for (std::size_t n : lengths)
    {
        SecureString v = Filled(n, 'z');
        if (!v.InArena())
        {
            if (n + 1 <= top)
            {
                CHECK_MSG(false, "a value of " + std::to_string(n) +
                                     " bytes fits a class and still "
                                     "ended up on the heap");
                return;
            }
            continue;
        }
        if (v.SlotBytesForTests() < n + 1)
        {
            CHECK_MSG(false, "a value of " + std::to_string(n) +
                                 " bytes got a slot of " +
                                 std::to_string(v.SlotBytesForTests()) +
                                 " -- the NUL byte falls into the neighbour");
            return;
        }
    }
}

TEST_CASE(Arena_AReusedSlotKeepsItsChunkAlive)
{
    std::vector<SecureString> first;
    for (int i = 0; i < 100; ++i)
        first.push_back(Filled(40, 'A'));
    for (std::size_t i = 0; i < first.size(); i += 2)
        first[i] = SecureString();

    std::vector<SecureString> reused;
    for (int i = 0; i < 50; ++i)
        reused.push_back(Filled(40, 'B'));

    first.clear();

    std::vector<SecureString> later;
    for (int i = 0; i < 100; ++i)
        later.push_back(Filled(40, 'C'));

    std::vector<Range> slots;
    for (const SecureString& s : reused)
        AddSlot(slots, s);
    for (const SecureString& s : later)
        AddSlot(slots, s);
    const std::size_t overlaps = CountOverlaps(slots);
    CHECK_MSG(overlaps == 0, std::to_string(overlaps) +
                                 " slots were handed out while still alive");

    std::size_t intact = 0;
    for (const SecureString& s : reused)
        if (s == std::string(40, 'B'))
            ++intact;
    CHECK_EQ(intact, reused.size());
}

TEST_CASE(Arena_NoSlotIsHandedOutTwice)
{
    std::vector<SecureString> live;
    for (int i = 0; i < 400; ++i)
        live.push_back(Filled(static_cast<std::size_t>(1 + (i * 37) % 300),
                              static_cast<char>('a' + (i % 26))));

    std::vector<Range> slots;
    for (const SecureString& s : live)
        AddSlot(slots, s);
    CHECK_EQ(slots.size(), live.size());
    const std::size_t overlaps = CountOverlaps(slots);
    CHECK_MSG(overlaps == 0, std::to_string(overlaps) + " overlapping slots");

    for (int i = 0; i < 400; ++i)
    {
        const std::size_t n = static_cast<std::size_t>(1 + (i * 37) % 300);
        const std::string want(n, static_cast<char>('a' + (i % 26)));
        if (!(live[static_cast<std::size_t>(i)] == want))
        {
            CHECK_MSG(false, "value " + std::to_string(i) + " was overwritten");
            break;
        }
    }
}

TEST_CASE(Arena_PayloadSurvivesVectorGrowth)
{
    std::vector<SecureString> v;
    v.push_back(SecureString(kLong));

    std::set<const unsigned char*> seen;
    seen.insert(v[0].PayloadForTests());
    for (int i = 0; i < 1000; ++i)
    {
        v.push_back(SecureString("fills the vector"));
        seen.insert(v[0].PayloadForTests());
    }

    CHECK_MSG(seen.size() == 1, "while growing, the payload was at " +
                                    std::to_string(seen.size()) + " places");
    CHECK(v[0] == kLong);
}

TEST_CASE(SecureString_MoveIsNoexcept)
{
    bool nothrowMove = std::is_nothrow_move_constructible_v<SecureString>;
    bool nothrowMoveAssign = std::is_nothrow_move_assignable_v<SecureString>;
    CHECK_MSG(nothrowMove, "SecureString is not noexcept-movable");
    CHECK_MSG(nothrowMoveAssign, "SecureString is not noexcept-assignable");

    std::size_t size = sizeof(SecureString);
    CHECK_MSG(size == 16, "SecureString is " + std::to_string(size) +
                              " bytes instead of 16");
}

TEST_CASE(SecureString_CopyIsDeepAndIndependent)
{
    SecureString a = kLong;
    SecureString b = a;

    CHECK(a == b);
    CHECK_MSG(a.PayloadForTests() != b.PayloadForTests(),
              "the copy shares the slot with the original");

    b = "other";
    CHECK(a == kLong);
    CHECK(b == "other");
}

TEST_CASE(SecureString_MoveStealsTheSlot)
{
    SecureString a = kLong;
    const unsigned char* slot = a.PayloadForTests();

    SecureString b = std::move(a);
    CHECK(b.PayloadForTests() == slot);
    CHECK(b == kLong);
    CHECK(a.Empty());
    CHECK(a.PayloadForTests() == nullptr);
}

TEST_CASE(SecureString_MoveAssignWipesTheOldValue)
{
    std::vector<SecureString> neighbours;
    for (int i = 0; i < 8; ++i)
        neighbours.push_back(SecureString(kLong));

    SecureString a = kLong;
    const unsigned char* old = a.PayloadForTests();
    const std::size_t slot = a.SlotBytesForTests();

    a = SecureString("neu");

    CHECK(a == "neu");
    CHECK_MSG(a.PayloadForTests() != old, "the new value is in the old slot");
    CHECK_MSG(NonZero(old, slot) == 0,
              "the overwritten value is still in its old slot");
}

TEST_CASE(SecureString_EqualityCompares)
{
    SecureString a = "password-one";
    SecureString same = "password-one";
    SecureString sameLength = "password-two";
    SecureString shorter = "password";
    SecureString empty;

    CHECK(a == same);
    CHECK(!(a == sameLength));
    CHECK(!(a == shorter));
    CHECK(a != shorter);
    CHECK(empty == SecureString());
    CHECK(empty == "");
    CHECK(!(a == empty));
    CHECK(a == std::string_view("password-one"));
    CHECK(a == std::string("password-one"));
}

TEST_CASE(SecureString_EmbeddedNulSurvivesFromStdString)
{
    std::string poisoned = "pre";
    poisoned.push_back('\0');
    poisoned += "post";

    SecureString fromStd;
    fromStd = poisoned;
    CHECK_EQ(fromStd.Size(), std::size_t{ 8 });
    CHECK_MSG(std::memcmp(fromStd.Bytes(), poisoned.data(), poisoned.size()) == 0,
              "the bytes after the NUL byte did not arrive");

    SecureString fromC = poisoned.c_str();
    CHECK_EQ(fromC.Size(), std::size_t{ 3 });
}

TEST_CASE(SecureString_WipeKeepsTheSlotButClearsIt)
{
    SecureString s = kLong;
    const unsigned char* slot = s.PayloadForTests();

    s.Wipe();

    CHECK(s.Empty());
    CHECK_MSG(s.PayloadForTests() == slot, "Wipe freed the slot");
    CHECK_MSG(NonZero(slot, std::strlen(kLong) + 1) == 0,
              "Wipe left the content in place");
}

TEST_CASE(SecureString_EmptyIsNeverNull)
{
    SecureString s;
    CHECK(s.CStr() != nullptr);
    CHECK(s.CStr()[0] == '\0');
    CHECK(s.Bytes() != nullptr);
    CHECK(s.View().empty());
}

TEST_CASE(SecureString_AssignFromItselfKeepsTheValue)
{
    SecureString s = "0123456789";
    s.Assign(s.CStr() + 3, 4);
    CHECK_MSG(s == "3456", "self-assignment destroyed the value: \"" +
                               s.ToStdString() + "\"");
}

TEST_CASE(SecureString_AViewHeldAcrossAWipeIsSafe)
{
    {
        SecureString s = "0123456789ABCDEFGHIJ";
        const std::string_view tail = s.View().substr(1);
        s.Wipe();
        s = tail;
        CHECK_EQ(s.Size(), std::size_t{ 19 });
        CHECK_MSG(NonZero(s.Bytes(), s.Size()) == 0,
                  "the view after Wipe returned something other than zeros");
    }
    {
        SecureString s = "0123456789ABCDEFGHIJ";
        const std::string_view whole = s.View();
        s.Wipe();
        s = whole;
        CHECK_EQ(s.Size(), std::size_t{ 20 });
        CHECK_MSG(NonZero(s.Bytes(), s.Size()) == 0,
                  "the view from the start returned foreign bytes");
    }
    {
        SecureString s = Filled(9000, 'x');
        CHECK(!s.InArena());
        const std::string_view tail = s.View().substr(1);
        s.Wipe();
        s = tail;
        CHECK_EQ(s.Size(), std::size_t{ 8999 });
        CHECK_MSG(NonZero(s.Bytes(), s.Size()) == 0,
                  "on the heap path the assignment read freed memory");
    }
}

TEST_CASE(SecureString_HeapFallbackIsWipedToo)
{
    SecureString s = Filled(9000, 'H');
    CHECK(!s.InArena());
    const unsigned char* buffer = s.PayloadForTests();

    s.Wipe();
    CHECK(s.Empty());
    CHECK_MSG(NonZero(buffer, 9001) == 0, "Wipe on the heap path left bytes behind");
}

TEST_CASE(SecureString_TooLongIsCountedAsLost)
{
    SecureArena& arena = SecureArena::Instance();
    SecureString s = "vorher";
    const std::size_t lostBefore = arena.LostCount();

    const char tiny[] = "abc";
    s.Assign(tiny, SecureString::kMaxLength + 1);

    CHECK(s.Empty());
    CHECK_EQ(arena.LostCount(), lostBefore + 1);
}

TEST_CASE(SecureString_ALostValueIsEmptyAndCounted)
{
    SecureArena& arena = SecureArena::Instance();
    SecureString s = kLong;
    const unsigned char* old = s.PayloadForTests();
    const std::size_t oldSlot = s.SlotBytesForTests();
    const std::size_t lostBefore = arena.LostCount();
    const std::size_t overflowBefore = arena.OverflowCount();

    {
        const NoHeapFallbackForTest noHeap;
        s = std::string(9000, 'v');
    }

    CHECK(s.Empty());
    CHECK(s.PayloadForTests() == nullptr);
    CHECK_EQ(arena.LostCount(), lostBefore + 1);
    CHECK_MSG(arena.OverflowCount() == overflowBefore,
              "a loss was counted as a fallback");
    CHECK_MSG(NonZero(old, oldSlot) == 0, "the old value remained");

    s = std::string(9000, 'v');
    CHECK_EQ(s.Size(), std::size_t{ 9000 });
}

TEST_CASE(Arena_OverflowFallsBackVisibly)
{
    SecureArena& arena = SecureArena::Instance();

    SecureString warmup = "anlegen";
    const ChunkLimitForTest limit(arena.ChunkCount());

    const std::size_t overflowBefore = arena.OverflowCount();
    std::vector<std::unique_ptr<SecureString>> big;
    const std::string payload(kLargeValue, 'x');
    std::size_t servedByArena = 0;
    bool fellBack = false;
    for (std::size_t i = 0; i < kMoreThanTheBudget && !fellBack; ++i)
    {
        auto s = std::make_unique<SecureString>();
        *s = payload;
        if (s->InArena())
        {
            ++servedByArena;
        }
        else
        {
            fellBack = true;
            CHECK_MSG(*s == payload, "the fallback value was lost");
        }
        big.push_back(std::move(s));
    }

    CHECK_MSG(servedByArena > 0,
              "the arena never served the class -- the case measures nothing");
    CHECK_MSG(fellBack, "the arena never filled up -- the case measures nothing");
    CHECK_MSG(arena.OverflowCount() > overflowBefore,
              "fell back but was not counted -- a silent fallback");
}

TEST_CASE(Arena_NeverGrowsPastItsBudget)
{
    SecureArena& arena = SecureArena::Instance();
    const std::size_t overflowBefore = arena.OverflowCount();

    std::vector<std::unique_ptr<SecureString>> big;
    const std::string payload(kLargeValue, 'b');
    for (std::size_t i = 0; i < kMoreThanTheBudget; ++i)
    {
        auto s = std::make_unique<SecureString>();
        *s = payload;
        big.push_back(std::move(s));
    }

    CHECK_MSG(arena.ChunkCount() <= SecureArena::kDefaultChunkLimit,
              "the arena created " + std::to_string(arena.ChunkCount()) +
                  " chunks, the budget allows " +
                  std::to_string(SecureArena::kDefaultChunkLimit));
    CHECK_MSG(arena.OverflowCount() > overflowBefore,
              "asked for more than the budget holds, and nothing fell back");

    std::size_t intact = 0;
    for (const auto& s : big)
        if (*s == payload)
            ++intact;
    CHECK_EQ(intact, big.size());
}

TEST_CASE(Arena_AnEmptiedChunkServesEveryClassAgain)
{
    SecureArena& arena = SecureArena::Instance();
    {
        const std::size_t overflowBefore = arena.OverflowCount();
        std::vector<std::unique_ptr<SecureString>> spike;
        const std::string payload(kLargeValue, 's');
        for (std::size_t i = 0; i < kMoreThanTheBudget; ++i)
        {
            auto s = std::make_unique<SecureString>();
            *s = payload;
            spike.push_back(std::move(s));
        }
        CHECK_MSG(arena.OverflowCount() > overflowBefore,
                  "the peak did not exhaust the budget -- so "
                  "the case measures nothing");
    }

    std::vector<SecureString> small;
    for (int i = 0; i < 100; ++i)
        small.push_back(Filled(1000, 'm'));
    std::size_t inArena = 0;
    for (const SecureString& s : small)
        if (s.InArena())
            ++inArena;
    CHECK_MSG(inArena == small.size(),
              "after the peak only " + std::to_string(inArena) +
                  " of 100 small values fit into the arena");

    std::vector<SecureString> large;
    for (std::size_t i = 0; i < kMoreThanTheBudget; ++i)
        large.push_back(Filled(kLargeValue, 'L'));

    std::vector<Range> slots;
    for (const SecureString& s : small)
        AddSlot(slots, s);
    for (const SecureString& s : large)
        AddSlot(slots, s);
    const std::size_t overlaps = CountOverlaps(slots);
    CHECK_MSG(overlaps == 0, std::to_string(overlaps) +
                                 " slots were handed out twice -- a "
                                 "free list pointed into a reset chunk");

    std::size_t intact = 0;
    for (const SecureString& s : small)
        if (s == std::string(1000, 'm'))
            ++intact;
    CHECK_EQ(intact, small.size());
}
