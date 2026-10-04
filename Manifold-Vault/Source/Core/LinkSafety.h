#pragma once

#include <cstddef>
#include <string_view>

namespace Bunker
{
    inline constexpr std::size_t kMaxLinkBytes = 2048;

    bool IsOpenableWebLink(std::string_view value);

    std::string_view LinkToOpen(std::string_view value);

    bool IsUnencryptedWebLink(std::string_view value);
}
