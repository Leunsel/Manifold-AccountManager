#include "Core/Redact.h"

#include "Core/GermanText.h"
#include "Core/Secret.h"

#include <map>

namespace Bunker
{
    namespace
    {
        constexpr std::string_view kMask = "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";

        std::string_view FirstCharacters(std::string_view text, std::size_t count,
                                         bool& tookEverything)
        {
            std::size_t i = 0;
            std::size_t taken = 0;
            while (i < text.size() && taken < count)
            {
                i += Utf8SequenceLength(static_cast<unsigned char>(text[i]));
                ++taken;
            }
            if (i > text.size())
                i = text.size();
            tookEverything = (i >= text.size());
            return text.substr(0, i);
        }

        bool SplitAddress(std::string_view value, std::string_view& local,
                          std::string_view& domain)
        {
            const std::size_t at = value.find('@');
            if (at == std::string_view::npos || at == 0 || at + 1 >= value.size())
                return false;
            if (value.find('@', at + 1) != std::string_view::npos)
                return false;
            local = value.substr(0, at);
            domain = value.substr(at);
            return true;
        }

        std::string Masked(std::string_view head, std::string_view tail)
        {
            std::string out;
            out.reserve(head.size() + kMask.size() + tail.size());
            out.append(head);
            out.append(kMask);
            out.append(tail);
            return out;
        }
    }

    std::string RedactAccountHint(std::string_view value, std::size_t keepChars)
    {
        if (value.empty())
            return {};

        std::string_view local;
        std::string_view domain;
        if (SplitAddress(value, local, domain))
        {
            bool whole = false;
            const std::string_view head = FirstCharacters(local, keepChars, whole);
            if (whole)
                return std::string(value);
            return Masked(head, domain);
        }

        bool whole = false;
        const std::string_view head = FirstCharacters(value, keepChars, whole);
        if (whole)
            return std::string(value);
        return Masked(head, {});
    }

    std::vector<std::string> RedactAccountHints(const std::vector<std::string_view>& values)
    {
        std::vector<std::string> out(values.size());
        std::vector<std::size_t> keep(values.size(), kRedactKeepDefault);

        for (std::size_t round = 0; ; ++round)
        {
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                WipeString(out[i]);
                out[i] = RedactAccountHint(values[i], keep[i]);
            }

            std::map<std::string_view, std::vector<std::size_t>> groups;
            for (std::size_t i = 0; i < out.size(); ++i)
                groups[out[i]].push_back(i);

            bool widened = false;
            for (const auto& group : groups)
            {
                if (group.second.size() < 2)
                    continue;
                bool differ = false;
                for (std::size_t k = 1; k < group.second.size(); ++k)
                    if (values[group.second[k]] != values[group.second[0]])
                        differ = true;
                if (!differ)
                    continue;

                for (std::size_t index : group.second)
                {
                    if (keep[index] < kRedactKeepMax)
                    {
                        ++keep[index];
                        widened = true;
                    }
                }
            }

            if (!widened)
                break;
            if (round > kRedactKeepMax + 2)
                break;
        }
        return out;
    }
}
