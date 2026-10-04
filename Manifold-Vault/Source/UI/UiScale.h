#pragma once

#include "imgui.h"

#include <cmath>

namespace Bunker::UI
{
    inline float Scale()
    {
        const float s = ImGui::GetStyle().FontScaleDpi;
        return s > 0.0f ? s : 1.0f;
    }

    inline float Px(float px) { return px * Scale(); }

    inline ImVec2 Px(float w, float h)
    {
        const float s = Scale();
        return ImVec2(w * s, h * s);
    }

    // Window padding: self-sizing windows truncate fractions and then show a scrollbar.
    inline ImVec2 PxWhole(float w, float h)
    {
        const float s = Scale();
        return ImVec2(std::round(w * s), std::round(h * s));
    }
}
