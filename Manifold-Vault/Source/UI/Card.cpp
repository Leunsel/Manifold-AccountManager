#include "UI/Card.h"

#include "App/BunkerTheme.h"
#include "UI/BunkerFonts.h"
#include "UI/UiScale.h"

#include <cfloat>
#include <cmath>

namespace Bunker::UI
{
    using Theme::Token;

    namespace
    {
        constexpr int kMaxDepth = 4;
        ImDrawListSplitter g_splitters[kMaxDepth];
        int g_depth = 0;
    }

    void BeginBox(const char* id, float width, ImVec2 padding)
    {
        IM_ASSERT(g_depth < kMaxDepth && "BeginBox nested too deeply");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImDrawListSplitter& split = g_splitters[g_depth++];
        split.Split(dl, 2);
        split.SetCurrentChannel(dl, 1);

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, padding);
        ImGui::BeginTable(id, 1, ImGuiTableFlags_PadOuterX | ImGuiTableFlags_NoSavedSettings,
                          ImVec2(width, 0.0f));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PopStyleVar();
    }

    void EndBox(ImU32 fill, ImU32 border, float rounding)
    {
        ImGui::EndTable();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        IM_ASSERT(g_depth > 0 && "EndBox ohne BeginBox");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImDrawListSplitter& split = g_splitters[--g_depth];
        split.SetCurrentChannel(dl, 0);
        dl->AddRectFilled(min, max, fill, rounding);
        if ((border & IM_COL32_A_MASK) != 0)
        {
            const float half = Px(1.0f) * 0.5f;
            dl->AddRect(ImVec2(min.x + half, min.y + half), ImVec2(max.x - half, max.y - half),
                        border, rounding, 0, Px(1.0f));
        }
        split.Merge(dl);
    }

    ImVec2 CardPadding() { return Px(16.0f, 14.0f); }
    ImVec2 TilePadding() { return Px(12.0f, 10.0f); }

    void BeginCard(const char* id, float width) { BeginBox(id, width, CardPadding()); }

    void EndCard()
    {
        EndBox(Theme::U32(Token::Elevated, 0.45f), Theme::U32(Token::Line),
               Theme::Round(Px(10.0f)));
    }

    void BeginTile(const char* id, float width) { BeginBox(id, width, TilePadding()); }

    void EndTile()
    {
        EndBox(Theme::U32(Token::Inset, 0.55f), Theme::U32(Token::Line, 0.8f),
               Theme::Round(Px(8.0f)));
    }

    void CardHeader(const char* icon, const char* title, const char* badge)
    {
        const Fonts::Set& f = Fonts::Current();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float right = at.x + ImGui::GetContentRegionAvail().x;
        const float size = Px(Fonts::Sizes::Body);
        const float badgeSize = Px(Fonts::Sizes::Label);

        dl->PushClipRect(at, ImVec2(right, at.y + size * 2.0f), true);
        dl->AddText(f.Text, size, at, Theme::U32(Token::AccentCore), icon);
        float x = at.x + f.Text->CalcTextSizeA(size, FLT_MAX, 0.0f, icon).x + Px(9.0f);
        dl->AddText(f.Strong, size, ImVec2(x, at.y), Theme::U32(Token::TextPrimary), title);
        x += f.Strong->CalcTextSizeA(size, FLT_MAX, 0.0f, title).x + Px(10.0f);
        if (badge && *badge)
            dl->AddText(f.Mono, badgeSize, ImVec2(x, at.y + (size - badgeSize) * 0.55f),
                        Theme::U32(Token::TextMuted), badge);
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(0.0f, size + Px(10.0f)));
    }

    void CardColumns::Begin(float twoFrom, std::size_t cards)
    {
        m_origin = ImGui::GetCursorScreenPos();
        const float avail = ImGui::GetContentRegionAvail().x;
        m_gap = Px(12.0f);
        m_two = avail >= twoFrom && cards > 1;
        m_width = m_two ? std::floor((avail - m_gap) * 0.5f) : avail;
        m_bottom[0] = m_bottom[1] = m_origin.y;
        m_current = 0;
    }

    float CardColumns::Next()
    {
        m_current = (m_two && m_bottom[1] < m_bottom[0]) ? 1 : 0;
        ImGui::SetCursorScreenPos(
            ImVec2(m_origin.x + static_cast<float>(m_current) * (m_width + m_gap),
                   m_bottom[m_current]));
        return m_width;
    }

    void CardColumns::Placed()
    {
        m_bottom[m_current] = ImGui::GetItemRectMax().y + m_gap;
    }

    void CardColumns::End()
    {
        const float bottom = m_bottom[0] > m_bottom[1] ? m_bottom[0] : m_bottom[1];
        ImGui::SetCursorScreenPos(ImVec2(m_origin.x, bottom > m_origin.y ? bottom - m_gap
                                                                         : m_origin.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    namespace
    {
        ImVec2 KeyChipPadding() { return Px(6.0f, 3.0f); }
    }

    float KeyChipWidth(const char* text)
    {
        const Fonts::Set& f = Fonts::Current();
        return f.Mono->CalcTextSizeA(Px(Fonts::Sizes::Label), FLT_MAX, 0.0f, text).x +
               KeyChipPadding().x * 2.0f;
    }

    void KeyChip(const char* text)
    {
        const Fonts::Set& f = Fonts::Current();
        const float size = Px(Fonts::Sizes::Label);
        const ImVec2 t = f.Mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        const ImVec2 pad = KeyChipPadding();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 box(t.x + pad.x * 2.0f, t.y + pad.y * 2.0f);
        ImGui::Dummy(box);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(at.x + box.x, at.y + box.y);
        dl->AddRectFilled(at, end, Theme::U32(Token::Inset, 0.6f), Theme::Round(Px(4.0f)));
        dl->AddRect(at, end, Theme::U32(Token::Line), Theme::Round(Px(4.0f)), 0, 1.0f);
        dl->AddText(f.Mono, size, ImVec2(at.x + pad.x, at.y + pad.y),
                    Theme::U32(Token::TextSecondary), text);
    }
}
