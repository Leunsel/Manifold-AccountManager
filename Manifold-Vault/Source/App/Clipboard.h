#pragma once

#include <cstddef>
#include <string>

namespace Bunker
{
    class SecretClipboard
    {
    public:
        bool CopySecret(const char* text, std::size_t length, double clearAfterSeconds,
                        double now);

        bool CopyPlain(const char* text, std::size_t length);

        void Poll(double now);

        void ClearIfOurs();

        bool HasPendingClear() const { return m_armed; }
        double SecondsUntilClear(double now) const;

        const std::string& LastLabel() const { return m_lastLabel; }
        double LastCopyTime() const { return m_lastCopyTime; }

        void SetLastLabel(std::string label) { m_lastLabel = std::move(label); }

    private:
        bool m_armed = false;
        double m_clearAt = 0.0;
        unsigned long m_sequenceAtCopy = 0;
        std::string m_lastLabel;
        double m_lastCopyTime = -1000.0;
    };
}
