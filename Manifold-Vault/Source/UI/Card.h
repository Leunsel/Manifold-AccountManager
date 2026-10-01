#pragma once

#include "imgui.h"

#include <cstddef>

namespace Bunker::UI
{
    void BeginBox(const char* id, float width, ImVec2 padding);

    void EndBox(ImU32 fill, ImU32 border, float rounding);

    ImVec2 CardPadding();
    ImVec2 TilePadding();

    void BeginCard(const char* id, float width);
    void EndCard();

    void BeginTile(const char* id, float width);
    void EndTile();

    void CardHeader(const char* icon, const char* title, const char* badge);

    class CardColumns
    {
    public:
        void Begin(float twoFrom, std::size_t cards);
        float Next();
        void Placed();
        void End();

    private:
        ImVec2 m_origin{ 0.0f, 0.0f };
        float m_width = 0.0f;
        float m_gap = 0.0f;
        bool m_two = false;
        float m_bottom[2] = { 0.0f, 0.0f };
        int m_current = 0;
    };

    float KeyChipWidth(const char* text);
    void KeyChip(const char* text);
}
