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

            { "black", false,
              0x000000, 0x000000, 0x000000, 0x0A0A0A, 0x000000,
              0x242424, 0x333333,
              0xFAFAFA, 0xA1A1A1, 0x8F8F8F, 0x5A5A5A,
              0xEDEDED, 0xFFFFFF, 0x3A3A3A,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x000000,
              0x8F8F8F, 0x2A2A2A },

            { "fjord", false,
              0x080D0F, 0x0F1618, 0x142024, 0x1A292E, 0x0B1214,
              0x21343A, 0x2C444B,
              0xE3EEF0, 0xA2B9BE, 0x7A9398, 0x52686D,
              0x3CC7C0, 0x72E0DA, 0x1D5F5B,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x050809,
              0x3D8BD9, 0x1C3F63 },

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

            { "aubergine", false,
              0x0D0A10, 0x141018, 0x1B1521, 0x231B2B, 0x100C14,
              0x2E2438, 0x3B2F48,
              0xECE6F2, 0xB3A8BF, 0x9084A0, 0x5F5469,
              0xB48CFF, 0xCDB0FF, 0x553E80,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x080609,
              0xE06AB8, 0x6B2E58 },

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

            { "moss", false,
              0x0C0F0B, 0x111510, 0x171C15, 0x1E251B, 0x0C0F0B,
              0x283024, 0x343E2F,
              0xE7ECE3, 0xAAB5A2, 0x828E7A, 0x596352,
              0xA3C46A, 0xC2DE8C, 0x4D5E30,
              0xE8A33D, 0xFFC266, 0x8A6224,
              0xE5484D, 0xFF6369, 0x7A2326,
              0x3FB950, 0x1F4B27,
              0x070906,
              0xD4B04F, 0x5C4C22 },

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
            { "arc", 1 },   { "dark", 2 },      { "light", 3 },     { "black", 4 },
            { "fjord", 5 }, { "mint", 6 },      { "aubergine", 7 }, { "midnight", 8 },
            { "moss", 9 },  { "terminal", 10 },
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

    bool SeparatesByTone(const Palette& palette)
    {
        return ContrastRatio(Rgb::FromPacked(palette.Panel),
                             Rgb::FromPacked(palette.Surface)) >= 1.03;
    }
}
