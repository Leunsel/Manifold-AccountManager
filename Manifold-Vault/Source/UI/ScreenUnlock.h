#pragma once

namespace Bunker
{
    class Application;
    class VaultSession;

    namespace UI
    {
        void DrawGate(Application& app, VaultSession& session);

        bool GateOpening(Application& app, VaultSession& session);

        bool GateHoldsTheme();
    }
}
