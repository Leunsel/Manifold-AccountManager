#include "Core/ListKind.h"

namespace Bunker::Vault
{
    namespace
    {
        struct KindEntry
        {
            std::uint32_t Kind;
            ListKindTexts Texts;
        };

        constexpr KindEntry kKinds[] = {
            { kListKindProductKey,
              { Str::List_SectionProductKey, Str::List_TitleProductKey, Str::List_NameProductKey,
                Str::List_LabelHintProductKey, Str::List_SecretHintProductKey,
                "product_key", false } },
            { kListKindSecurityQuestion,
              { Str::List_SectionSecurityQuestion, Str::List_TitleSecurityQuestion, Str::List_NameSecurityQuestion,
                Str::List_LabelHintSecurityQuestion, Str::List_SecretHintSecurityQuestion,
                "security_question", true } },
            { kListKindOrderNumber,
              { Str::List_SectionOrderNumber, Str::List_TitleOrderNumber, Str::List_NameOrderNumber,
                Str::List_LabelHintOrderNumber, Str::List_SecretHintOrderNumber,
                "order_number", false } },
            { kListKindGiftCode,
              { Str::List_SectionGiftCode, Str::List_TitleGiftCode, Str::List_NameGiftCode,
                Str::List_LabelHintGiftCode, Str::List_SecretHintGiftCode,
                "gift_code", false } },
            { kListKindSerialNumber,
              { Str::List_SectionSerialNumber, Str::List_TitleSerialNumber, Str::List_NameSerialNumber,
                Str::List_LabelHintSerialNumber, Str::List_SecretHintSerialNumber,
                "serial_number", false } },
            { kListKindOther,
              { Str::List_SectionOther, Str::List_TitleOther, Str::List_NameOther,
                Str::List_LabelHintOther, Str::List_SecretHintOther,
                "other", false } },
        };

        constexpr std::uint32_t kGameStore[] = { kListKindProductKey, kListKindOrderNumber };
        constexpr std::uint32_t kMicrosoft[] = { kListKindSecurityQuestion, kListKindProductKey };
        constexpr std::uint32_t kKeysOnly[] = { kListKindProductKey };

        struct Offer
        {
            const char* PlatformId;
            const std::uint32_t* Kinds;
            std::size_t Count;
        };

#define BUNKER_OFFER(id, kinds) { id, kinds, sizeof(kinds) / sizeof(kinds[0]) }
        constexpr Offer kOffers[] = {
            BUNKER_OFFER("steam", kGameStore),
            BUNKER_OFFER("square-steam", kGameStore),
            BUNKER_OFFER("steam-symbol", kGameStore),
            BUNKER_OFFER("gog", kGameStore),
            BUNKER_OFFER("epic-games", kGameStore),
            BUNKER_OFFER("ubisoft", kGameStore),
            BUNKER_OFFER("ea", kGameStore),
            BUNKER_OFFER("microsoft", kMicrosoft),
            BUNKER_OFFER("xbox", kKeysOnly),
            BUNKER_OFFER("windows", kKeysOnly),
        };
#undef BUNKER_OFFER

        char Lower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t';
        }

        bool SameId(std::string_view typed, const char* known)
        {
            std::size_t i = 0;
            for (; i < typed.size(); ++i)
            {
                if (known[i] == '\0' || Lower(typed[i]) != known[i])
                    return false;
            }
            return known[i] == '\0';
        }
    }

    bool IsKnownListKind(std::uint32_t kind)
    {
        for (const KindEntry& k : kKinds)
            if (k.Kind == kind)
                return true;
        return false;
    }

    const ListKindTexts& ListKindTextsFor(std::uint32_t kind)
    {
        for (const KindEntry& k : kKinds)
            if (k.Kind == kind)
                return k.Texts;
        for (const KindEntry& k : kKinds)
            if (k.Kind == kListKindOther)
                return k.Texts;
        return kKinds[0].Texts;
    }

    bool ListKindFromCsvName(std::string_view name, std::uint32_t& kind)
    {
        for (const KindEntry& k : kKinds)
            if (name == k.Texts.CsvName)
            {
                kind = k.Kind;
                return true;
            }

        if (name.empty() || name.size() > 10)
            return false;
        std::uint64_t value = 0;
        for (const char c : name)
        {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + static_cast<std::uint64_t>(c - '0');
        }
        if (value > 0xFFFFFFFFull)
            return false;
        kind = static_cast<std::uint32_t>(value);
        return true;
    }

    std::string_view ListKindCsvName(std::uint32_t kind, char (&scratch)[12])
    {
        for (const KindEntry& k : kKinds)
            if (k.Kind == kind)
                return k.Texts.CsvName;

        std::size_t at = sizeof(scratch);
        do
        {
            scratch[--at] = static_cast<char>('0' + kind % 10);
            kind /= 10;
        } while (kind != 0 && at > 0);
        return std::string_view(scratch + at, sizeof(scratch) - at);
    }

    ListKindOffer ListKindsFor(const char* platformId)
    {
        if (!platformId)
            return {};
        std::string_view id(platformId);
        while (!id.empty() && IsSpace(id.front()))
            id.remove_prefix(1);
        while (!id.empty() && IsSpace(id.back()))
            id.remove_suffix(1);
        if (id.empty())
            return {};

        for (const Offer& o : kOffers)
            if (SameId(id, o.PlatformId))
                return ListKindOffer{ o.Kinds, o.Count };
        return {};
    }
}
