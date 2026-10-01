#pragma once

namespace Bunker::UI
{
    struct GlyphInk
    {
        float X0 = 0.0f;
        float Y0 = 0.0f;
        float X1 = 0.0f;
        float Y1 = 0.0f;
    };

    struct Point
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    constexpr Point CentredIconPos(float minX, float minY, float maxX, float maxY,
                                   const GlyphInk& ink)
    {
        const float centreX = (minX + maxX) * 0.5f;
        const float centreY = (minY + maxY) * 0.5f;
        return Point{ centreX - (ink.X0 + ink.X1) * 0.5f,
                      centreY - (ink.Y0 + ink.Y1) * 0.5f };
    }

    inline Point SnapToPixels(Point p)
    {
        auto halfDown = [](float v) -> float
        {
            const float shifted = v - 0.5f;
            const auto truncated = static_cast<int>(shifted);
            const int up = (shifted > static_cast<float>(truncated))
                               ? truncated + 1
                               : truncated;
            return static_cast<float>(up);
        };
        return Point{ halfDown(p.X), halfDown(p.Y) };
    }
}
