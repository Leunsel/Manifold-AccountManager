#pragma once

#include "Core/Model.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Bunker
{
    class Application;
    class VaultSession;
}

namespace Bunker::UI::Editor
{
    bool IsOpen();

    std::uint64_t EditingId();

    void BeginNew(std::string folder = {});
    void BeginEdit(const Vault::Entry& entry);

    void Close();

    bool HasUnsavedChanges(const Vault::Document& doc);

    void RequestClose(const Vault::Document& doc, std::function<void()> then);

    void AbandonForLock();

    bool TakeDiscardedNotice();

    std::uint64_t TakeShowTarget();

    bool Draw(Application& app, VaultSession& session);

    void OpenStandaloneGenerator();

    bool DrawPopups(Application& app, VaultSession& session);
}
