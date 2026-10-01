#pragma once

#include <cstddef>
#include <cstdint>

namespace Bunker
{
    class RandomSource
    {
    public:
        virtual ~RandomSource() = default;
        virtual void Fill(unsigned char* out, std::size_t len) = 0;

        std::uint32_t Uniform(std::uint32_t bound);
    };

    RandomSource& SystemRandom();

    class DeterministicRandom : public RandomSource
    {
    public:
        explicit DeterministicRandom(std::uint64_t seed = 1);
        void Fill(unsigned char* out, std::size_t len) override;
        void Reset(std::uint64_t seed);

    private:
        void Refill();

        unsigned char m_key[32] = {};
        unsigned char m_block[64] = {};
        std::size_t m_offset = sizeof(m_block);
        std::uint32_t m_counter = 0;
    };

    class ScriptedRandom : public RandomSource
    {
    public:
        ScriptedRandom(const unsigned char* bytes, std::size_t count)
            : m_bytes(bytes), m_count(count) {}
        void Fill(unsigned char* out, std::size_t len) override;
        std::size_t Consumed() const { return m_consumed; }

    private:
        const unsigned char* m_bytes = nullptr;
        std::size_t m_count = 0;
        std::size_t m_pos = 0;
        std::size_t m_consumed = 0;
    };
}
