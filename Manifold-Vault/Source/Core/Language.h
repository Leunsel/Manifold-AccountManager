#pragma once

#include "Lang/Languages.h"

#include <cstddef>

namespace Bunker
{
    const char* T(Str id);

    const Lang::LanguageTable& SetLanguage(const char* code);

    const Lang::LanguageTable& CurrentLanguage();

    const char* const* PassphraseWords(std::size_t& count);
    inline constexpr std::size_t kMinPassphraseWords = 128;
}
