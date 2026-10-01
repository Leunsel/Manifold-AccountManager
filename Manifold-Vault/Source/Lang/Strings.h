#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    enum class Str : std::uint16_t
    {
#define BUNKER_STRING(name, english) name,
#include "Lang/Strings.def"
#undef BUNKER_STRING
        Count,
    };

    struct Strings
    {
#define BUNKER_STRING(name, english) const char* name;
#include "Lang/Strings.def"
#undef BUNKER_STRING
    };

    inline constexpr Strings kEnglishStrings = {
#define BUNKER_STRING(name, english) english,
#include "Lang/Strings.def"
#undef BUNKER_STRING
    };

    inline constexpr const char* kStringName[] = {
#define BUNKER_STRING(name, english) #name,
#include "Lang/Strings.def"
#undef BUNKER_STRING
    };

    constexpr const char* Get(const Strings& table, Str id)
    {
        switch (id)
        {
#define BUNKER_STRING(name, english) case Str::name: return table.name;
#include "Lang/Strings.def"
#undef BUNKER_STRING
        default:
            return nullptr;
        }
    }

    static_assert(sizeof(kStringName) / sizeof(kStringName[0]) ==
                      static_cast<std::size_t>(Str::Count),
                  "the name table and the enumeration have drifted apart");
}
