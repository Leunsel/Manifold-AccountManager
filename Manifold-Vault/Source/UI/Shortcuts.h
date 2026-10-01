#pragma once

#include "Lang/Strings.h"

#include "imgui.h"

#include <cstddef>

namespace Bunker::UI::Shortcuts
{
    enum class Action
    {
        MoveSelection,
        EditEntry,
        DoubleClick,
        OpenLogin,
        SaveEntry,
        CopyPassword,
        CopyUsername,
        CopyTwoFactor,
        ToggleFavourite,
        Rename,
        Delete,
        Undo,
        MultiClick,
        SelectAll,
        ContextMenu,
        NewEntry,
        Search,
        CommandPalette,
        JumpToFolder,
        ToggleSidebar,
        StepBack,
        Lock,
        Help,
    };

    struct Shortcut
    {
        Action What;
        ImGuiKey Key;
        ImGuiKey AlsoKey;
        bool Ctrl;
        Str KeyName;
        Str Description;
    };

    const Shortcut* All(std::size_t& count);

    const Shortcut& Of(Action action);

    const char* KeyName(Action action);

    bool Pressed(Action action);

    void Claim(Action action);

    void ClaimNavigationKeys();
}
