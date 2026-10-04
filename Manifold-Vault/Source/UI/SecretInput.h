#pragma once

#include "Core/Secret.h"

#include "imgui.h"

#include <cstddef>
#include <string>

namespace Bunker::UI
{
    class SecretField
    {
    public:
        explicit SecretField(std::size_t capacity = 512);

        SecretField(const SecretField&) = delete;
        SecretField& operator=(const SecretField&) = delete;

        void Clear();

        bool Set(const char* text, std::size_t len);

        void SetRevealed(bool on) { m_revealed = on; }
        bool IsRevealed() const { return m_revealed; }

        void SetEscapeClears(bool clears) { m_escapeClears = clears; }

        static bool TookEscapeThisFrame();

        const char* Data() const;
        std::size_t Length() const { return m_length; }
        bool IsEmpty() const { return m_length == 0; }
        bool IsFull() const { return m_length + 1 >= m_capacity; }

        bool Draw(const char* id, const char* placeholder, float width,
                  bool focusNow = false, float height = 0.0f);

        bool HasFocus() const { return m_focused; }
        void SetFocus(bool on) { m_focused = on; }

        static bool CapsLockOn();
        static std::string KeyboardLayout();

    private:
        void Append(unsigned int codepoint);
        void PasteFromClipboard();
        void Backspace();
        void DeleteWordBack();

        SecureBuffer m_buffer;
        std::size_t m_capacity = 0;
        std::size_t m_length = 0;
        bool m_focused = false;
        bool m_revealed = false;
        bool m_escapeClears = true;
        float m_caretPhase = 0.0f;
    };
}
