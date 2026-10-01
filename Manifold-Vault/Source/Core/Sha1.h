#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    class Sha1
    {
    public:
        static constexpr std::size_t kDigestBytes = 20;
        static constexpr std::size_t kBlockBytes = 64;

        Sha1() { Reset(); }

        ~Sha1();

        Sha1(const Sha1&) = delete;
        Sha1& operator=(const Sha1&) = delete;

        void Reset();
        void Update(const unsigned char* data, std::size_t len);

        void Final(unsigned char out[kDigestBytes]);

        static void Hash(const unsigned char* data, std::size_t len,
                         unsigned char out[kDigestBytes]);

    private:
        void Compress(const unsigned char block[kBlockBytes]);

        std::uint32_t m_state[5] = {};
        unsigned char m_buffer[kBlockBytes] = {};
        std::size_t m_pending = 0;
        std::uint64_t m_totalBytes = 0;
    };
}
