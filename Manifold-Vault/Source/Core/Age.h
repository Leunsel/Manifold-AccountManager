#pragma once

#include <cstdint>

namespace Bunker::Vault
{
    enum class AgeUnit : std::uint8_t
    {
        JustNow,
        Minutes,
        Hours,
        Days,
        Weeks,
        Months,
        Years,
    };

    struct Age
    {
        AgeUnit Unit = AgeUnit::JustNow;
        std::uint64_t Count = 0;
    };

    Age AgeBetween(std::uint64_t thenUtc, std::uint64_t nowUtc);
}
