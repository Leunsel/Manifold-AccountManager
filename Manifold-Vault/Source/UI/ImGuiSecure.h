#pragma once

#include <cstddef>

namespace Bunker::UI
{
    void InstallWipeOnFreeAllocator();

    std::size_t ImGuiLiveBytes();

    std::size_t ImGuiWipedBytes();
}
