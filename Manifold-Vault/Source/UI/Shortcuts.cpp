#include "UI/Shortcuts.h"

#include "Core/Language.h"

namespace Bunker::UI::Shortcuts
{
    namespace
    {
        constexpr Shortcut kShortcuts[] = {
            { Action::MoveSelection, ImGuiKey_None, ImGuiKey_None, false,
              Str::Settings_KeyUpDown, Str::Settings_ShortcutUpDown },
            { Action::EditEntry, ImGuiKey_Enter, ImGuiKey_KeypadEnter, false,
              Str::Settings_KeyEnter, Str::Settings_ShortcutEnter },
            { Action::DoubleClick, ImGuiKey_None, ImGuiKey_None, false,
              Str::Settings_KeyDoubleClick, Str::Settings_ShortcutDoubleClick },
            { Action::OpenLogin, ImGuiKey_Enter, ImGuiKey_KeypadEnter, true,
              Str::Settings_KeyCtrlEnter, Str::Settings_ShortcutCtrlEnter },
            { Action::SaveEntry, ImGuiKey_S, ImGuiKey_None, true,
              Str::Settings_KeyCtrlS, Str::Settings_ShortcutCtrlS },
            { Action::CopyPassword, ImGuiKey_C, ImGuiKey_None, true,
              Str::Settings_KeyCtrlC, Str::Settings_ShortcutCtrlC },
            { Action::CopyUsername, ImGuiKey_U, ImGuiKey_None, true,
              Str::Settings_KeyCtrlU, Str::Settings_ShortcutCtrlU },
            { Action::CopyTwoFactor, ImGuiKey_T, ImGuiKey_None, true,
              Str::Settings_KeyCtrlT, Str::Settings_ShortcutCtrlT },
            { Action::ToggleFavourite, ImGuiKey_D, ImGuiKey_None, true,
              Str::Settings_KeyCtrlD, Str::Settings_ShortcutCtrlD },
            { Action::Rename, ImGuiKey_F2, ImGuiKey_None, false,
              Str::Settings_KeyF2, Str::Settings_ShortcutF2 },
            { Action::Delete, ImGuiKey_Delete, ImGuiKey_None, false,
              Str::Settings_KeyDelete, Str::Settings_ShortcutDelete },
            { Action::Undo, ImGuiKey_Z, ImGuiKey_None, true,
              Str::Settings_KeyCtrlZ, Str::Settings_ShortcutCtrlZ },
            { Action::MultiClick, ImGuiKey_None, ImGuiKey_None, false,
              Str::Settings_KeyMultiClick, Str::Settings_ShortcutMultiClick },
            { Action::SelectAll, ImGuiKey_A, ImGuiKey_None, true,
              Str::Settings_KeyCtrlA, Str::Settings_ShortcutCtrlA },
            { Action::ContextMenu, ImGuiKey_None, ImGuiKey_None, false,
              Str::Settings_KeyRightClick, Str::Settings_ShortcutRightClick },
            { Action::NewEntry, ImGuiKey_N, ImGuiKey_None, true,
              Str::Settings_KeyCtrlN, Str::Settings_ShortcutCtrlN },
            { Action::Search, ImGuiKey_F, ImGuiKey_None, true,
              Str::Settings_KeyCtrlF, Str::Settings_ShortcutCtrlF },
            { Action::CommandPalette, ImGuiKey_K, ImGuiKey_None, true,
              Str::Settings_KeyCtrlK, Str::Settings_ShortcutCtrlK },
            { Action::JumpToFolder, ImGuiKey_O, ImGuiKey_None, true,
              Str::Settings_KeyCtrlO, Str::Settings_ShortcutCtrlO },
            { Action::ToggleSidebar, ImGuiKey_B, ImGuiKey_None, true,
              Str::Settings_KeyCtrlB, Str::Settings_ShortcutCtrlB },
            { Action::StepBack, ImGuiKey_Escape, ImGuiKey_None, false,
              Str::Settings_KeyEscape, Str::Settings_ShortcutEscape },
            { Action::Lock, ImGuiKey_L, ImGuiKey_None, true,
              Str::Settings_KeyCtrlL, Str::Settings_ShortcutCtrlL },
            { Action::Help, ImGuiKey_F1, ImGuiKey_None, false,
              Str::Settings_KeyF1, Str::Settings_ShortcutF1 },
        };

        void ClaimChord(ImGuiKeyChord chord)
        {
            (void)ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal);
        }
    }

    const Shortcut* All(std::size_t& count)
    {
        count = sizeof(kShortcuts) / sizeof(kShortcuts[0]);
        return kShortcuts;
    }

    const Shortcut& Of(Action action)
    {
        for (const Shortcut& s : kShortcuts)
            if (s.What == action)
                return s;
        return kShortcuts[0];
    }

    const char* KeyName(Action action)
    {
        return T(Of(action).KeyName);
    }

    bool Pressed(Action action)
    {
        const Shortcut& s = Of(action);
        if (s.Key == ImGuiKey_None)
            return false;
        if (ImGui::GetIO().KeyCtrl != s.Ctrl)
            return false;
        return ImGui::IsKeyPressed(s.Key, false) ||
               (s.AlsoKey != ImGuiKey_None && ImGui::IsKeyPressed(s.AlsoKey, false));
    }

    void Claim(Action action)
    {
        const Shortcut& s = Of(action);
        const ImGuiKeyChord mods = s.Ctrl ? ImGuiMod_Ctrl : ImGuiMod_None;
        if (s.Key != ImGuiKey_None)
            ClaimChord(mods | s.Key);
        if (s.AlsoKey != ImGuiKey_None)
            ClaimChord(mods | s.AlsoKey);
    }

    void ClaimNavigationKeys()
    {
        const ImGuiKeyChord arrows[] = { ImGuiKey_UpArrow, ImGuiKey_DownArrow,
                                         ImGuiKey_LeftArrow, ImGuiKey_RightArrow };
        const ImGuiKeyChord mods[] = { ImGuiMod_None, ImGuiMod_Shift, ImGuiMod_Ctrl,
                                       ImGuiMod_Alt };
        for (ImGuiKeyChord arrow : arrows)
            for (ImGuiKeyChord mod : mods)
                ClaimChord(mod | arrow);

        ClaimChord(ImGuiKey_Tab);
        ClaimChord(ImGuiMod_Shift | ImGuiKey_Tab);
        ClaimChord(ImGuiKey_Menu);
        ClaimChord(ImGuiMod_Shift | ImGuiKey_F10);
    }
}
