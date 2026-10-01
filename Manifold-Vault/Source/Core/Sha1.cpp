#include "Core/Sha1.h"

#include <sodium.h>

#include <cstring>

namespace Bunker
{
    namespace
    {
        template <int N>
        constexpr std::uint32_t Rotl(std::uint32_t v)
        {
            static_assert(N > 0 && N < 32, "rotate count out of range");
            return (v << N) | (v >> (32 - N));
        }
    }

    Sha1::~Sha1()
    {
        sodium_memzero(m_state, sizeof(m_state));
        sodium_memzero(m_buffer, sizeof(m_buffer));
        m_pending = 0;
        m_totalBytes = 0;
    }

    void Sha1::Reset()
    {
        m_state[0] = 0x67452301u;
        m_state[1] = 0xEFCDAB89u;
        m_state[2] = 0x98BADCFEu;
        m_state[3] = 0x10325476u;
        m_state[4] = 0xC3D2E1F0u;
        std::memset(m_buffer, 0, sizeof(m_buffer));
        m_pending = 0;
        m_totalBytes = 0;
    }

    void Sha1::Compress(const unsigned char block[kBlockBytes])
    {
        std::uint32_t w[80];

        for (int i = 0; i < 16; ++i)
        {
            w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = Rotl<1>(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);

        std::uint32_t a = m_state[0];
        std::uint32_t b = m_state[1];
        std::uint32_t c = m_state[2];
        std::uint32_t d = m_state[3];
        std::uint32_t e = m_state[4];

        for (int i = 0; i < 80; ++i)
        {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }

            const std::uint32_t t = Rotl<5>(a) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rotl<30>(b);
            b = a;
            a = t;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;

        sodium_memzero(w, sizeof(w));
        a = b = c = d = e = 0;
        sodium_memzero(&a, sizeof(a));
        sodium_memzero(&b, sizeof(b));
        sodium_memzero(&c, sizeof(c));
        sodium_memzero(&d, sizeof(d));
        sodium_memzero(&e, sizeof(e));
    }

    void Sha1::Update(const unsigned char* data, std::size_t len)
    {
        if (!data || len == 0)
            return;
        m_totalBytes += len;

        if (m_pending > 0)
        {
            const std::size_t room = kBlockBytes - m_pending;
            const std::size_t take = (len < room) ? len : room;
            std::memcpy(m_buffer + m_pending, data, take);
            m_pending += take;
            data += take;
            len -= take;
            if (m_pending == kBlockBytes)
            {
                Compress(m_buffer);
                m_pending = 0;
            }
        }

        while (len >= kBlockBytes)
        {
            Compress(data);
            data += kBlockBytes;
            len -= kBlockBytes;
        }

        if (len > 0)
        {
            std::memcpy(m_buffer + m_pending, data, len);
            m_pending += len;
        }
    }

    void Sha1::Final(unsigned char out[kDigestBytes])
    {
        const std::uint64_t bits = m_totalBytes * 8u;

        unsigned char pad[kBlockBytes * 2] = {};
        pad[0] = 0x80;
        const std::size_t padLen =
            (m_pending < 56) ? (56 - m_pending) : (120 - m_pending);
        Update(pad, padLen);

        unsigned char lengthBytes[8];
        for (int i = 0; i < 8; ++i)
            lengthBytes[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFF);
        Update(lengthBytes, sizeof(lengthBytes));

        for (int i = 0; i < 5; ++i)
        {
            out[i * 4 + 0] = static_cast<unsigned char>((m_state[i] >> 24) & 0xFF);
            out[i * 4 + 1] = static_cast<unsigned char>((m_state[i] >> 16) & 0xFF);
            out[i * 4 + 2] = static_cast<unsigned char>((m_state[i] >> 8) & 0xFF);
            out[i * 4 + 3] = static_cast<unsigned char>((m_state[i]) & 0xFF);
        }
    }

    void Sha1::Hash(const unsigned char* data, std::size_t len,
                    unsigned char out[kDigestBytes])
    {
        Sha1 h;
        h.Update(data, len);
        h.Final(out);
    }
}
