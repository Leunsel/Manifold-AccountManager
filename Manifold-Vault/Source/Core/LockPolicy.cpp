#include "Core/Language.h"
#include "Core/LockPolicy.h"

namespace Bunker
{
    const char* ToString(LockReason reason)
    {
        switch (reason)
        {
        case LockReason::None:          return "None";
        case LockReason::Manual:        return "Manual";
        case LockReason::Idle:          return "Idle";
        case LockReason::SessionLocked: return "SessionLocked";
        case LockReason::Suspend:       return "Suspend";
        case LockReason::Minimised:     return "Minimised";
        case LockReason::FocusLost:     return "FocusLost";
        case LockReason::Shutdown:      return "Shutdown";
        }
        return "Unknown";
    }

    const char* ToUserMessage(LockReason reason)
    {
        switch (reason)
        {
        case LockReason::None:          return "";
        case LockReason::Manual:        return T(Str::Lock_Manual);
        case LockReason::Idle:          return T(Str::Lock_Idle);
        case LockReason::SessionLocked: return T(Str::Lock_SessionLocked);
        case LockReason::Suspend:       return T(Str::Lock_Suspend);
        case LockReason::Minimised:     return T(Str::Lock_Minimised);
        case LockReason::FocusLost:     return T(Str::Lock_FocusLost);
        case LockReason::Shutdown:      return T(Str::Lock_Shutdown);
        }
        return T(Str::Lock_Manual);
    }

    void LockPolicy::NotifyActivity(double now)
    {
        m_lastActivity = now;

        if (m_pending == LockReason::FocusLost)
            m_pending = LockReason::None;
    }

    void LockPolicy::NotifyEvent(LockReason reason, double now)
    {
        if (reason == LockReason::None)
            return;

        auto weight = [](LockReason r) {
            switch (r)
            {
            case LockReason::Shutdown:      return 6;
            case LockReason::Suspend:       return 5;
            case LockReason::SessionLocked: return 4;
            case LockReason::Manual:        return 3;
            case LockReason::Idle:          return 2;
            case LockReason::Minimised:     return 1;
            case LockReason::FocusLost:     return 0;
            case LockReason::None:          return -1;
            }
            return -1;
        };

        if (weight(reason) >= weight(m_pending))
        {
            m_pending = reason;
            m_pendingSince = now;
        }
    }

    LockReason LockPolicy::Evaluate(double now, bool vaultIsUnlocked)
    {
        if (!vaultIsUnlocked)
        {
            m_pending = LockReason::None;
            return LockReason::None;
        }

        if (m_pending != LockReason::None)
        {
            const bool deferred = (m_pending == LockReason::FocusLost);
            if (!deferred)
            {
                const LockReason fired = m_pending;
                m_pending = LockReason::None;
                return fired;
            }
            if (now - m_pendingSince >= m_settings.FocusGraceSeconds)
            {
                m_pending = LockReason::None;
                return LockReason::FocusLost;
            }
        }

        if (m_settings.IdleSeconds > 0 &&
            (now - m_lastActivity) >= static_cast<double>(m_settings.IdleSeconds))
        {
            return LockReason::Idle;
        }

        return LockReason::None;
    }

    double LockPolicy::SecondsUntilIdleLock(double now) const
    {
        if (m_settings.IdleSeconds == 0)
            return -1.0;
        const double remaining =
            static_cast<double>(m_settings.IdleSeconds) - (now - m_lastActivity);
        return remaining > 0.0 ? remaining : 0.0;
    }

    void LockPolicy::CancelPending()
    {
        m_pending = LockReason::None;
    }

    void LockPolicy::Reset(double now)
    {
        m_lastActivity = now;
        m_pending = LockReason::None;
        m_pendingSince = 0.0;
    }
}
