#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Bunker
{
    class Application;
    class VaultSession;
}

namespace Bunker::UI::Settings
{
    bool IsOpen();
    void Open();
    void Close();

    bool Draw(Application& app, VaultSession& session);

    bool DrawPopups(Application& app, VaultSession& session);

    void StartImport();

    std::string TakeImportSummary(bool& worked);

    const char* ThemeLabel(std::size_t index);
}
