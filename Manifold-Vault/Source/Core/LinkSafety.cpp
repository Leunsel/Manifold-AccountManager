#include "Core/LinkSafety.h"

#include <string>

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

        bool ParseIpv4(std::string_view host, unsigned (&octets)[4])
        {
            std::size_t part = 0;
            unsigned value = 0;
            std::size_t digits = 0;
            for (const char c : host)
            {
                if (c == '.')
                {
                    if (digits == 0 || part == 3)
                        return false;
                    octets[part++] = value;
                    value = 0;
                    digits = 0;
                    continue;
                }
                if (c < '0' || c > '9' || ++digits > 3)
                    return false;
                value = value * 10 + static_cast<unsigned>(c - '0');
                if (value > 255)
                    return false;
            }
            if (digits == 0 || part != 3)
                return false;
            octets[3] = value;
            return true;
        }

        // Home-network addresses rarely offer https; flagging them would only be noise.
        bool IsLocalHost(const std::string& host)
        {
            if (host.empty() || host.find('.') == std::string::npos)
                return true;
            if (host.front() == '[')
                return host == "[::1]" || host.starts_with("[fe8") || host.starts_with("[fe9") ||
                       host.starts_with("[fea") || host.starts_with("[feb") ||
                       host.starts_with("[fc") || host.starts_with("[fd");

            unsigned ip[4] = {};
            if (ParseIpv4(host, ip))
                return ip[0] == 10 || ip[0] == 127 || (ip[0] == 169 && ip[1] == 254) ||
                       (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
                       (ip[0] == 192 && ip[1] == 168);

            const char* local[] = { "localhost", "local", "lan", "home", "internal",
                                    "localdomain", "home.arpa", "fritz.box", "speedport.ip" };
            for (const char* name : local)
            {
                const std::string_view n(name);
                if (host == n || (host.ends_with(n) && host[host.size() - n.size() - 1] == '.'))
                    return true;
            }
            return false;
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

    bool IsUnencryptedWebLink(std::string_view value)
    {
        const std::string_view link = LinkToOpen(value);
        if (link.empty() || !StartsWithFolded(link, "http://"))
            return false;

        std::string_view authority = link.substr(7);
        authority = authority.substr(0, authority.find_first_of("/?#"));
        if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos)
            authority = authority.substr(at + 1);

        std::string host;
        for (const char c : authority)
        {
            if (c == ':' && (host.empty() || host.front() != '['))
                break;
            host.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
            if (c == ']')
                break;
        }
        if (!host.empty() && host.back() == '.')
            host.pop_back();
        return !IsLocalHost(host);
    }
}
