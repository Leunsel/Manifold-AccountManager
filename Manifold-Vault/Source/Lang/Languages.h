#pragma once

#include "Lang/De.h"
#include "Lang/En.h"
#include "Lang/Strings.h"

namespace Bunker::Lang
{
    struct LanguageTable
    {
        const char* Code;
        const char* Name;
        const Strings* Text;
        const char* const* Words;
        std::size_t WordCount;
    };

    inline constexpr LanguageTable kLanguages[] = {
        { "en", "English", &kEnglishStrings, kEnglishWords,
          sizeof(kEnglishWords) / sizeof(kEnglishWords[0]) },
        { "de", "Deutsch", &kGerman, kGermanWords,
          sizeof(kGermanWords) / sizeof(kGermanWords[0]) },
    };

    inline constexpr std::size_t kLanguageCount =
        sizeof(kLanguages) / sizeof(kLanguages[0]);

    static_assert(kLanguageCount >= 1, "without English there is no fallback");
}
