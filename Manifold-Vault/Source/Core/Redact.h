#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Bunker
{
    inline constexpr std::size_t kRedactKeepDefault = 3;

    inline constexpr std::size_t kRedactKeepMax = 10;

    std::string RedactAccountHint(std::string_view value, std::size_t keepChars);

    std::vector<std::string> RedactAccountHints(const std::vector<std::string_view>& values);
}
