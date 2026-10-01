#include "UI/Tooltip.h"
#include "UI/IconButton.h"

#include "UI/IconGeometry.h"

namespace Bunker::UI
{
    using Theme::Token;

    namespace
    {
        unsigned int FirstCodepoint(const char* text)
        {
            if (!text || !*text)
                return 0;
            const auto* p = reinterpret_cast<const unsigned char*>(text);
            const unsigned char b0 = p[0];

            if (b0 < 0x80)
                return b0;

            int extra = 0;
            unsigned int cp = 0;
            if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1Fu; }
            else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0Fu; }
            else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07u; }
            else return 0;

            for (int i = 1; i <= extra; ++i)
            {
                if ((p[i] & 0xC0) != 0x80)
                    return 0;
                cp = (cp << 6) | (p[i] & 0x3Fu);
            }
            return cp;
        }
    }

    void DrawIconCentred(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                         const char* icon, ImU32 colour)
    {
        if (!dl || !icon || !*icon)
            return;

        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize();
        ImFontBaked* baked = font ? font->GetFontBaked(size) : nullptr;
        if (!baked)
            return;

        const unsigned int cp = FirstCodepoint(icon);
        const ImFontGlyph* g = cp ? baked->FindGlyphNoFallback(
                                        static_cast<ImWchar>(cp))
                                  : nullptr;
        if (!g)
            return;

        const GlyphInk ink{ g->X0, g->Y0, g->X1, g->Y1 };
        const Point at = SnapToPixels(
            CentredIconPos(min.x, min.y, max.x, max.y, ink));

        dl->AddText(font, size, ImVec2(at.X, at.Y), colour, icon);
    }

    bool IconButton(const char* id, const char* icon, const ImVec2& size,
                    Token colour, const char* tooltip)
    {
        const bool hit = ImGui::Button(id, size);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        ImVec4 tint = Theme::Color(colour);
        tint.w *= ImGui::GetStyle().Alpha;
        DrawIconCentred(ImGui::GetWindowDrawList(), min, max, icon,
                        ImGui::ColorConvertFloat4ToU32(tint));

        if (tooltip && *tooltip && ImGui::IsItemHovered())
            Tooltip(tooltip);
        return hit;
    }
}
