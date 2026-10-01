#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    struct Rgb
    {
        unsigned char R = 0;
        unsigned char G = 0;
        unsigned char B = 0;

        constexpr std::uint32_t Packed() const
        {
            return (static_cast<std::uint32_t>(R) << 16) |
                   (static_cast<std::uint32_t>(G) << 8) |
                   static_cast<std::uint32_t>(B);
        }

        static constexpr Rgb FromPacked(std::uint32_t v)
        {
            return Rgb{ static_cast<unsigned char>((v >> 16) & 0xFF),
                        static_cast<unsigned char>((v >> 8) & 0xFF),
                        static_cast<unsigned char>(v & 0xFF) };
        }
    };

    inline constexpr Rgb kPanelColour{ 0x16, 0x1B, 0x21 };

    inline constexpr double kMinContrast = 3.5;

    double RelativeLuminance(Rgb c);

    double ContrastRatio(Rgb a, Rgb b);

    Rgb DerivedAccent(const char* platformId);

    Rgb ReadableFromHue(double hue);

    Rgb ReadableAgainst(Rgb colour, Rgb background, double minContrast = kMinContrast);

    std::uint32_t TunedAccent(const char* platformId);

    std::uint32_t ResolveAccent(std::uint32_t own, const char* platformId);

    std::uint32_t ResolveAccent(std::uint32_t own, const char* platformId, const char* symbol);
}
