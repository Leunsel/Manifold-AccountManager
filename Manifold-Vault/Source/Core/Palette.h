#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Bunker
{
    struct Palette
    {
        const char* Id;   // stored in vaults: never rename
        bool Light;

        std::uint32_t Void, Surface, Panel, Elevated, Inset;
        std::uint32_t Line, LineStrong;
        std::uint32_t TextPrimary, TextSecondary, TextMuted, TextDisabled;
        std::uint32_t AccentCore, AccentBright, AccentDim;
        std::uint32_t WarnCore, WarnBright, WarnDim;
        std::uint32_t RedCore, RedBright, RedDim;
        std::uint32_t GreenCore, GreenDim;
        std::uint32_t Shade;

        std::uint32_t GlowCore, GlowDim;

        bool Square = false;
        bool Monospace = false;
    };

    std::size_t PaletteCount();
    const Palette& PaletteAt(std::size_t index);

    std::size_t PaletteIndex(std::string_view id);

    std::uint8_t PaletteHeaderCode(std::string_view id);

    std::string_view PaletteIdForHeaderCode(std::uint8_t code);

    bool SeparatesByTone(const Palette& palette);
}
