#pragma once

#include "imgui.h"

#include <string_view>

namespace Bunker::UI
{
    inline float TooltipWrapWidth()
    {
        return ImGui::GetFontSize() * 26.0f;
    }

    inline bool BeginWrappedTooltip()
    {
        if (!ImGui::BeginTooltip())
            return false;
        ImGui::PushTextWrapPos(TooltipWrapWidth());
        return true;
    }

    inline void EndWrappedTooltip()
    {
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    inline void Tooltip(std::string_view text)
    {
        if (!BeginWrappedTooltip())
            return;
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        EndWrappedTooltip();
    }
}
