#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Bunker
{
    std::string FormatLocalTimestamp(std::uint64_t utcSeconds);

    std::string UppercaseGerman(const std::string& text);

    std::string LowercaseGerman(std::string_view text);

    std::size_t Utf8SequenceLength(unsigned char lead);

    bool Utf8IsValid(const std::string& text);

    std::size_t Utf8SnapBack(std::string_view text, std::size_t index);
}
