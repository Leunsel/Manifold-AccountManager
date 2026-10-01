#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    inline void StoreU16Le(unsigned char* out, std::uint16_t v)
    {
        out[0] = static_cast<unsigned char>(v & 0xFF);
        out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    }

    inline void StoreU32Le(unsigned char* out, std::uint32_t v)
    {
        out[0] = static_cast<unsigned char>(v & 0xFF);
        out[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        out[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
        out[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
    }

    inline void StoreU64Le(unsigned char* out, std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            out[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
    }

    inline std::uint16_t LoadU16Le(const unsigned char* in)
    {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(in[0]) |
            (static_cast<std::uint16_t>(in[1]) << 8));
    }

    inline std::uint32_t LoadU32Le(const unsigned char* in)
    {
        return static_cast<std::uint32_t>(in[0]) |
               (static_cast<std::uint32_t>(in[1]) << 8) |
               (static_cast<std::uint32_t>(in[2]) << 16) |
               (static_cast<std::uint32_t>(in[3]) << 24);
    }

    inline std::uint64_t LoadU64Le(const unsigned char* in)
    {
        std::uint64_t v = 0;
        for (int i = 7; i >= 0; --i)
            v = (v << 8) | static_cast<std::uint64_t>(in[i]);
        return v;
    }
}
