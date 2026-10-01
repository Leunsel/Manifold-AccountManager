#include "Core/SecretChars.h"

namespace Bunker
{
    static_assert(sizeof(wchar_t) == 2,
                  "AppendUtf16 liest UTF-16, so wie CF_UNICODETEXT es liefert");

    bool AppendUtf8(unsigned char* buf, std::size_t capacity, std::size_t& length,
                    unsigned int cp) noexcept
    {
        if (!buf)
            return false;
        const std::size_t n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (length + n + 1 > capacity)
            return false;

        unsigned char* out = buf + length;
        switch (n)
        {
        case 1:
            out[0] = static_cast<unsigned char>(cp);
            break;
        case 2:
            out[0] = static_cast<unsigned char>(0xC0 | (cp >> 6));
            out[1] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            break;
        case 3:
            out[0] = static_cast<unsigned char>(0xE0 | (cp >> 12));
            out[1] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
            out[2] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            break;
        default:
            out[0] = static_cast<unsigned char>(0xF0 | (cp >> 18));
            out[1] = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
            out[2] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
            out[3] = static_cast<unsigned char>(0x80 | (cp & 0x3F));
            break;
        }
        length += n;
        buf[length] = 0;
        return true;
    }

    void AppendUtf16(unsigned char* buf, std::size_t capacity, std::size_t& length,
                     const wchar_t* text, std::size_t units) noexcept
    {
        if (!text)
            return;
        for (std::size_t i = 0; i < units && text[i] != 0; ++i)
        {
            unsigned int cp = static_cast<unsigned int>(text[i]);
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                const unsigned int low = i + 1 < units ? static_cast<unsigned int>(text[i + 1]) : 0u;
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
                else
                {
                    cp = 0xFFFD;
                }
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
                cp = 0xFFFD;
            }

            if (IsSecretFieldChar(cp))
                AppendUtf8(buf, capacity, length, cp);
        }
    }
}
