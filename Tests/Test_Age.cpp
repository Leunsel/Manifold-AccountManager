#include "TestRunner.h"

#include "Core/Age.h"

using namespace Bunker::Vault;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000ull;
    constexpr std::uint64_t kMin = 60;
    constexpr std::uint64_t kHour = 3600;
    constexpr std::uint64_t kDay = 86400;

    bool Is(std::uint64_t secondsAgo, AgeUnit unit, std::uint64_t count)
    {
        const Age a = AgeBetween(kNow - secondsAgo, kNow);
        return a.Unit == unit && a.Count == count;
    }
}

TEST_CASE(Age_EachStepStartsWhereTheLastEnds)
{
    CHECK(Is(0, AgeUnit::JustNow, 0));
    CHECK(Is(59, AgeUnit::JustNow, 0));
    CHECK(Is(60, AgeUnit::Minutes, 1));
    CHECK(Is(kHour - 1, AgeUnit::Minutes, 59));
    CHECK(Is(kHour, AgeUnit::Hours, 1));
    CHECK(Is(kDay - 1, AgeUnit::Hours, 23));
    CHECK(Is(kDay, AgeUnit::Days, 1));
    CHECK(Is(7 * kDay - 1, AgeUnit::Days, 6));
    CHECK(Is(7 * kDay, AgeUnit::Weeks, 1));
    CHECK(Is(30 * kDay - 1, AgeUnit::Weeks, 4));
    CHECK(Is(30 * kDay, AgeUnit::Months, 1));
    CHECK(Is(365 * kDay - 1, AgeUnit::Months, 12));
    CHECK(Is(365 * kDay, AgeUnit::Years, 1));
    CHECK(Is(3 * 365 * kDay + kDay, AgeUnit::Years, 3));
}

TEST_CASE(Age_RoundsDownLikeSpeech)
{
    CHECK(Is(47 * kHour, AgeUnit::Days, 1));
    CHECK(Is(90 * kMin, AgeUnit::Hours, 1));
}

TEST_CASE(Age_TheFutureIsJustNow)
{
    const Age a = AgeBetween(kNow + 3 * kDay, kNow);
    CHECK(a.Unit == AgeUnit::JustNow);
    CHECK_EQ(a.Count, std::uint64_t{ 0 });
}
