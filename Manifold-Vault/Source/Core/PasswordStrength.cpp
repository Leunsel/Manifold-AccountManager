#include "Core/Language.h"
#include "Core/PasswordStrength.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Bunker
{
    namespace
    {
        constexpr double kGuessesPerSecondAt1MiB1Pass = 6.0e6;
        constexpr double kAttackerMemoryBytes = 96.0 * 1024 * 1024 * 1024;

        double GuessRate(const Crypto::KdfParams& params)
        {
            const double memMiB = std::max(1.0, static_cast<double>(params.MemLimit) / (1024.0 * 1024.0));
            const double passes = std::max(1.0, static_cast<double>(params.OpsLimit));

            const double rateSingle = kGuessesPerSecondAt1MiB1Pass / (memMiB * passes);

            const double parallel = std::max(1.0, kAttackerMemoryBytes /
                                                  static_cast<double>(std::max<std::uint64_t>(params.MemLimit, 1)));
            return std::max(1.0, rateSingle * parallel);
        }

        constexpr double kRunCharacterWeight = 0.25;

        std::size_t CountRunCharacters(const char* p, std::size_t n)
        {
            if (n < 3)
                return 0;
            std::size_t runs = 0;
            for (std::size_t i = 2; i < n; ++i)
            {
                const int d1 = static_cast<unsigned char>(p[i]) - static_cast<unsigned char>(p[i - 1]);
                const int d2 = static_cast<unsigned char>(p[i - 1]) - static_cast<unsigned char>(p[i - 2]);
                if (d1 == d2 && (d1 == 0 || d1 == 1 || d1 == -1))
                    ++runs;
            }
            return runs;
        }
    }

    PasswordStrength EstimateStrength(const char* password, std::size_t length,
                                      const Crypto::KdfParams& params)
    {
        PasswordStrength s;
        s.Length = length;
        s.GuessesPerSecond = GuessRate(params);
        if (!password || length == 0)
            return s;

        bool lower = false, upper = false, digit = false, symbol = false, high = false;
        for (std::size_t i = 0; i < length; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(password[i]);
            if (c >= 'a' && c <= 'z') lower = true;
            else if (c >= 'A' && c <= 'Z') upper = true;
            else if (c >= '0' && c <= '9') digit = true;
            else if (c >= 0x80) high = true;
            else symbol = true;
        }

        int alphabet = 0;
        if (lower) { alphabet += 26; ++s.CharacterClasses; }
        if (upper) { alphabet += 26; ++s.CharacterClasses; }
        if (digit) { alphabet += 10; ++s.CharacterClasses; }
        if (symbol) { alphabet += 33; ++s.CharacterClasses; }
        if (high) { alphabet += 8; ++s.CharacterClasses; }

        const std::size_t runChars = CountRunCharacters(password, length);
        const double effectiveLength =
            static_cast<double>(length - runChars) +
            static_cast<double>(runChars) * kRunCharacterWeight;

        s.EntropyBits = effectiveLength * std::log2(static_cast<double>(std::max(alphabet, 2)));
        s.LooksRepetitive = length >= 4 && runChars * 3 > length;

        const double guesses = std::pow(2.0, std::min(s.EntropyBits, 512.0)) * 0.5;
        s.SecondsToCrack = guesses / s.GuessesPerSecond;
        return s;
    }

    const char* StrengthVerdict(const PasswordStrength& s)
    {
        const double years = s.SecondsToCrack / (365.25 * 24 * 3600);
        if (s.Length == 0)          return "";
        if (years < 1.0 / 365.0)    return T(Str::Strength_VeryWeak);
        if (years < 1.0)            return T(Str::Strength_Weak);
        if (years < 100.0)          return T(Str::Strength_Usable);
        if (years < 1.0e6)          return T(Str::Strength_Strong);
        return T(Str::Strength_VeryStrong);
    }

    std::string HumanDuration(double seconds)
    {
        if (!(seconds > 0.0) || std::isnan(seconds))
            return T(Str::Strength_DurationInstant);

        struct Unit { const char* One; const char* Many; double Seconds; };
        const Unit units[] = {
            { T(Str::Strength_UnitSecondOne),     T(Str::Strength_UnitSecondMany),   1.0 },
            { T(Str::Strength_UnitMinuteOne),      T(Str::Strength_UnitMinuteMany),    60.0 },
            { T(Str::Strength_UnitHourOne),      T(Str::Strength_UnitHourMany),    3600.0 },
            { T(Str::Strength_UnitDayOne),         T(Str::Strength_UnitDayMany),       86400.0 },
            { T(Str::Strength_UnitMonthOne),       T(Str::Strength_UnitMonthMany),     2629800.0 },
            { T(Str::Strength_UnitYearOne),        T(Str::Strength_UnitYearMany),      31557600.0 },
            { T(Str::Strength_UnitMillenniumOne), T(Str::Strength_UnitMillenniumMany), 31557600.0 * 1000.0 },
        };

        if (seconds < 1.0)
            return T(Str::Strength_DurationUnderASecond);

        const double universeSeconds = 13.8e9 * 31557600.0;
        if (seconds > universeSeconds)
            return T(Str::Strength_DurationLongerThanUniverse);

        const Unit* chosen = &units[0];
        for (const Unit& u : units)
            if (seconds >= u.Seconds)
                chosen = &u;

        const double value = seconds / chosen->Seconds;
        char buf[64];
        if (value >= 1000.0)
            std::snprintf(buf, sizeof(buf), T(Str::Strength_DurationMillions),
                          value / 1.0e6, chosen->Many);
        else if (value >= 10.0)
            std::snprintf(buf, sizeof(buf), "%.0f %s", value, chosen->Many);
        else
            std::snprintf(buf, sizeof(buf), "%.1f %s", value,
                          (value < 1.05 && value > 0.95) ? chosen->One : chosen->Many);
        return buf;
    }
}
