#pragma once

#include "Core/Result.h"

namespace Bunker
{
    class SaveGuard
    {
    public:
        static constexpr double kHoldSeconds = 600.0;

        void Record(const Status& result, double now);

        bool Failing() const { return m_failing; }
        ErrorCode Code() const { return m_code; }

        const char* Detail() const { return m_detail; }

        int ConsecutiveFailures() const { return m_consecutive; }

        double SecondsFailing(double now) const;

        bool HoldsAutoLock(double now) const;

        void Acknowledge();

        void Reset();

    private:
        bool m_failing = false;
        bool m_acknowledged = false;
        int m_consecutive = 0;
        double m_firstFailure = 0.0;
        ErrorCode m_code = ErrorCode::Ok;
        const char* m_detail = nullptr;
    };
}
