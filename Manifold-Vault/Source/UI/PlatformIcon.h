#pragma once

#include "imgui.h"

namespace Bunker::UI
{
    struct IconSpec
    {
        const char* BrandName = nullptr;
        const char* Symbol = nullptr;
        const char* Monogram = nullptr;
        ImU32 Accent = 0;
    };

    ImWchar LookupBrandIcon(const char* name);

    bool HasGlyph(const IconSpec& spec);

    ImWchar LookupSymbolIcon(const char* name);

    void DrawPlatformIcon(const IconSpec& spec, float size);

    void DrawPlatformIconAt(ImDrawList* dl, ImVec2 topLeft, float size, const IconSpec& spec);

    void DeriveMonogram(const char* displayName, char* out, size_t outSize);
}
