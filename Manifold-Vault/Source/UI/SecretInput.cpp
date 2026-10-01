#include "UI/SecretInput.h"

#include "App/BunkerTheme.h"
#include "Core/SecretChars.h"
#include "UI/BunkerFonts.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"

#include <sodium.h>

#include <cmath>
#include <cstdio>

#include <windows.h>

namespace Bunker::UI
{
    using Theme::Token;

    namespace
    {
        int g_escapeFrame = -1;
    }

    SecretField::SecretField(std::size_t capacity)
        : m_buffer(capacity), m_capacity(capacity)
    {
    }

    bool SecretField::TookEscapeThisFrame()
    {
        return g_escapeFrame == ImGui::GetFrameCount();
    }

    void SecretField::Clear()
    {
        m_buffer.Wipe();
        m_length = 0;
    }

    bool SecretField::Set(const char* text, std::size_t len)
    {
        Clear();
        if (!m_buffer.IsValid() || !text)
            return false;
        if (len + 1 > m_capacity)
            return false;
        for (std::size_t i = 0; i < len; ++i)
            m_buffer.Data()[i] = static_cast<unsigned char>(text[i]);
        m_length = len;
        m_buffer.Data()[m_length] = 0;
        return true;
    }

    const char* SecretField::Data() const
    {
        return m_buffer.IsValid() ? reinterpret_cast<const char*>(m_buffer.Data()) : "";
    }

    bool SecretField::CapsLockOn()
    {
        return (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    }

    void SecretField::Append(unsigned int cp)
    {
        if (m_buffer.IsValid())
            AppendUtf8(m_buffer.Data(), m_capacity, m_length, cp);
    }

    void SecretField::PasteFromClipboard()
    {
        if (!m_buffer.IsValid())
            return;
        if (!::OpenClipboard(nullptr))
            return;
        if (HANDLE h = ::GetClipboardData(CF_UNICODETEXT))
        {
            if (const auto* text = static_cast<const wchar_t*>(::GlobalLock(h)))
            {
                AppendUtf16(m_buffer.Data(), m_capacity, m_length, text,
                            ::GlobalSize(h) / sizeof(wchar_t));
                ::GlobalUnlock(h);
            }
        }
        ::CloseClipboard();
    }

    void SecretField::Backspace()
    {
        if (m_length == 0 || !m_buffer.IsValid())
            return;
        std::size_t n = m_length - 1;
        while (n > 0 && (m_buffer.Data()[n] & 0xC0) == 0x80)
            --n;
        for (std::size_t i = n; i < m_length; ++i)
            m_buffer.Data()[i] = 0;
        m_length = n;
    }

    void SecretField::DeleteWordBack()
    {
        Clear();
    }

    bool SecretField::Draw(const char* id, const char* placeholder, float width,
                           bool focusNow, float height)
    {
        ImGuiIO& io = ImGui::GetIO();
        const Fonts::Set& fonts = Fonts::Current();

        ImGui::PushID(id);

        if (height <= 0.0f)
            height = ImGui::GetFrameHeight() + Px(8.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(width, height);

        ImGui::InvisibleButton("##hit", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool ourHitActive = ImGui::IsItemActive();
        if (ImGui::IsItemClicked())
            m_focused = true;
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered)
            m_focused = false;

        if (m_focused && !ourHitActive && ImGui::IsAnyItemActive())
            m_focused = false;

        if (focusNow)
            m_focused = true;

        bool submitted = false;

        if (m_focused)
        {
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
            {
                const ImWchar c = io.InputQueueCharacters[i];
                if (IsSecretFieldChar(c))
                    Append(static_cast<unsigned int>(c));
            }
            if (io.InputQueueCharacters.Data)
                sodium_memzero(io.InputQueueCharacters.Data,
                               static_cast<std::size_t>(io.InputQueueCharacters.Capacity) *
                                   sizeof(ImWchar));
            io.InputQueueCharacters.resize(0);

            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
            {
                if (io.KeyCtrl) DeleteWordBack();
                else Backspace();
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
                Clear();

            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
                PasteFromClipboard();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                if (m_escapeClears)
                    Clear();
                else
                    m_focused = false;
                g_escapeFrame = ImGui::GetFrameCount();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                submitted = m_length > 0;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 br(pos.x + size.x, pos.y + size.y);
        const float rounding = ImGui::GetStyle().FrameRounding;

        dl->AddRectFilled(pos, br, Theme::U32(Token::Inset), rounding);
        dl->AddRect(pos, br,
                    Theme::U32(m_focused ? Token::AccentCore
                                         : (hovered ? Token::LineStrong : Token::Line)),
                    rounding, 0, Px(m_focused ? 2.0f : 1.0f));

        const float padX = Px(12.0f);
        const float centreY = pos.y + size.y * 0.5f;

        const float dotGap = Px(10.0f);
        const std::size_t maxDots = static_cast<std::size_t>(
            Max(1.0f, (size.x - padX * 2 - Px(20.0f)) / dotGap));

        if (m_revealed && m_length > 0)
        {
            const ImVec2 t = fonts.Mono->CalcTextSizeA(Px(Fonts::Sizes::Body), FLT_MAX, 0.0f, Data());
            dl->PushClipRect(ImVec2(pos.x + padX, pos.y),
                             ImVec2(br.x - padX, br.y), true);
            dl->AddText(fonts.Mono, Px(Fonts::Sizes::Body),
                        ImVec2(pos.x + padX, centreY - t.y * 0.5f),
                        Theme::U32(Token::WarnBright), Data());
            dl->PopClipRect();
        }
        else if (m_length == 0 && placeholder && *placeholder)
        {
            const ImVec2 t = fonts.Text->CalcTextSizeA(Px(Fonts::Sizes::Body), FLT_MAX, 0.0f, placeholder);
            dl->AddText(fonts.Text, Px(Fonts::Sizes::Body),
                        ImVec2(pos.x + padX, centreY - t.y * 0.5f),
                        Theme::U32(Token::TextDisabled), placeholder);
        }
        else
        {
            std::size_t chars = 0;
            for (std::size_t i = 0; i < m_length; ++i)
                if ((m_buffer.Data()[i] & 0xC0) != 0x80)
                    ++chars;

            const float dotRadius = Px(3.0f);
            const std::size_t shown = Min(chars, maxDots);

            for (std::size_t i = 0; i < shown; ++i)
                dl->AddCircleFilled(ImVec2(pos.x + padX + dotRadius + i * dotGap, centreY),
                                    dotRadius, Theme::U32(Token::TextPrimary), 12);

            if (chars > shown)
            {
                char more[24];
                ::snprintf(more, sizeof(more), "+%zu", chars - shown);
                dl->AddText(fonts.Text, Px(Fonts::Sizes::Small),
                            ImVec2(pos.x + padX + shown * dotGap + Px(6.0f), centreY - Px(7.0f)),
                            Theme::U32(Token::TextMuted), more);
            }
        }

        if (m_focused && !m_revealed)
        {
            m_caretPhase += io.DeltaTime;
            if (std::fmod(m_caretPhase, 1.06f) < 0.53f)
            {
                std::size_t chars = 0;
                for (std::size_t i = 0; i < m_length; ++i)
                    if ((m_buffer.Data()[i] & 0xC0) != 0x80)
                        ++chars;
                const float x = pos.x + padX + Min(chars, maxDots) * dotGap + Px(1.0f);
                dl->AddLine(ImVec2(x, centreY - Px(8.0f)), ImVec2(x, centreY + Px(8.0f)),
                            Theme::U32(Token::AccentBright), Px(1.5f));
            }
        }

        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

        ImGui::PopID();
        return submitted;
    }
}
