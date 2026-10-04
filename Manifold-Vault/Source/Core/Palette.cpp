#include "Core/Palette.h"

#include "Core/AccentColour.h"

namespace Bunker
{
    namespace
    {
        constexpr Palette kPalettes[] = {
            { "arc", false,
              0x0A0C0E, 0x101418, 0x161B21, 0x1C222A, 0x0C0F13,
              0x232A33, 0x2E3742,
              0xE6EAEF, 0xA3AEBB, 0x7C8895, 0x566270,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x0A0C0E,
              0xE5484D, 0x7A2326 },

            { "red", false,
              0x0B0C0D, 0x131416, 0x191A1D, 0x202226, 0x0E0F11,
              0x2A2C30, 0x373A3F,
              0xE8E9EB, 0xACAFB5, 0x858991, 0x5B5F66,
              0xEF4F6C, 0xFF8197, 0x7A2537,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x050506,
              0xF08A4B, 0x7A3A1E },

            { "red-oled", false,
              0x000000, 0x000000, 0x000000, 0x0A0A0A, 0x000000,
              0x242424, 0x333333,
              0xFAFAFA, 0xA1A1A1, 0x8F8F8F, 0x5A5A5A,
              0xEF4F6C, 0xFF8197, 0x7A2537,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x000000,
              0xF08A4B, 0x7A3A1E },

            { "red-light", true,
              0xDDE1E6, 0xEEF0F3, 0xF8F9FB, 0xFFFFFF, 0xFFFFFF,
              0xD6DBE1, 0xC2C9D1,
              0x1A1E23, 0x464E58, 0x5F6873, 0x98A0AA,
              0xC8234A, 0xA3193B, 0xF7CDD7,
              0x9A5B00, 0x7F4A00, 0xF0D9B5,
              0xC42B2F, 0xA51F24, 0xF3C5C6,
              0x1B7F37, 0xCDEBD5,
              0x3A414B,
              0xD9772F, 0xF9DCC6 },

            { "green", false,
              0x0B0C0D, 0x131416, 0x191A1D, 0x202226, 0x0E0F11,
              0x2A2C30, 0x373A3F,
              0xE8E9EB, 0xACAFB5, 0x858991, 0x5B5F66,
              0x3DD68C, 0x74EDB0, 0x16683F,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x050506,
              0x2FB8B0, 0x135A55 },

            { "green-oled", false,
              0x000000, 0x000000, 0x000000, 0x0A0A0A, 0x000000,
              0x242424, 0x333333,
              0xFAFAFA, 0xA1A1A1, 0x8F8F8F, 0x5A5A5A,
              0x3DD68C, 0x74EDB0, 0x16683F,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x000000,
              0x2FB8B0, 0x135A55 },

            { "green-light", true,
              0xDDE1E6, 0xEEF0F3, 0xF8F9FB, 0xFFFFFF, 0xFFFFFF,
              0xD6DBE1, 0xC2C9D1,
              0x1A1E23, 0x464E58, 0x5F6873, 0x98A0AA,
              0x15874F, 0x0E6E3F, 0xC9EDD8,
              0x9A5B00, 0x7F4A00, 0xF0D9B5,
              0xC42B2F, 0xA51F24, 0xF3C5C6,
              0x1B7F37, 0xCDEBD5,
              0x3A414B,
              0x1F9A93, 0xCDEDEA },

            { "dark", false,
              0x0B0C0D, 0x131416, 0x191A1D, 0x202226, 0x0E0F11,
              0x2A2C30, 0x373A3F,
              0xE8E9EB, 0xACAFB5, 0x858991, 0x5B5F66,
              0x5B9BF0, 0x86B8FF, 0x2D4D78,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x050506,
              0x8E6BF0, 0x47347A },

            { "blue-oled", false,
              0x000000, 0x000000, 0x000000, 0x0A0A0A, 0x000000,
              0x242424, 0x333333,
              0xFAFAFA, 0xA1A1A1, 0x8F8F8F, 0x5A5A5A,
              0x5B9BF0, 0x86B8FF, 0x2D4D78,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x000000,
              0x8E6BF0, 0x47347A },

            { "light", true,
              0xDDE1E6, 0xEEF0F3, 0xF8F9FB, 0xFFFFFF, 0xFFFFFF,
              0xD6DBE1, 0xC2C9D1,
              0x1A1E23, 0x464E58, 0x5F6873, 0x98A0AA,
              0x1F66C9, 0x1552A6, 0xC6DBF5,
              0x9A5B00, 0x7F4A00, 0xF0D9B5,
              0xC42B2F, 0xA51F24, 0xF3C5C6,
              0x1B7F37, 0xCDEBD5,
              0x3A414B,
              0x6A4BC9, 0xDDD3F5 },

            { "mint", false,
              0x070C0D, 0x0A1113, 0x0D1719, 0x132125, 0x081012,
              0x1A2A2E, 0x24393E,
              0xE4EEEC, 0xA4B6B2, 0x7A8F8B, 0x4F625E,
              0x2FD9AE, 0x6CF0CE, 0x135C4A,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x040708,
              0x2BA8D9, 0x12465C },

            { "midnight", false,
              0x0A0C18, 0x0E1120, 0x131729, 0x1A1F35, 0x0A0D19,
              0x232A45, 0x2E3657,
              0xE6E9F5, 0xA6ACC8, 0x838AAB, 0x555B78,
              0x8C9EFF, 0xB0BCFF, 0x3B4580,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x05060C,
              0xB27CF5, 0x4F3578 },

            { "terminal", false,
              0x000000, 0x040A06, 0x07120A, 0x0C1B10, 0x020503,
              0x17361F, 0x23502E,
              0x9BF7B4, 0x5FD382, 0x46A865, 0x2C6B3F,
              0x3BF07A, 0xA6FFC2, 0x1B5A30,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x000000,
              0x1FA650, 0x0D3F1F,
              true, true },
        };

        bool SameId(std::string_view a, const char* b)
        {
            return a == std::string_view(b);
        }

        // Stored in vault headers: never change or reuse a number.
        struct HeaderCode
        {
            const char* Id;
            std::uint8_t Code;
        };

        constexpr HeaderCode kHeaderCodes[] = {
            { "arc", 1 },        { "dark", 2 },        { "light", 3 },       { "black", 4 },
            { "fjord", 5 },      { "mint", 6 },        { "aubergine", 7 },   { "midnight", 8 },
            { "moss", 9 },       { "terminal", 10 },   { "red", 11 },        { "red-oled", 12 },
            { "red-light", 13 }, { "green", 14 },      { "green-oled", 15 }, { "green-light", 16 },
            { "blue-oled", 17 }, { "red-auto", 18 },     { "green-auto", 19 }, { "blue-auto", 20 },
        };

        struct ThemeFamily
        {
            const char* Ids[kThemeModeCount];
        };

        // Order of the colour choice in the settings; null: mode not offered.
        constexpr ThemeFamily kFamilies[] = {
            { { "arc", nullptr, nullptr, nullptr } },
            { { "red", "red-oled", "red-light", "red-auto" } },
            { { "green", "green-oled", "green-light", "green-auto" } },
            { { "dark", "blue-oled", "light", "blue-auto" } },
            { { "mint", nullptr, nullptr, nullptr } },
            { { "midnight", nullptr, nullptr, nullptr } },
            { { "terminal", nullptr, nullptr, nullptr } },
        };

        bool FindChoice(std::string_view id, ThemeChoice& out)
        {
            for (std::size_t f = 0; f < sizeof(kFamilies) / sizeof(kFamilies[0]); ++f)
                for (std::size_t m = 0; m < kThemeModeCount; ++m)
                    if (kFamilies[f].Ids[m] && SameId(id, kFamilies[f].Ids[m]))
                    {
                        out = { f, static_cast<ThemeMode>(m) };
                        return true;
                    }
            return false;
        }

        // Retired schemes: ids stored in vaults resolve to the closest current one.
        struct Retired
        {
            const char* Id;
            const char* Now;
        };

        constexpr Retired kRetired[] = {
            { "black", "blue-oled" }, { "fjord", "mint" }, { "aubergine", "midnight" }, { "moss", "green" },
        };
    }

    std::size_t PaletteCount()
    {
        return sizeof(kPalettes) / sizeof(kPalettes[0]);
    }

    const Palette& PaletteAt(std::size_t index)
    {
        return index < PaletteCount() ? kPalettes[index] : kPalettes[0];
    }

    std::size_t PaletteIndex(std::string_view id)
    {
        for (std::size_t i = 0; i < PaletteCount(); ++i)
            if (SameId(id, kPalettes[i].Id))
                return i;
        for (const Retired& r : kRetired)
            if (SameId(id, r.Id))
                return PaletteIndex(r.Now);
        return 0;
    }

    std::uint8_t PaletteHeaderCode(std::string_view id)
    {
        for (const HeaderCode& h : kHeaderCodes)
            if (SameId(id, h.Id))
                return h.Code;
        return 0;
    }

    std::string_view PaletteIdForHeaderCode(std::uint8_t code)
    {
        if (code == 0)
            return {};
        for (const HeaderCode& h : kHeaderCodes)
            if (h.Code == code)
                return h.Id;
        return {};
    }

    std::size_t ThemeFamilyCount()
    {
        return sizeof(kFamilies) / sizeof(kFamilies[0]);
    }

    bool ThemeModeAvailable(std::size_t family, ThemeMode mode)
    {
        return family < ThemeFamilyCount() &&
               kFamilies[family].Ids[static_cast<std::size_t>(mode)] != nullptr;
    }

    ThemeChoice ThemeChoiceOf(std::string_view id)
    {
        ThemeChoice choice;
        if (!FindChoice(id, choice))
            FindChoice(PaletteAt(PaletteIndex(id)).Id, choice);
        return choice;
    }

    std::string_view ThemeIdOf(ThemeChoice choice)
    {
        if (choice.Family >= ThemeFamilyCount())
            return PaletteAt(0).Id;
        const ThemeFamily& family = kFamilies[choice.Family];
        const char* id = family.Ids[static_cast<std::size_t>(choice.Mode)];
        return id ? id : family.Ids[0];
    }

    std::string_view ResolvePaletteId(std::string_view id, bool systemLight)
    {
        ThemeChoice choice;
        if (!FindChoice(id, choice) || choice.Mode != ThemeMode::System)
            return id;
        return ThemeIdOf({ choice.Family, systemLight ? ThemeMode::Light : ThemeMode::Dark });
    }

    bool SeparatesByTone(const Palette& palette)
    {
        return ContrastRatio(Rgb::FromPacked(palette.Panel),
                             Rgb::FromPacked(palette.Surface)) >= 1.03;
    }
}
