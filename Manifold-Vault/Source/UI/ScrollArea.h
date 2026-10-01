#pragma once

#include "App/BunkerTheme.h"
#include "UI/UiScale.h"

#include "imgui.h"

#include <algorithm>

namespace Bunker::UI
{
    inline void ScrollFade(Theme::Token background)
    {
        const float scroll = ImGui::GetScrollY();
        const float scrollMax = ImGui::GetScrollMaxY();
        const float fade = Px(12.0f);
        const float topAmount = std::clamp(scroll / fade, 0.0f, 1.0f);
        const float bottomAmount = std::clamp((scrollMax - scroll) / fade, 0.0f, 1.0f);
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 clear = Theme::U32(background, 0.0f);
        if (topAmount > 0.0f)
        {
            const ImU32 solid = Theme::U32(background, topAmount);
            dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + size.x, pos.y + fade),
                                        solid, solid, clear, clear);
        }
        if (bottomAmount > 0.0f)
        {
            const ImU32 solid = Theme::U32(background, bottomAmount);
            dl->AddRectFilledMultiColor(ImVec2(pos.x, pos.y + size.y - fade),
                                        ImVec2(pos.x + size.x, pos.y + size.y),
                                        clear, clear, solid, solid);
        }
    }

    inline float ScreenContentWidth(float avail)
    {
        return std::max(0.0f, avail - ImGui::GetStyle().ScrollbarSize);
    }

    constexpr float kScrollOverhang = 6.0f;

    inline void BeginScrollBody(const char* id, float contentWidth = 0.0f)
    {
        const float pad = Px(kScrollOverhang);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x + pad * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(at.x - pad, at.y));
        if (contentWidth > 0.0f)
            ImGui::SetNextWindowContentSize(ImVec2(contentWidth, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, 0.0f));
        ImGui::BeginChild(id, ImVec2(width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
    }

    inline void BeginScreenBody(const char* id, float width)
    {
        BeginScrollBody(id, width);
    }

    inline void EndScreenBody()
    {
        ImGui::EndChild();
    }

    inline void ScreenRule(float width)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(at, ImVec2(at.x + width, at.y),
                                            ImGui::GetColorU32(ImGuiCol_Separator),
                                            Px(1.0f));
        ImGui::Dummy(ImVec2(width, Px(1.0f)));
    }
}
