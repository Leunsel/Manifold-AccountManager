#pragma once

#include "imgui.h"

#include <cstdint>

namespace Bunker::UI::Motion
{
    void SetEnabled(bool on);
    bool Enabled();

    inline constexpr float kEntranceSeconds = 0.18f;

    ImGuiID KeyFor(ImGuiID item, const char* what);

    float Toward(ImGuiID key, bool on, float seconds);

    float Follow(ImGuiID key, float target, float seconds);

    class Entrance
    {
    public:
        void Begin(std::uint64_t key, float rise, float depth);

        void End(ImVec4 ground);

        std::uint64_t Key() const { return m_key; }

        void Forget();

    private:
        std::uint64_t m_key = 0;
        bool m_known = false;
        int m_lastFrame = -2;
        double m_start = -100.0;
        double m_lastChange = -100.0;
        float m_rise = 0.0f;
        float m_depth = 0.0f;
        float m_progress = 1.0f;
        ImVec2 m_anchor;
        ImVec2 m_windowMin;
        ImVec2 m_windowMax;
    };
}
