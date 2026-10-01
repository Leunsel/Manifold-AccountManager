#pragma once

#include "Core/Random.h"
#include "Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Bunker
{
    struct GeneratorOptions
    {
        std::size_t Length = 20;

        bool UseLower = true;
        bool UseUpper = true;
        bool UseDigits = true;
        bool UseSymbols = true;

        bool AvoidAmbiguous = false;

        bool RequireEveryClass = true;
    };

    struct GeneratedPassword
    {
        std::string Text;

        double EntropyBits = 0.0;
        std::size_t AlphabetSize = 0;
        int Redraws = 0;
    };

    Result<GeneratedPassword> GeneratePassword(const GeneratorOptions& options,
                                               RandomSource& source);

    Result<GeneratedPassword> GeneratePassphrase(std::size_t words, char separator,
                                                 RandomSource& source);

    std::size_t PassphraseWordCount();
}
