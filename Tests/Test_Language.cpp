#include "TestRunner.h"

#include "Core/Language.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace Bunker;

namespace
{
    std::vector<std::string> Specifiers(const char* text)
    {
        std::vector<std::string> found;
        for (std::size_t i = 0; text[i] != '\0';)
        {
            if (text[i] != '%')
            {
                ++i;
                continue;
            }
            if (text[i + 1] == '%')
            {
                i += 2;
                continue;
            }
            const std::size_t start = i;
            ++i;
            while (text[i] != '\0' &&
                   std::strchr("-+ #0123456789.*hlLjzt", text[i]) != nullptr)
            {
                ++i;
            }
            if (text[i] == '\0')
            {
                found.push_back(text + start);
                break;
            }
            found.push_back(std::string(text + start, i - start + 1));
            ++i;
        }
        return found;
    }
}

TEST_CASE(Lang_EveryTranslationTakesTheSameArguments)
{
    for (std::size_t l = 0; l < Lang::kLanguageCount; ++l)
    {
        const Lang::LanguageTable& lang = Lang::kLanguages[l];
        for (std::size_t i = 0; i < static_cast<std::size_t>(Str::Count); ++i)
        {
            const Str id = static_cast<Str>(i);
            const char* translated = Get(*lang.Text, id);
            if (!translated || *translated == '\0')
                continue;

            const char* english = Get(kEnglishStrings, id);
            std::string what = std::string(lang.Code) + " / " + kStringName[i];
            CHECK_MSG(Specifiers(english) == Specifiers(translated), what.c_str());
        }
    }
}

TEST_CASE(Lang_NoTranslationWritesThroughAPointer)
{
    for (std::size_t l = 0; l < Lang::kLanguageCount; ++l)
    {
        const Lang::LanguageTable& lang = Lang::kLanguages[l];
        for (std::size_t i = 0; i < static_cast<std::size_t>(Str::Count); ++i)
        {
            const char* text = Get(*lang.Text, static_cast<Str>(i));
            if (!text)
                continue;
            for (const std::string& spec : Specifiers(text))
            {
                std::string what = std::string(lang.Code) + " / " + kStringName[i];
                CHECK_MSG(spec.back() != 'n', what.c_str());
            }
        }
    }
}

TEST_CASE(Lang_EnglishIsCompleteByConstruction)
{
    SetLanguage("en");
    for (std::size_t i = 0; i < static_cast<std::size_t>(Str::Count); ++i)
    {
        const char* text = T(static_cast<Str>(i));
        CHECK_MSG(text != nullptr && *text != '\0', kStringName[i]);
    }
}

TEST_CASE(Lang_AMissingTranslationFallsBackPerString)
{
    SetLanguage("de");
    for (std::size_t i = 0; i < static_cast<std::size_t>(Str::Count); ++i)
    {
        const char* shown = T(static_cast<Str>(i));
        CHECK_MSG(shown != nullptr && *shown != '\0', kStringName[i]);
    }
    SetLanguage("en");
}

TEST_CASE(Lang_CodesAreMatchedLooselyEnoughToBeUseful)
{
    CHECK(std::strcmp(SetLanguage("de").Code, "de") == 0);
    CHECK(std::strcmp(SetLanguage("DE").Code, "de") == 0);
    CHECK(std::strcmp(SetLanguage("de-AT").Code, "de") == 0);
    CHECK(std::strcmp(SetLanguage("de_DE").Code, "de") == 0);

    CHECK(std::strcmp(SetLanguage("fr").Code, "en") == 0);
    CHECK(std::strcmp(SetLanguage("").Code, "en") == 0);
    CHECK(std::strcmp(SetLanguage(nullptr).Code, "en") == 0);
    CHECK_MSG(std::strcmp(SetLanguage("d").Code, "en") == 0,
              "a prefix matched a language it is not");
    SetLanguage("en");
}

TEST_CASE(Lang_EveryLanguageBringsAUsableWordList)
{
    for (std::size_t l = 0; l < Lang::kLanguageCount; ++l)
    {
        const Lang::LanguageTable& lang = Lang::kLanguages[l];
        CHECK_MSG(lang.WordCount >= kMinPassphraseWords, lang.Code);

        std::vector<std::string> words(lang.Words, lang.Words + lang.WordCount);
        for (const std::string& w : words)
        {
            CHECK_MSG(!w.empty(), lang.Code);
            CHECK_MSG(w.find(' ') == std::string::npos, lang.Code);
            for (const char c : w)
                CHECK_MSG(static_cast<unsigned char>(c) < 0x80, lang.Code);
        }

        std::vector<std::string> sorted = words;
        std::sort(sorted.begin(), sorted.end());
        for (std::size_t i = 1; i < sorted.size(); ++i)
        {
            std::string what = std::string(lang.Code) + ": " + sorted[i];
            CHECK_MSG(sorted[i] != sorted[i - 1], what.c_str());
        }
    }
}

TEST_CASE(Lang_TheListNamesEnglishFirst)
{
    CHECK(std::strcmp(Lang::kLanguages[0].Code, "en") == 0);

    for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
    {
        CHECK_MSG(Lang::kLanguages[i].Name != nullptr, Lang::kLanguages[i].Code);
        for (std::size_t j = i + 1; j < Lang::kLanguageCount; ++j)
            CHECK_MSG(std::strcmp(Lang::kLanguages[i].Code,
                                  Lang::kLanguages[j].Code) != 0,
                      Lang::kLanguages[i].Code);
    }
}

TEST_CASE(Lang_EveryStringHasAGermanText)
{
    for (std::size_t i = 0; i < static_cast<std::size_t>(Str::Count); ++i)
    {
        const char* text = Get(Lang::kGerman, static_cast<Str>(i));
        CHECK_MSG(text != nullptr && *text != '\0', kStringName[i]);
    }
}
