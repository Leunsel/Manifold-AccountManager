#pragma once

#include "imgui.h"

namespace Bunker::UI
{
    inline constexpr float kPi = 3.14159265358979323846f;
    inline constexpr float kTwoPi = kPi * 2.0f;

    template <typename T>
    constexpr T Min(T a, T b) { return a < b ? a : b; }

    template <typename T>
    constexpr T Max(T a, T b) { return a > b ? a : b; }

    template <typename T>
    constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

    constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    constexpr ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
    constexpr ImVec2 Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
}
