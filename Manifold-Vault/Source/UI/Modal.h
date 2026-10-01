#pragma once

#include "imgui.h"

#include <cstdio>

namespace Bunker::UI
{
    inline bool BeginTitledModal(const char* title, const char* id, ImGuiWindowFlags flags)
    {
        char name[256];
        std::snprintf(name, sizeof(name), "%s%s", title, id);
        return ImGui::BeginPopupModal(name, nullptr, flags);
    }
}
