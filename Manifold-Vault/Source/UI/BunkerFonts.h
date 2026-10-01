#pragma once

#include "imgui.h"

namespace Bunker::Fonts
{
    struct Set
    {
        ImFont* Text = nullptr;
        ImFont* Medium = nullptr;
        ImFont* Strong = nullptr;
        ImFont* Mono = nullptr;
        ImFont* Brands = nullptr;

        bool Valid() const { return Text && Medium && Strong && Mono && Brands; }
    };

    namespace Sizes
    {
        constexpr float Body = 15.0f;
        constexpr float Small = 13.0f;
        constexpr float Label = 12.0f;
        constexpr float Title = 20.0f;
        constexpr float Display = 27.0f;
        constexpr float IconRow = 19.0f;
        constexpr float IconLarge = 30.0f;
    }

    bool Build(Set& out, const char** error);

    const Set& Current();

    void UseMonospace(bool monospace);
}
