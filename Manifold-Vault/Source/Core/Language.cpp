#include "Core/Language.h"

#include <cstring>

namespace Bunker
{
    namespace
    {
        const Lang::LanguageTable* g_current = &Lang::kLanguages[0];

        char Lower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool CodeMatches(const char* wanted, const char* table)
        {
            std::size_t i = 0;
            for (; wanted[i] != '\0' && wanted[i] != '-' && wanted[i] != '_'; ++i)
            {
                if (table[i] == '\0' || Lower(wanted[i]) != Lower(table[i]))
                    return false;
            }
            return table[i] == '\0';
        }
    }

    const char* T(Str id)
    {
        if (const char* text = Get(*g_current->Text, id))
            if (*text != '\0')
                return text;
        if (const char* english = Get(kEnglishStrings, id))
            return english;
        return "";
    }

    const Lang::LanguageTable& SetLanguage(const char* code)
    {
        if (code && *code)
        {
            for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
            {
                if (CodeMatches(code, Lang::kLanguages[i].Code))
                {
                    g_current = &Lang::kLanguages[i];
                    return *g_current;
                }
            }
        }
        g_current = &Lang::kLanguages[0];
        return *g_current;
    }

    const Lang::LanguageTable& CurrentLanguage() { return *g_current; }

    const char* const* PassphraseWords(std::size_t& count)
    {
        count = g_current->WordCount;
        return g_current->Words;
    }
}
