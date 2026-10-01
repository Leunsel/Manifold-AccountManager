#include "PlatformIcon.h"

#include "Core/GermanText.h"
#include "App/BunkerTheme.h"
#include "UI/BunkerFonts.h"
#include "Generated/FaBrands.h"
#include "Generated/FaSymbols.h"

#include <cstring>

namespace Bunker::UI
{
    namespace
    {
        void EncodeUtf8(ImWchar cp, char out[5])
        {
            unsigned int c = static_cast<unsigned int>(cp);
            if (c < 0x80)
            {
                out[0] = static_cast<char>(c);
                out[1] = '\0';
            }
            else if (c < 0x800)
            {
                out[0] = static_cast<char>(0xC0 | (c >> 6));
                out[1] = static_cast<char>(0x80 | (c & 0x3F));
                out[2] = '\0';
            }
            else
            {
                out[0] = static_cast<char>(0xE0 | (c >> 12));
                out[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out[2] = static_cast<char>(0x80 | (c & 0x3F));
                out[3] = '\0';
            }
        }
    }

    ImWchar LookupBrandIcon(const char* name)
    {
        if (!name || !*name)
            return 0;

        size_t lo = 0;
        size_t hi = kFaBrandCount;
        while (lo < hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            const int cmp = std::strcmp(kFaBrands[mid].name, name);
            if (cmp == 0)
                return kFaBrands[mid].codepoint;
            if (cmp < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        return 0;
    }

    bool HasGlyph(const IconSpec& spec)
    {
        return LookupBrandIcon(spec.BrandName) != 0;
    }

    ImWchar LookupSymbolIcon(const char* name)
    {
        if (!name || !*name)
            return 0;
        for (std::size_t i = 0; i < kFaSymbolCount; ++i)
            if (std::strcmp(kFaSymbols[i].name, name) == 0)
                return kFaSymbols[i].codepoint;
        return 0;
    }

    namespace
    {
        void DrawTile(ImDrawList* dl, ImVec2 topLeft, float size, ImU32 colour)
        {
            const ImVec2 bottomRight(topLeft.x + size, topLeft.y + size);
            dl->AddRectFilled(topLeft, bottomRight, colour, Theme::Round(size * 0.26f));
            dl->AddRect(topLeft, bottomRight, Theme::U32(Theme::Token::Shade, 0.35f),
                        Theme::Round(size * 0.26f));
        }
    }

    namespace
    {
        std::size_t CopyUpper(const char* src, std::size_t seq, char* dst)
        {
            if (seq == 1)
            {
                const unsigned char ch = static_cast<unsigned char>(src[0]);
                dst[0] = static_cast<char>((ch >= 'a' && ch <= 'z') ? (ch - 32) : ch);
                dst[1] = '\0';
                return 1;
            }
            for (std::size_t i = 0; i < seq; ++i)
                dst[i] = src[i];
            dst[seq] = '\0';
            const unsigned char lead = static_cast<unsigned char>(dst[0]);
            const unsigned char second = static_cast<unsigned char>(dst[1]);
            if (seq == 2 && lead == 0xC3 && second >= 0xA0 && second <= 0xBE)
                dst[1] = static_cast<char>(second - 0x20);
            return seq;
        }
    }

    void DeriveMonogram(const char* displayName, char* out, size_t outSize)
    {
        if (!out || outSize == 0)
            return;
        out[0] = '\0';
        if (!displayName)
            return;

        char letters[2][5] = { { 0 }, { 0 } };
        int found = 0;
        bool atWordStart = true;

        for (const char* p = displayName; *p && found < 2; )
        {
            const unsigned char ch = static_cast<unsigned char>(*p);
            const std::size_t seq = Utf8SequenceLength(ch);
            const bool alnum = (ch >= '0' && ch <= '9') ||
                               (ch >= 'A' && ch <= 'Z') ||
                               (ch >= 'a' && ch <= 'z') ||
                               ch >= 0x80;
            if (!alnum)
            {
                atWordStart = true;
            }
            else if (atWordStart)
            {
                CopyUpper(p, seq, letters[found]);
                ++found;
                atWordStart = false;
            }
            for (std::size_t i = 0; i < seq && *p; ++i)
                ++p;
        }

        if (found == 1)
        {
            for (const char* p = displayName; *p; )
            {
                const unsigned char ch = static_cast<unsigned char>(*p);
                const std::size_t seq = Utf8SequenceLength(ch);
                const bool alpha = (ch >= 'A' && ch <= 'Z') ||
                                   (ch >= 'a' && ch <= 'z') ||
                                   ch >= 0x80;
                if (alpha)
                {
                    char candidate[5] = {};
                    CopyUpper(p, seq, candidate);
                    if (std::strcmp(candidate, letters[0]) != 0)
                    {
                        std::memcpy(letters[1], candidate, sizeof(candidate));
                        found = 2;
                        break;
                    }
                }
                for (std::size_t i = 0; i < seq && *p; ++i)
                    ++p;
            }
        }

        std::size_t n = 0;
        for (int i = 0; i < found; ++i)
        {
            const std::size_t len = std::strlen(letters[i]);
            if (n + len + 1 > outSize)
                break;
            std::memcpy(out + n, letters[i], len);
            n += len;
        }
        out[n] = '\0';
    }

    void DrawPlatformIconAt(ImDrawList* dl, ImVec2 topLeft, float size, const IconSpec& spec)
    {
        if (!dl || size <= 0.0f)
            return;

        const ImWchar cp = LookupBrandIcon(spec.BrandName);

        const ImU32 specAccent = Theme::ColouredIcons() ? spec.Accent : 0;

        if (const ImWchar symbol = LookupSymbolIcon(spec.Symbol); symbol != 0)
        {
            const ImU32 tile = specAccent ? specAccent : Theme::U32(Theme::Token::LineStrong);
            DrawTile(dl, topLeft, size, tile);

            ImFont* font = Fonts::Current().Text;
            if (!font)
                return;
            char utf8[5];
            EncodeUtf8(symbol, utf8);
            const float glyphSize = size * 0.52f;
            const ImVec2 centre(topLeft.x + size * 0.5f, topLeft.y + size * 0.5f);

            ImVec2 at;
            const ImFontGlyph* glyph = nullptr;
            if (ImFontBaked* baked = font->GetFontBaked(glyphSize))
                glyph = baked->FindGlyph(symbol);
            if (glyph)
            {
                at = ImVec2(centre.x - (glyph->X0 + glyph->X1) * 0.5f,
                            centre.y - (glyph->Y0 + glyph->Y1) * 0.5f);
            }
            else
            {
                const ImVec2 measured = font->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, utf8);
                at = ImVec2(centre.x - measured.x * 0.5f, centre.y - measured.y * 0.5f);
            }
            dl->AddText(font, glyphSize, at,
                        Theme::ReadableTextOn(ImGui::ColorConvertU32ToFloat4(tile)), utf8);
            return;
        }

        if (cp != 0)
        {
            ImFont* font = Fonts::Current().Brands;
            if (font)
            {
                char utf8[5];
                EncodeUtf8(cp, utf8);

                const float glyphSize = size * 0.86f;
                const ImVec2 measured = font->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, utf8);
                const ImVec2 at(topLeft.x + (size - measured.x) * 0.5f,
                                topLeft.y + (size - measured.y) * 0.5f);
                dl->AddText(font, glyphSize, at,
                            specAccent ? specAccent
                                       : Theme::U32(Theme::Token::TextSecondary),
                            utf8);
                return;
            }
        }

        const ImU32 accent = specAccent ? specAccent : Theme::U32(Theme::Token::LineStrong);
        const ImVec4 accentV = ImGui::ColorConvertU32ToFloat4(accent);
        DrawTile(dl, topLeft, size, accent);

        char derived[5] = { 0 };
        const char* text = spec.Monogram;
        if (!text || !*text)
        {
            DeriveMonogram(spec.BrandName, derived, sizeof(derived));
            text = derived;
        }
        if (!text || !*text)
            return;

        ImFont* font = Fonts::Current().Strong;
        if (!font)
            return;

        const float textSize = size * (std::strlen(text) > 1 ? 0.42f : 0.52f);
        const ImVec2 measured = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, text);
        const ImVec2 at(topLeft.x + (size - measured.x) * 0.5f,
                        topLeft.y + (size - measured.y) * 0.5f);
        dl->AddText(font, textSize, at, Theme::ReadableTextOn(accentV), text);
    }

    void DrawPlatformIcon(const IconSpec& spec, float size)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        DrawPlatformIconAt(ImGui::GetWindowDrawList(), pos, size, spec);
        ImGui::Dummy(ImVec2(size, size));
    }
}
