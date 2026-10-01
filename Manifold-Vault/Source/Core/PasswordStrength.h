#pragma once

#include "Core/Crypto.h"

#include <cstddef>
#include <string>

namespace Bunker
{
    struct PasswordStrength
    {
        double EntropyBits = 0.0;
        double GuessesPerSecond = 0.0;
        double SecondsToCrack = 0.0;
        int CharacterClasses = 0;
        std::size_t Length = 0;
        bool LooksRepetitive = false;
    };

    PasswordStrength EstimateStrength(const char* password, std::size_t length,
                                      const Crypto::KdfParams& params);

    const char* StrengthVerdict(const PasswordStrength& s);

    std::string HumanDuration(double seconds);
}
