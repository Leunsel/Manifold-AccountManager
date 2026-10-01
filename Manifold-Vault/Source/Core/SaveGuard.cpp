#include "Core/SaveGuard.h"

namespace Bunker
{
    void SaveGuard::Record(const Status& result, double now)
    {
        if (result.IsOk())
        {
            Reset();
            return;
        }

        if (!m_failing)
        {
            m_firstFailure = now;
            m_consecutive = 0;
            m_acknowledged = false;
        }
        m_failing = true;
        ++m_consecutive;
        m_code = result.GetError().Code;
        m_detail = result.GetError().Detail;
    }

    double SaveGuard::SecondsFailing(double now) const
    {
        if (!m_failing)
            return 0.0;
        const double elapsed = now - m_firstFailure;
        return elapsed > 0.0 ? elapsed : 0.0;
    }

    bool SaveGuard::HoldsAutoLock(double now) const
    {
        if (!m_failing || m_acknowledged)
            return false;
        return SecondsFailing(now) < kHoldSeconds;
    }

    void SaveGuard::Acknowledge()
    {
        m_acknowledged = true;
    }

    void SaveGuard::Reset()
    {
        m_failing = false;
        m_acknowledged = false;
        m_consecutive = 0;
        m_firstFailure = 0.0;
        m_code = ErrorCode::Ok;
        m_detail = nullptr;
    }
}
