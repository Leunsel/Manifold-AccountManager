#pragma once

#include <cstddef>

namespace Bunker
{
    constexpr bool IsSecretFieldChar(unsigned int cp) noexcept
    {
        return cp >= 32 && cp != 127;
    }

    bool AppendUtf8(unsigned char* buf, std::size_t capacity, std::size_t& length,
                    unsigned int cp) noexcept;

    void AppendUtf16(unsigned char* buf, std::size_t capacity, std::size_t& length,
                     const wchar_t* text, std::size_t units) noexcept;
}
