#include "TestRunner.h"

#include "Core/Generator.h"
#include "Core/Random.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Bunker;

TEST_CASE(Random_DeterministicSourceIsStateful)
{
    DeterministicRandom r(42);
    unsigned char a[32], b[32];
    r.Fill(a, sizeof(a));
    r.Fill(b, sizeof(b));
    CHECK(std::memcmp(a, b, sizeof(a)) != 0);

    DeterministicRandom r2(42);
    unsigned char c[32];
    r2.Fill(c, sizeof(c));
    CHECK_BYTES_EQ(a, c, sizeof(a));

    DeterministicRandom r3(43);
    unsigned char d[32];
    r3.Fill(d, sizeof(d));
    CHECK(std::memcmp(a, d, sizeof(a)) != 0);
}

TEST_CASE(Random_UniformIsWithinBounds)
{
    DeterministicRandom r(7);
    for (std::uint32_t bound : { 2u, 3u, 26u, 62u, 255u, 1000u })
        for (int i = 0; i < 500; ++i)
            CHECK(r.Uniform(bound) < bound);

    CHECK_EQ(r.Uniform(1), 0u);
    CHECK_EQ(r.Uniform(0), 0u);
}

TEST_CASE(Random_UniformRejectsTheBiasedTail)
{
    const unsigned char script[] = {
        0xFF, 0xFF, 0xFF, 0xFF,
        0x05, 0x00, 0x00, 0x00,
    };
    ScriptedRandom source(script, sizeof(script));

    CHECK_EQ(source.Uniform(62), 5u);
    CHECK_MSG(source.Consumed() == 8,
              "consumed " + std::to_string(source.Consumed()) +
              " bytes instead of 8 -- the rejection step is missing");

    static_assert(0xFFFFFFFFu >= 0xFFFFFFFFu - (0xFFFFFFFFu % 62u),
                  "0xFFFFFFFF is not in the rejected range");
    static_assert(0xFFFFFFFFu % 62u == 3u,
                  "modulo would not yield 3 here");
}

TEST_CASE(Random_UniformAcceptsAValueJustBelowTheLimit)
{
    const std::uint32_t bound = 62;
    const std::uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % bound);
    const std::uint32_t justBelow = limit - 1;

    unsigned char script[4];
    script[0] = static_cast<unsigned char>(justBelow & 0xFF);
    script[1] = static_cast<unsigned char>((justBelow >> 8) & 0xFF);
    script[2] = static_cast<unsigned char>((justBelow >> 16) & 0xFF);
    script[3] = static_cast<unsigned char>((justBelow >> 24) & 0xFF);

    ScriptedRandom source(script, sizeof(script));
    CHECK_EQ(source.Uniform(bound), justBelow % bound);
    CHECK_EQ(source.Consumed(), std::size_t{ 4 });
}

TEST_CASE(Random_UniformRejectsTheLimitItself)
{
    const std::uint32_t bound = 62;
    const std::uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % bound);

    unsigned char script[8] = {};
    script[0] = static_cast<unsigned char>(limit & 0xFF);
    script[1] = static_cast<unsigned char>((limit >> 8) & 0xFF);
    script[2] = static_cast<unsigned char>((limit >> 16) & 0xFF);
    script[3] = static_cast<unsigned char>((limit >> 24) & 0xFF);
    script[4] = 0x07;

    ScriptedRandom source(script, sizeof(script));
    CHECK_EQ(source.Uniform(bound), 7u);
    CHECK_MSG(source.Consumed() == 8,
              "the value on the boundary was accepted ('<=' instead of '<')");
}

TEST_CASE(Random_UniformIsUnbiasedOverSmallBounds)
{
    DeterministicRandom r(12345);
    const std::uint32_t bound = 62;
    const int draws = 200000;
    std::vector<int> counts(bound, 0);
    for (int i = 0; i < draws; ++i)
        counts[r.Uniform(bound)]++;

    const double expected = static_cast<double>(draws) / bound;
    const double sigma = std::sqrt(expected * (1.0 - 1.0 / bound));
    int worst = 0;
    double worstDeviation = 0.0;
    for (std::uint32_t i = 0; i < bound; ++i)
    {
        const double deviation = std::abs(counts[i] - expected) / sigma;
        if (deviation > worstDeviation) { worstDeviation = deviation; worst = static_cast<int>(i); }
    }
    CHECK_MSG(worstDeviation < 5.0,
              "bucket " + std::to_string(worst) + " deviates " +
              std::to_string(worstDeviation) + " sigma");
    for (std::uint32_t i = 0; i < bound; ++i)
        CHECK_MSG(counts[i] > 0, "bucket " + std::to_string(i) + " stayed empty");
}

TEST_CASE(Generator_RespectsLengthAndAlphabet)
{
    DeterministicRandom r(1);
    GeneratorOptions o;
    o.Length = 24;
    const auto result = GeneratePassword(o, r);
    CHECK(result.IsOk());
    CHECK_EQ(result.Value().Text.size(), std::size_t{ 24 });
    CHECK(result.Value().AlphabetSize > 60);
}

TEST_CASE(Generator_RequiresEveryEnabledClass)
{
    DeterministicRandom r(9);
    GeneratorOptions o;
    o.Length = 8;
    o.RequireEveryClass = true;

    for (int run = 0; run < 200; ++run)
    {
        const auto result = GeneratePassword(o, r);
        CHECK(result.IsOk());
        const std::string& s = result.Value().Text;
        bool lower=false, upper=false, digit=false, symbol=false;
        for (char c : s)
        {
            if (c >= 'a' && c <= 'z') lower = true;
            else if (c >= 'A' && c <= 'Z') upper = true;
            else if (c >= '0' && c <= '9') digit = true;
            else symbol = true;
        }
        CHECK_MSG(lower && upper && digit && symbol,
                  "run " + std::to_string(run) + ": class missing in '" + s + "'");
    }
}

TEST_CASE(Generator_ImpossibleConstraintsFailInsteadOfHanging)
{
    DeterministicRandom r(1);
    GeneratorOptions o;
    o.Length = 3;
    o.RequireEveryClass = true;
    const auto result = GeneratePassword(o, r);
    CHECK(!result.IsOk());
    CHECK(result.Code() == ErrorCode::InvalidArgument);

    GeneratorOptions none;
    none.UseLower = none.UseUpper = none.UseDigits = none.UseSymbols = false;
    CHECK(!GeneratePassword(none, r).IsOk());

    GeneratorOptions zero;
    zero.Length = 0;
    CHECK(!GeneratePassword(zero, r).IsOk());
}

TEST_CASE(Generator_AmbiguousCharactersCanBeExcluded)
{
    DeterministicRandom r(3);
    GeneratorOptions o;
    o.Length = 64;
    o.AvoidAmbiguous = true;
    for (int run = 0; run < 40; ++run)
    {
        const auto result = GeneratePassword(o, r);
        CHECK(result.IsOk());
        for (char c : result.Value().Text)
            CHECK_MSG(std::strchr("0O1lI", c) == nullptr,
                      std::string("ambiguous character '") + c + "' generated");
    }
}

TEST_CASE(Generator_EntropyIsHonestAboutConstraints)
{
    DeterministicRandom r(5);
    GeneratorOptions loose;
    loose.Length = 8;
    loose.RequireEveryClass = false;

    GeneratorOptions strict = loose;
    strict.RequireEveryClass = true;

    const auto a = GeneratePassword(loose, r);
    const auto b = GeneratePassword(strict, r);
    CHECK(a.IsOk() && b.IsOk());

    CHECK_MSG(b.Value().EntropyBits < a.Value().EntropyBits,
              "the restricted variant does not report less entropy");

    GeneratorOptions longStrict = strict;
    longStrict.Length = 40;
    const auto c = GeneratePassword(longStrict, r);
    CHECK(c.IsOk());
    const double raw = 40.0 * std::log2(static_cast<double>(c.Value().AlphabetSize));
    CHECK(c.Value().EntropyBits <= raw);
    CHECK(c.Value().EntropyBits > raw - 1.0);
}

TEST_CASE(Generator_ProducesDifferentPasswordsFromTheSystemSource)
{
    GeneratorOptions o;
    o.Length = 20;
    const auto a = GeneratePassword(o, SystemRandom());
    const auto b = GeneratePassword(o, SystemRandom());
    CHECK(a.IsOk() && b.IsOk());
    CHECK(a.Value().Text != b.Value().Text);
}

TEST_CASE(Generator_EveryAlphabetPositionIsReachable)
{
    DeterministicRandom r(777);
    GeneratorOptions o;
    o.Length = 64;
    o.RequireEveryClass = false;

    std::map<char, int> seen;
    std::size_t alphabet = 0;
    for (int run = 0; run < 400; ++run)
    {
        const auto result = GeneratePassword(o, r);
        CHECK(result.IsOk());
        alphabet = result.Value().AlphabetSize;
        for (char c : result.Value().Text)
            seen[c]++;
    }
    CHECK_MSG(seen.size() == alphabet,
              "only " + std::to_string(seen.size()) + " of " +
              std::to_string(alphabet) + " alphabet characters generated");
}

TEST_CASE(Passphrase_HasTheRightShapeAndEntropy)
{
    DeterministicRandom r(11);
    const auto result = GeneratePassphrase(5, '-', r);
    CHECK(result.IsOk());

    int separators = 0;
    for (char c : result.Value().Text)
        if (c == '-') ++separators;
    CHECK_EQ(separators, 4);

    const double expected = 5.0 * std::log2(static_cast<double>(PassphraseWordCount()));
    CHECK(std::abs(result.Value().EntropyBits - expected) < 1e-9);
    CHECK(PassphraseWordCount() >= 200);
}

TEST_CASE(Passphrase_RejectsSillyWordCounts)
{
    DeterministicRandom r(1);
    CHECK(!GeneratePassphrase(0, '-', r).IsOk());
    CHECK(!GeneratePassphrase(1000, '-', r).IsOk());
    CHECK(GeneratePassphrase(1, '-', r).IsOk());
}

TEST_CASE(Passphrase_UsesTheWholeWordList)
{
    DeterministicRandom r(4242);
    std::map<std::string, int> seen;
    for (int run = 0; run < 4000; ++run)
    {
        const auto result = GeneratePassphrase(6, ' ', r);
        CHECK(result.IsOk());
        std::string word;
        for (char c : result.Value().Text)
        {
            if (c == ' ') { seen[word]++; word.clear(); }
            else word.push_back(c);
        }
        if (!word.empty()) seen[word]++;
    }
    CHECK_MSG(seen.size() == PassphraseWordCount(),
              "only " + std::to_string(seen.size()) + " of " +
              std::to_string(PassphraseWordCount()) + " words generated");
}
