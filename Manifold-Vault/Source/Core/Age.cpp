#include "Core/Age.h"

namespace Bunker::Vault
{
    namespace
    {
        constexpr std::uint64_t kMinute = 60;
        constexpr std::uint64_t kHour = 60 * kMinute;
        constexpr std::uint64_t kDay = 24 * kHour;
        constexpr std::uint64_t kWeek = 7 * kDay;
        constexpr std::uint64_t kMonth = 30 * kDay;
        constexpr std::uint64_t kYear = 365 * kDay;
    }

    Age AgeBetween(std::uint64_t thenUtc, std::uint64_t nowUtc)
    {
        if (thenUtc >= nowUtc)
            return {};
        const std::uint64_t d = nowUtc - thenUtc;
        if (d < kMinute)
            return {};
        if (d < kHour)
            return { AgeUnit::Minutes, d / kMinute };
        if (d < kDay)
            return { AgeUnit::Hours, d / kHour };
        if (d < kWeek)
            return { AgeUnit::Days, d / kDay };
        if (d < kMonth)
            return { AgeUnit::Weeks, d / kWeek };
        if (d < kYear)
            return { AgeUnit::Months, d / kMonth };
        return { AgeUnit::Years, d / kYear };
    }
}
