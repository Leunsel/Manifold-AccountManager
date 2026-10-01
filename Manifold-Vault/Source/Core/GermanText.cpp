#include "Core/GermanText.h"

#include <cctype>
#include <cstdint>
#include <ctime>

namespace Bunker
{
    std::string FormatLocalTimestamp(std::uint64_t utcSeconds)
    {
        if (utcSeconds == 0)
            return "unknown";
        const std::time_t t = static_cast<std::time_t>(utcSeconds);
        std::tm local{};
        if (::localtime_s(&local, &t) != 0)
            return "unknown";
        char buffer[64];
        if (std::strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", &local) == 0)
            return "unknown";
        return buffer;
    }

    std::string LowercaseGerman(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);

            if (c < 0x80)
            {
                out.push_back(static_cast<char>(std::tolower(c)));
                continue;
            }

            if (c == 0xC3 && i + 1 < text.size())
            {
                const unsigned char second = static_cast<unsigned char>(text[i + 1]);
                ++i;
                if (second >= 0x80 && second <= 0x9E && second != 0x97)
                {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(second + 0x20));
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(second));
                }
                continue;
            }

            const std::size_t seq = Utf8SequenceLength(c);
            for (std::size_t k = 0; k < seq && i + k < text.size(); ++k)
                out.push_back(text[i + k]);
            i += seq - 1;
        }
        return out;
    }

    std::size_t Utf8SequenceLength(unsigned char lead)
    {
        if (lead < 0x80) return 1;
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        if ((lead & 0xF8) == 0xF0) return 4;
        return 1;
    }

    std::size_t Utf8SnapBack(std::string_view text, std::size_t index)
    {
        if (index > text.size())
            index = text.size();
        while (index > 0 && index < text.size() &&
               (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80)
            --index;
        return index;
    }

    bool Utf8IsValid(const std::string& text)
    {
        std::size_t i = 0;
        while (i < text.size())
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            int extra = 0;
            std::uint32_t cp = 0;
            if (lead < 0x80)                { extra = 0; cp = lead; }
            else if ((lead & 0xE0) == 0xC0) { extra = 1; cp = lead & 0x1Fu; }
            else if ((lead & 0xF0) == 0xE0) { extra = 2; cp = lead & 0x0Fu; }
            else if ((lead & 0xF8) == 0xF0) { extra = 3; cp = lead & 0x07u; }
            else return false;

            if (i + static_cast<std::size_t>(extra) >= text.size())
                return false;
            for (int k = 1; k <= extra; ++k)
            {
                const unsigned char c = static_cast<unsigned char>(text[i + k]);
                if ((c & 0xC0) != 0x80)
                    return false;
                cp = (cp << 6) | (c & 0x3Fu);
            }
            if (extra == 1 && cp < 0x80)    return false;
            if (extra == 2 && cp < 0x800)   return false;
            if (extra == 3 && cp < 0x10000) return false;
            if (cp > 0x10FFFF)              return false;
            if (cp >= 0xD800 && cp <= 0xDFFF) return false;

            i += static_cast<std::size_t>(extra) + 1;
        }
        return true;
    }

    std::string UppercaseGerman(const std::string& text)
    {
        std::string out;
        out.reserve(text.size() + 4);

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);

            if (c < 0x80)
            {
                out.push_back(static_cast<char>(std::toupper(c)));
                continue;
            }

            if (c == 0xC3 && i + 1 < text.size())
            {
                const unsigned char second = static_cast<unsigned char>(text[i + 1]);
                ++i;
                if (second == 0x9F)
                {
                    out += "SS";
                }
                else if (second >= 0xA0 && second <= 0xBE && second != 0xB7)
                {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(second - 0x20));
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                    out.push_back(static_cast<char>(second));
                }
                continue;
            }

            const std::size_t seq = Utf8SequenceLength(c);
            for (std::size_t k = 0; k < seq && i + k < text.size(); ++k)
                out.push_back(text[i + k]);
            i += seq - 1;
        }
        return out;
    }
}
