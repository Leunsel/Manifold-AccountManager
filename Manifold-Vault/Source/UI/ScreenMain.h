#pragma once

namespace Bunker
{
    class Application;
    class VaultSession;

    namespace UI
    {
        void DrawMain(Application& app, VaultSession& session);

        void ResetMainScreenState();
    }
}
