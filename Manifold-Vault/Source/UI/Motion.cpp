#include "UI/Motion.h"

#include "UI/MotionCurve.h"
#include "UI/UiScale.h"

#include <cmath>

namespace Bunker::UI::Motion
{
    namespace
    {
        bool g_enabled = true;

        constexpr double kQuietGap = 0.25;
    }

    void SetEnabled(bool on) { g_enabled = on; }
    bool Enabled() { return g_enabled; }

    ImGuiID KeyFor(ImGuiID item, const char* what)
    {
        ImGui::PushID(static_cast<int>(item));
        const ImGuiID key = ImGui::GetID(what);
        ImGui::PopID();
        return key;
    }

    float Toward(ImGuiID key, bool on, float seconds)
    {
        const float target = on ? 1.0f : 0.0f;
        ImGuiStorage* store = ImGui::GetStateStorage();
        float* value = store->GetFloatRef(key, target);
        *value = g_enabled ? Curve::StepToward(*value, target, ImGui::GetIO().DeltaTime, seconds)
                           : target;
        return *value;
    }

    float Follow(ImGuiID key, float target, float seconds)
    {
        ImGuiStorage* store = ImGui::GetStateStorage();
        float* value = store->GetFloatRef(key, target);
        *value = g_enabled ? Curve::StepFollow(*value, target, ImGui::GetIO().DeltaTime, seconds)
                           : target;
        return *value;
    }

    void Entrance::Begin(std::uint64_t key, float rise, float depth)
    {
        const double now = ImGui::GetTime();
        const int frame = ImGui::GetFrameCount();
        const bool continuous = m_lastFrame == frame - 1;
        const bool changed = m_known && (key != m_key || !continuous);
        m_known = true;
        m_key = key;
        m_lastFrame = frame;

        if (changed && g_enabled)
        {
            if (now - m_lastChange >= kQuietGap)
            {
                m_start = now;
                m_rise = rise;
                m_depth = depth;
            }
            m_lastChange = now;
        }
        if (!g_enabled)
            m_start = -100.0;

        const float t = static_cast<float>((now - m_start) / kEntranceSeconds);
        m_progress = Curve::EaseOut(t);

        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        m_windowMin = pos;
        m_windowMax = ImVec2(pos.x + size.x, pos.y + size.y);
        m_anchor = ImGui::GetCursorScreenPos();

        const float drop = std::floor((1.0f - m_progress) * m_rise + 0.5f);
        if (drop >= 1.0f)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + drop);
    }

    void Entrance::End(ImVec4 ground)
    {
        ImGui::SetCursorScreenPos(ImVec2(m_anchor.x + ImGui::GetScrollX(),
                                         m_anchor.y + ImGui::GetScrollY()));
        ImGui::BeginChild("##motion-curtain", Px(1.0f, 1.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        const float alpha = (1.0f - m_progress) * m_depth;
        if (alpha > 0.004f)
        {
            const float edge = Px(1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(m_windowMin, m_windowMax, false);
            dl->AddRectFilled(ImVec2(m_windowMin.x + edge, m_windowMin.y),
                              ImVec2(m_windowMax.x - edge, m_windowMax.y),
                              ImGui::ColorConvertFloat4ToU32(
                                  ImVec4(ground.x, ground.y, ground.z, ground.w * alpha)));
            dl->PopClipRect();
        }
        ImGui::EndChild();
    }

    void Entrance::Forget()
    {
        m_known = false;
        m_lastFrame = -2;
        m_start = -100.0;
        m_lastChange = -100.0;
        m_progress = 1.0f;
    }
}
