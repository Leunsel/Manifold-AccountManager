#pragma once

#include "Core/Codec.h"

#include <cstddef>
#include <vector>

namespace Bunker::Vault
{
    class Body
    {
    public:
        template <class Fn>
        explicit Body(Fn&& build)
        {
            Writer measure = Writer::Measuring();
            build(measure);
            m_balanced = measure.IsBalanced();
            m_failed = measure.Failed();

            Writer::Plan plan;
            if (!measure.Measured(plan))
                return;

            m_bytes.assign(plan.Bytes() + 1, 0);
            Writer fill = Writer::Filling(plan, m_bytes.data(), plan.Bytes());
            build(fill);
            m_ok = fill.Filled();
            m_bytes.resize(m_ok ? plan.Bytes() : 0);
        }

        const unsigned char* data() const { return m_bytes.data(); }
        std::size_t size() const { return m_bytes.size(); }

        std::size_t Size() const { return m_bytes.size(); }

        bool IsBalanced() const { return m_balanced; }
        bool Failed() const { return m_failed; }

        bool Ok() const { return m_ok; }

        bool operator==(const Body& other) const { return m_bytes == other.m_bytes; }
        bool operator!=(const Body& other) const { return !(*this == other); }

    private:
        std::vector<unsigned char> m_bytes;
        bool m_ok = false;
        bool m_balanced = false;
        bool m_failed = false;
    };
}
