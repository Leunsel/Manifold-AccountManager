#pragma once

#include <cstdint>

namespace Bunker
{
    enum class LockReason
    {
        None = 0,
        Manual,
        Idle,
        SessionLocked,
        Suspend,
        Minimised,
        FocusLost,
        Shutdown,
    };

    const char* ToString(LockReason reason);
    const char* ToUserMessage(LockReason reason);

    struct LockSettings
    {
        std::uint32_t IdleSeconds = 300;

        bool LockOnMinimise = false;

        bool LockOnFocusLost = false;
        double FocusGraceSeconds = 20.0;
    };

    class LockPolicy
    {
    public:
        void SetSettings(const LockSettings& s) { m_settings = s; }
        const LockSettings& Settings() const { return m_settings; }

        void NotifyActivity(double now);

        void NotifyEvent(LockReason reason, double now);

        LockReason Evaluate(double now, bool vaultIsUnlocked);

        double SecondsUntilIdleLock(double now) const;

        void CancelPending();

        void Reset(double now);

    private:
        LockSettings m_settings;
        double m_lastActivity = 0.0;
        LockReason m_pending = LockReason::None;
        double m_pendingSince = 0.0;
    };
}
