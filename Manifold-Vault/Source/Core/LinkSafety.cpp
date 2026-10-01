#include "Core/LinkSafety.h"

namespace Bunker
{
    namespace
    {
        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        std::string_view Trimmed(std::string_view value)
        {
            std::size_t first = 0;
            while (first < value.size() && IsSpace(value[first]))
                ++first;
            std::size_t last = value.size();
            while (last > first && IsSpace(value[last - 1]))
                --last;
            return value.substr(first, last - first);
        }

        bool StartsWithFolded(std::string_view s, const char* prefix)
        {
            std::size_t i = 0;
            for (; prefix[i] != '\0'; ++i)
            {
                if (i >= s.size())
                    return false;
                char c = s[i];
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
                if (c != prefix[i])
                    return false;
            }
            return true;
        }
    }

    bool IsOpenableWebLink(std::string_view value)
    {
        const std::string_view link = Trimmed(value);
        if (link.empty() || link.size() > kMaxLinkBytes)
            return false;

        const char* schemes[] = { "http://", "https://" };
        const char* matched = nullptr;
        for (const char* scheme : schemes)
        {
            if (StartsWithFolded(link, scheme))
            {
                matched = scheme;
                break;
            }
        }
        if (!matched)
            return false;

        std::size_t schemeLength = 0;
        while (matched[schemeLength] != '\0')
            ++schemeLength;
        if (link.size() <= schemeLength)
            return false;

        for (const char c : link)
        {
            if (static_cast<unsigned char>(c) < 0x20 ||
                static_cast<unsigned char>(c) == 0x7F)
            {
                return false;
            }
        }
        return true;
    }

    std::string_view LinkToOpen(std::string_view value)
    {
        if (!IsOpenableWebLink(value))
            return {};
        return Trimmed(value);
    }
}
