#pragma once

#include "Core/Model.h"
#include "Lang/Strings.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Bunker::Vault
{
    struct ListKindTexts
    {
        Str Section;
        Str Title;
        Str Name;
        Str LabelHint;
        Str SecretHint;
        const char* CsvName;
        bool OffersGenerator;
    };

    inline constexpr std::uint32_t kListKindOrder[] = {
        kListKindProductKey,
        kListKindOrderNumber,
        kListKindGiftCode,
        kListKindSerialNumber,
        kListKindSecurityQuestion,
        kListKindOther,
    };
    inline constexpr std::size_t kListKindCount =
        sizeof(kListKindOrder) / sizeof(kListKindOrder[0]);

    bool IsKnownListKind(std::uint32_t kind);

    const ListKindTexts& ListKindTextsFor(std::uint32_t kind);

    bool ListKindFromCsvName(std::string_view name, std::uint32_t& kind);

    std::string_view ListKindCsvName(std::uint32_t kind, char (&scratch)[12]);

    struct ListKindOffer
    {
        const std::uint32_t* Kinds = nullptr;
        std::size_t Count = 0;
    };

    ListKindOffer ListKindsFor(const char* platformId);
}
