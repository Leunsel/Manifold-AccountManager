#include "Core/AccentColour.h"

#include <cmath>

namespace Bunker
{
    namespace
    {
        double Channel(unsigned char v)
        {
            const double s = static_cast<double>(v) / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        }

        unsigned char Clamp255(double v)
        {
            if (v <= 0.0)
                return 0;
            if (v >= 1.0)
                return 255;
            return static_cast<unsigned char>(v * 255.0 + 0.5);
        }

        Rgb FromHsl(double h, double s, double l)
        {
            const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
            const double hp = h / 60.0;
            const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
            double r = 0.0, g = 0.0, b = 0.0;
            if (hp < 1.0)      { r = c; g = x; }
            else if (hp < 2.0) { r = x; g = c; }
            else if (hp < 3.0) { g = c; b = x; }
            else if (hp < 4.0) { g = x; b = c; }
            else if (hp < 5.0) { r = x; b = c; }
            else               { r = c; b = x; }
            const double m = l - c * 0.5;
            return Rgb{ Clamp255(r + m), Clamp255(g + m), Clamp255(b + m) };
        }

        std::uint32_t Fnv1a(const char* text)
        {
            std::uint32_t hash = 2166136261u;
            for (const char* p = text; *p != '\0'; ++p)
            {
                hash ^= static_cast<unsigned char>(*p);
                hash *= 16777619u;
            }
            return hash;
        }
    }

    double RelativeLuminance(Rgb c)
    {
        return 0.2126 * Channel(c.R) + 0.7152 * Channel(c.G) + 0.0722 * Channel(c.B);
    }

    double ContrastRatio(Rgb a, Rgb b)
    {
        const double la = RelativeLuminance(a);
        const double lb = RelativeLuminance(b);
        const double hi = la > lb ? la : lb;
        const double lo = la > lb ? lb : la;
        return (hi + 0.05) / (lo + 0.05);
    }

    Rgb ReadableFromHue(double hue)
    {
        constexpr double kSaturation = 0.52;

        for (double l = 0.56; l <= 0.90; l += 0.02)
        {
            const Rgb candidate = FromHsl(hue, kSaturation, l);
            if (ContrastRatio(candidate, kPanelColour) >= kMinContrast)
                return candidate;
        }
        return FromHsl(hue, kSaturation * 0.5, 0.90);
    }

    namespace
    {
        void ToHsl(Rgb c, double& h, double& s, double& l)
        {
            const double r = c.R / 255.0, g = c.G / 255.0, b = c.B / 255.0;
            const double hi = std::fmax(r, std::fmax(g, b));
            const double lo = std::fmin(r, std::fmin(g, b));
            l = (hi + lo) * 0.5;
            const double d = hi - lo;
            if (d <= 0.0)
            {
                h = 0.0;
                s = 0.0;
                return;
            }
            s = d / (1.0 - std::fabs(2.0 * l - 1.0));
            if (hi == r)
                h = 60.0 * std::fmod((g - b) / d, 6.0);
            else if (hi == g)
                h = 60.0 * ((b - r) / d + 2.0);
            else
                h = 60.0 * ((r - g) / d + 4.0);
            if (h < 0.0)
                h += 360.0;
        }
    }

    Rgb ReadableAgainst(Rgb colour, Rgb background, double minContrast)
    {
        if (ContrastRatio(colour, background) >= minContrast)
            return colour;

        double h = 0.0, s = 0.0, l = 0.0;
        ToHsl(colour, h, s, l);
        const bool darken = RelativeLuminance(background) > 0.179;
        for (int step = 1; step <= 50; ++step)
        {
            const double moved = darken ? l - 0.02 * step : l + 0.02 * step;
            if (moved < 0.0 || moved > 1.0)
                break;
            const Rgb candidate = FromHsl(h, s, moved);
            if (ContrastRatio(candidate, background) >= minContrast)
                return candidate;
        }
        return darken ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    }

    Rgb DerivedAccent(const char* platformId)
    {
        if (!platformId || *platformId == '\0')
            return Rgb{};

        return ReadableFromHue(static_cast<double>(Fnv1a(platformId) % 360u));
    }

    namespace
    {
        struct Tuned { const char* Id; std::uint32_t Colour; };

        constexpr Tuned kTuned[] = {
            { "epic-games", 0x6E7A86u },
            { "ubisoft",    0x3C8FC4u },
            { "rockstar",   0xE0A32Eu },
            { "gog",        0xA85BB0u },
            { "nintendo",   0xD14B46u },
            { "ea",         0x7C8C9Eu },
        };

        bool SameId(const char* a, const char* b)
        {
            while (*a && *b && *a == *b) { ++a; ++b; }
            return *a == *b;
        }
    }

    std::uint32_t TunedAccent(const char* platformId)
    {
        if (!platformId || *platformId == '\0')
            return 0;
        for (const Tuned& t : kTuned)
            if (SameId(platformId, t.Id))
                return t.Colour;
        return 0;
    }

    std::uint32_t ResolveAccent(std::uint32_t own, const char* platformId)
    {
        if (const std::uint32_t chosen = own & 0x00FFFFFFu)
            return chosen;
        if (const std::uint32_t tuned = TunedAccent(platformId))
            return tuned;
        return DerivedAccent(platformId).Packed();
    }

    std::uint32_t ResolveAccent(std::uint32_t own, const char* platformId, const char* symbol)
    {
        const bool hasPlatform = platformId && *platformId;
        return ResolveAccent(own, hasPlatform ? platformId : (symbol ? symbol : ""));
    }
}
