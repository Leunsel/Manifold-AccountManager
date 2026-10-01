#pragma once

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Bunker::Theme
{
    enum class Token
    {
        Void,
        Surface,
        Panel,
        Elevated,
        Inset,

        Line,
        LineStrong,

        TextPrimary,
        TextSecondary,
        TextMuted,
        TextDisabled,

        AccentCore,
        AccentBright,
        AccentDim,

        WarnCore,
        WarnBright,
        WarnDim,

        RedCore,
        RedBright,
        RedDim,

        GreenCore,
        GreenDim,

        Shade,

        GlowCore,
        GlowDim,

        Count
    };

    void SetActive(std::string_view paletteId);
    std::size_t ActiveIndex();
    bool IsLight();

    bool SeparatesByTone();

    float Round(float radius);
    bool IsMonospace();

    ImU32 AccentOnPanel(std::uint32_t packedRgb);

    void SetColouredIcons(bool on);
    bool ColouredIcons();

    ImVec4 Color(Token t);

    ImVec4 Color(Token t, float alpha);

    ImU32 U32(Token t);
    ImU32 U32(Token t, float alpha);

    float RelativeLuminance(ImVec4 c);
    ImU32 ReadableTextOn(ImVec4 background);

    void Apply(ImGuiStyle& style);

    void ApplyColours(ImGuiStyle& style);

    void ApplyShape(ImGuiStyle& style);
}
