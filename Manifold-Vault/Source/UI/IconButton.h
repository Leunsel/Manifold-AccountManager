#pragma once

#include "App/BunkerTheme.h"

#include "imgui.h"

namespace Bunker::UI
{
    void DrawIconCentred(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                         const char* icon, ImU32 colour);

    bool IconButton(const char* id, const char* icon, const ImVec2& size,
                    Theme::Token colour = Theme::Token::TextMuted,
                    const char* tooltip = nullptr);
}
