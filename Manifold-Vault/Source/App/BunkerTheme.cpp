#include "BunkerTheme.h"

#include "Core/AccentColour.h"
#include "Core/Palette.h"

#include <cmath>

namespace Bunker::Theme
{
    namespace
    {
        std::size_t g_active = 0;

        std::uint32_t Hex(Token t)
        {
            const Palette& p = PaletteAt(g_active);
            switch (t)
            {
            case Token::Void:          return p.Void;
            case Token::Surface:       return p.Surface;
            case Token::Panel:         return p.Panel;
            case Token::Elevated:      return p.Elevated;
            case Token::Inset:         return p.Inset;
            case Token::Line:          return p.Line;
            case Token::LineStrong:    return p.LineStrong;
            case Token::TextPrimary:   return p.TextPrimary;
            case Token::TextSecondary: return p.TextSecondary;
            case Token::TextMuted:     return p.TextMuted;
            case Token::TextDisabled:  return p.TextDisabled;
            case Token::AccentCore:    return p.AccentCore;
            case Token::AccentBright:  return p.AccentBright;
            case Token::AccentDim:     return p.AccentDim;
            case Token::WarnCore:      return p.WarnCore;
            case Token::WarnBright:    return p.WarnBright;
            case Token::WarnDim:       return p.WarnDim;
            case Token::RedCore:       return p.RedCore;
            case Token::RedBright:     return p.RedBright;
            case Token::RedDim:        return p.RedDim;
            case Token::GreenCore:     return p.GreenCore;
            case Token::GreenDim:      return p.GreenDim;
            case Token::Shade:         return p.Shade;
            case Token::GlowCore:      return p.GlowCore;
            case Token::GlowDim:       return p.GlowDim;
            case Token::Count:         break;
            }
            return p.TextPrimary;
        }

        constexpr float kInv255 = 1.0f / 255.0f;

        ImVec4 FromHex(unsigned int rgb, float a)
        {
            return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) * kInv255,
                          static_cast<float>((rgb >> 8) & 0xFF) * kInv255,
                          static_cast<float>(rgb & 0xFF) * kInv255,
                          a);
        }

        float Linearize(float channel)
        {
            return (channel <= 0.04045f) ? (channel / 12.92f)
                                         : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        }
    }

    ImVec4 Color(Token t) { return FromHex(Hex(t), 1.0f); }
    ImVec4 Color(Token t, float alpha) { return FromHex(Hex(t), alpha); }

    void SetActive(std::string_view paletteId) { g_active = PaletteIndex(paletteId); }
    std::size_t ActiveIndex() { return g_active; }
    bool IsLight() { return PaletteAt(g_active).Light; }
    bool SeparatesByTone() { return Bunker::SeparatesByTone(PaletteAt(g_active)); }
    float Round(float radius) { return PaletteAt(g_active).Square ? 0.0f : radius; }
    bool IsMonospace() { return PaletteAt(g_active).Monospace; }

    ImU32 AccentOnPanel(std::uint32_t packedRgb)
    {
        Rgb rgb = Rgb::FromPacked(packedRgb);
        if (IsLight())
            rgb = ReadableAgainst(rgb, Rgb::FromPacked(PaletteAt(g_active).Panel));
        return IM_COL32(rgb.R, rgb.G, rgb.B, 255);
    }

    namespace
    {
        bool g_colouredIcons = true;
    }
    void SetColouredIcons(bool on) { g_colouredIcons = on; }
    bool ColouredIcons() { return g_colouredIcons; }

    ImU32 U32(Token t) { return ImGui::ColorConvertFloat4ToU32(Color(t)); }
    ImU32 U32(Token t, float alpha) { return ImGui::ColorConvertFloat4ToU32(Color(t, alpha)); }

    float RelativeLuminance(ImVec4 c)
    {
        return 0.2126f * Linearize(c.x) + 0.7152f * Linearize(c.y) + 0.0722f * Linearize(c.z);
    }

    ImU32 ReadableTextOn(ImVec4 background)
    {
        return (RelativeLuminance(background) > 0.179f) ? IM_COL32(10, 12, 14, 255)
                                                        : IM_COL32(255, 255, 255, 255);
    }

    void Apply(ImGuiStyle& style)
    {
        ApplyShape(style);

        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBorderSize = 0.0f;
        style.SeparatorTextBorderSize = 1.0f;

        style.WindowPadding = ImVec2(16, 14);
        style.FramePadding = ImVec2(10, 6);
        style.CellPadding = ImVec2(8, 5);
        style.ItemSpacing = ImVec2(8, 6);
        style.ItemInnerSpacing = ImVec2(7, 5);
        style.IndentSpacing = 18.0f;
        style.ScrollbarSize = 10.0f;
        style.GrabMinSize = 12.0f;

        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(16, 6);

        style.DisabledAlpha = 0.45f;
        style.CircleTessellationMaxError = 0.20f;
        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;
        style.AntiAliasedFill = true;

        ApplyColours(style);
    }

    void ApplyShape(ImGuiStyle& style)
    {
        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.PopupRounding = Round(6.0f);
        style.FrameRounding = Round(4.0f);
        style.GrabRounding = Round(3.0f);
        style.TabRounding = Round(4.0f);
        style.ScrollbarRounding = Round(6.0f);
    }

    void ApplyColours(ImGuiStyle& style)
    {
        ImVec4* c = style.Colors;

        c[ImGuiCol_Text]                   = Color(Token::TextPrimary);
        c[ImGuiCol_TextDisabled]           = Color(Token::TextDisabled);
        c[ImGuiCol_WindowBg]               = Color(Token::Surface);
        c[ImGuiCol_ChildBg]                = Color(Token::Panel);
        c[ImGuiCol_PopupBg]                = Color(Token::Elevated);
        c[ImGuiCol_Border]                 = Color(Token::Line);
        c[ImGuiCol_BorderShadow]           = Color(Token::Void, 0.0f);

        c[ImGuiCol_FrameBg]                = Color(Token::Inset);
        c[ImGuiCol_FrameBgHovered]         = Color(Token::Elevated);
        c[ImGuiCol_FrameBgActive]          = Color(Token::Elevated);

        c[ImGuiCol_TitleBg]                = Color(Token::Panel);
        c[ImGuiCol_TitleBgActive]          = Color(Token::Panel);
        c[ImGuiCol_TitleBgCollapsed]       = Color(Token::Panel, 0.75f);
        c[ImGuiCol_MenuBarBg]              = Color(Token::Panel);

        c[ImGuiCol_ScrollbarBg]            = Color(Token::Void, 0.0f);
        c[ImGuiCol_ScrollbarGrab]          = Color(Token::LineStrong);
        c[ImGuiCol_ScrollbarGrabHovered]   = Color(Token::TextDisabled);
        c[ImGuiCol_ScrollbarGrabActive]    = Color(Token::AccentCore);

        c[ImGuiCol_CheckMark]              = Color(Token::AccentCore);
        c[ImGuiCol_CheckboxSelectedBg]     = Color(Token::AccentCore, 0.18f);
        c[ImGuiCol_SliderGrab]             = Color(Token::AccentDim);
        c[ImGuiCol_SliderGrabActive]       = Color(Token::AccentCore);

        c[ImGuiCol_Button]                 = Color(Token::Void, 0.0f);
        c[ImGuiCol_ButtonHovered]          = Color(Token::AccentCore, 0.14f);
        c[ImGuiCol_ButtonActive]           = Color(Token::AccentCore, 0.24f);

        c[ImGuiCol_Header]                 = Color(Token::AccentCore, 0.12f);
        c[ImGuiCol_HeaderHovered]          = Color(Token::AccentCore, 0.135f);
        c[ImGuiCol_HeaderActive]           = Color(Token::AccentCore, 0.26f);

        c[ImGuiCol_Separator]              = Color(Token::Line);
        c[ImGuiCol_SeparatorHovered]       = Color(Token::AccentDim);
        c[ImGuiCol_SeparatorActive]        = Color(Token::AccentCore);

        c[ImGuiCol_ResizeGrip]             = Color(Token::Line);
        c[ImGuiCol_ResizeGripHovered]      = Color(Token::AccentDim);
        c[ImGuiCol_ResizeGripActive]       = Color(Token::AccentCore);

        c[ImGuiCol_InputTextCursor]        = Color(Token::AccentBright);

        c[ImGuiCol_Tab]                    = Color(Token::Void, 0.0f);
        c[ImGuiCol_TabHovered]             = Color(Token::AccentCore, 0.16f);
        c[ImGuiCol_TabSelected]            = Color(Token::Elevated);
        c[ImGuiCol_TabSelectedOverline]    = Color(Token::AccentCore);
        c[ImGuiCol_TabDimmed]              = Color(Token::Void, 0.0f);
        c[ImGuiCol_TabDimmedSelected]      = Color(Token::Panel);
        c[ImGuiCol_TabDimmedSelectedOverline] = Color(Token::AccentDim);

        c[ImGuiCol_DockingPreview]         = Color(Token::AccentCore, 0.30f);
        c[ImGuiCol_DockingEmptyBg]         = Color(Token::Void);

        c[ImGuiCol_PlotLines]              = Color(Token::TextMuted);
        c[ImGuiCol_PlotLinesHovered]       = Color(Token::AccentBright);
        c[ImGuiCol_PlotHistogram]          = Color(Token::AccentCore);
        c[ImGuiCol_PlotHistogramHovered]   = Color(Token::AccentBright);

        c[ImGuiCol_TableHeaderBg]          = Color(Token::Panel);
        c[ImGuiCol_TableBorderStrong]      = Color(Token::LineStrong);
        c[ImGuiCol_TableBorderLight]       = Color(Token::Line);
        c[ImGuiCol_TableRowBg]             = Color(Token::Void, 0.0f);
        c[ImGuiCol_TableRowBgAlt]          = Color(Token::Elevated, 0.35f);

        c[ImGuiCol_TextLink]               = Color(Token::AccentBright);
        c[ImGuiCol_TextSelectedBg]         = Color(Token::AccentCore, 0.28f);
        c[ImGuiCol_TreeLines]              = Color(Token::Line);

        c[ImGuiCol_DragDropTarget]         = Color(Token::AccentBright);
        c[ImGuiCol_DragDropTargetBg]       = Color(Token::AccentCore, 0.12f);
        c[ImGuiCol_UnsavedMarker]          = Color(Token::AccentCore);

        c[ImGuiCol_NavCursor]              = Color(Token::AccentBright);
        c[ImGuiCol_NavWindowingHighlight]  = Color(Token::AccentBright, 0.70f);
        c[ImGuiCol_NavWindowingDimBg]      = Color(Token::Shade, 0.55f);
        c[ImGuiCol_ModalWindowDimBg]       = Color(Token::Shade, 0.68f);
    }
}
