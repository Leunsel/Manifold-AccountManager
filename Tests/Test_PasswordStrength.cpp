#include "TestRunner.h"

#include "Core/Crypto.h"
#include "Core/Language.h"
#include "Core/PasswordStrength.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace Bunker;

namespace
{
    PasswordStrength Of(const char* pw, const Crypto::KdfParams& p)
    {
        return EstimateStrength(pw, std::strlen(pw), p);
    }
}

TEST_CASE(Strength_LongerIsNeverWorse)
{
    const auto params = Crypto::KdfParams::Moderate();
    double previous = -1.0;
    std::string pw;
    for (int i = 0; i < 24; ++i)
    {
        pw += "x";
        const PasswordStrength s = Of(pw.c_str(), params);
        CHECK_MSG(s.EntropyBits >= previous,
                  "entropy dropped at length " + std::to_string(pw.size()));
        previous = s.EntropyBits;
    }
}

TEST_CASE(Strength_MoreCharacterClassesHelp)
{
    const auto params = Crypto::KdfParams::Moderate();
    const PasswordStrength lower = Of("abcdefghijkl", params);
    const PasswordStrength mixed = Of("aBcDeFgHiJkL", params);
    const PasswordStrength full = Of("aB3!eF6?iJ9-", params);

    CHECK(mixed.EntropyBits > lower.EntropyBits);
    CHECK(full.EntropyBits > mixed.EntropyBits);
    CHECK_EQ(lower.CharacterClasses, 1);
    CHECK_EQ(mixed.CharacterClasses, 2);
    CHECK(full.CharacterClasses >= 3);
}

TEST_CASE(Strength_CostlierKdfBuysTime)
{
    const char* pw = "ein mittleres Passwort 42";
    const PasswordStrength cheap = Of(pw, Crypto::KdfParams::TestOnlyMinimum());
    const PasswordStrength moderate = Of(pw, Crypto::KdfParams::Moderate());

    CHECK(moderate.SecondsToCrack > cheap.SecondsToCrack);
    CHECK(moderate.GuessesPerSecond < cheap.GuessesPerSecond);
    CHECK(std::abs(moderate.EntropyBits - cheap.EntropyBits) < 1e-9);
}

TEST_CASE(Strength_RepetitionIsPenalised)
{
    const auto params = Crypto::KdfParams::Moderate();
    const PasswordStrength runs = Of("abcdefghijklmnop", params);
    const PasswordStrength mixedUp = Of("qmzvfpxrtdbwnkhj", params);

    CHECK(runs.LooksRepetitive);
    CHECK(!mixedUp.LooksRepetitive);
    CHECK(runs.EntropyBits < mixedUp.EntropyBits);

    CHECK(Of("aaaaaaaaaaaa", params).LooksRepetitive);
    CHECK(Of("123456789012", params).LooksRepetitive);
}

TEST_CASE(Strength_EmptyAndNullAreSafe)
{
    const auto params = Crypto::KdfParams::Moderate();
    const PasswordStrength empty = EstimateStrength("", 0, params);
    CHECK_EQ(empty.EntropyBits, 0.0);
    CHECK_EQ(empty.Length, std::size_t{ 0 });
    CHECK(std::strlen(StrengthVerdict(empty)) == 0);

    const PasswordStrength null = EstimateStrength(nullptr, 0, params);
    CHECK_EQ(null.EntropyBits, 0.0);
}

TEST_CASE(Strength_VerdictRisesWithTime)
{
    const auto params = Crypto::KdfParams::Moderate();
    const char* weak = "abc";
    const char* strong = "7#kQ!2vR$9mZ&4pL@8nT";
    CHECK(Of(weak, params).SecondsToCrack < Of(strong, params).SecondsToCrack);
    CHECK(std::strcmp(StrengthVerdict(Of(weak, params)),
                      T(Str::Strength_VeryWeak)) == 0);
    CHECK(std::strcmp(StrengthVerdict(Of(strong, params)),
                      T(Str::Strength_VeryStrong)) == 0);
}

TEST_CASE(Strength_HumanDurationReadsSensibly)
{
    CHECK(HumanDuration(0.0) == T(Str::Strength_DurationInstant));
    CHECK(HumanDuration(0.4) == T(Str::Strength_DurationUnderASecond));
    CHECK(HumanDuration(45.0).find(T(Str::Strength_UnitSecondMany)) != std::string::npos);
    CHECK(HumanDuration(3600.0 * 5).find(T(Str::Strength_UnitHourMany)) != std::string::npos);
    CHECK(HumanDuration(86400.0 * 400).find(T(Str::Strength_UnitYearOne)) != std::string::npos);
    CHECK(HumanDuration(1.0e30) == T(Str::Strength_DurationLongerThanUniverse));
}

TEST_CASE(Strength_ReadoutAgreesWithItsOwnArithmetic)
{
    const auto params = Crypto::KdfParams::Moderate();
    const PasswordStrength s = Of("Ein Passwort mit 9 Zeichen!", params);

    const double guesses = std::pow(2.0, s.EntropyBits) * 0.5;
    const double expected = guesses / s.GuessesPerSecond;
    CHECK_MSG(std::abs(expected - s.SecondsToCrack) / expected < 1e-9,
              "SecondsToCrack does not match EntropyBits/GuessesPerSecond");
}
