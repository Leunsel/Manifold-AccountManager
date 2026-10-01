#include "Core/Random.h"

#include "Core/Endian.h"

#include <sodium.h>

#include <cstring>

namespace Bunker
{
    std::uint32_t RandomSource::Uniform(std::uint32_t bound)
    {
        if (bound < 2)
            return 0;

        const std::uint32_t limit = std::uint32_t(0xFFFFFFFFu) -
                                    (std::uint32_t(0xFFFFFFFFu) % bound);
        for (;;)
        {
            unsigned char raw[4];
            Fill(raw, sizeof(raw));
            const std::uint32_t value = LoadU32Le(raw);
            if (value < limit)
                return value % bound;
        }
    }

    namespace
    {
        class LibsodiumRandom : public RandomSource
        {
        public:
            void Fill(unsigned char* out, std::size_t len) override
            {
                if (out && len)
                    randombytes_buf(out, len);
            }
        };
    }

    RandomSource& SystemRandom()
    {
        static LibsodiumRandom source;
        return source;
    }

    DeterministicRandom::DeterministicRandom(std::uint64_t seed)
    {
        Reset(seed);
    }

    void DeterministicRandom::Reset(std::uint64_t seed)
    {
        std::memset(m_key, 0, sizeof(m_key));
        StoreU64Le(m_key, seed);
        m_counter = 0;
        m_offset = sizeof(m_block);
    }

    void DeterministicRandom::Refill()
    {
        unsigned char zeros[sizeof(m_block)] = {};
        unsigned char nonce[crypto_stream_chacha20_ietf_NONCEBYTES] = {};
        crypto_stream_chacha20_ietf_xor_ic(m_block, zeros, sizeof(zeros),
                                           nonce, m_counter, m_key);
        ++m_counter;
        m_offset = 0;
    }

    void DeterministicRandom::Fill(unsigned char* out, std::size_t len)
    {
        if (!out)
            return;
        while (len > 0)
        {
            if (m_offset >= sizeof(m_block))
                Refill();
            const std::size_t take = (len < sizeof(m_block) - m_offset)
                                         ? len
                                         : sizeof(m_block) - m_offset;
            std::memcpy(out, m_block + m_offset, take);
            m_offset += take;
            out += take;
            len -= take;
        }
    }

    void ScriptedRandom::Fill(unsigned char* out, std::size_t len)
    {
        for (std::size_t i = 0; i < len; ++i)
        {
            out[i] = (m_count > 0) ? m_bytes[m_pos] : 0;
            if (m_count > 0)
                m_pos = (m_pos + 1) % m_count;
            ++m_consumed;
        }
    }
}
