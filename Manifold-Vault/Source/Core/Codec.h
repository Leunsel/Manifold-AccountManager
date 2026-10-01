#pragma once

#include "Core/Endian.h"
#include "Core/Result.h"
#include "Core/SecureString.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bunker::Vault
{
    struct UnknownRecord
    {
        std::uint16_t Tag = 0;
        std::vector<unsigned char> Payload;
    };

    class Writer
    {
    public:
        class Plan
        {
        public:
            Plan() = default;
            std::size_t Bytes() const { return m_bytes; }

        private:
            friend class Writer;
            explicit Plan(std::size_t b) : m_bytes(b) {}
            std::size_t m_bytes = 0;
        };

        static Writer Measuring() { return Writer(nullptr, 0); }
        static Writer Filling(const Plan& plan, unsigned char* dst, std::size_t cap);

        Writer(const Writer&) = delete;
        Writer& operator=(const Writer&) = delete;
        Writer(Writer&&) = default;
        Writer& operator=(Writer&&) = default;

        void BeginRecord(std::uint16_t tag);
        void EndRecord();

        void WriteU32(std::uint16_t tag, std::uint32_t value);
        void WriteU64(std::uint16_t tag, std::uint64_t value);
        void WriteBytes(std::uint16_t tag, const unsigned char* data, std::size_t len);
        void WriteString(std::uint16_t tag, const std::string& s);
        void WriteString(std::uint16_t tag, const SecureString& s);
        void WriteString(std::uint16_t tag, const char* z);
        void WriteUnknown(const UnknownRecord& rec);

        const unsigned char* Data() const { return m_out; }
        std::size_t Size() const { return m_size; }

        bool IsBalanced() const { return m_depth == 0; }

        bool Failed() const { return m_failed; }

        bool Measured(Plan& out) const;

        bool Filled() const;

    private:
        Writer(unsigned char* out, std::size_t cap) : m_out(out), m_cap(cap) {}

        void Emit(const unsigned char* src, std::size_t n);
        void WriteText(std::uint16_t tag, const char* p, std::size_t n);
        void Patch(std::size_t at, std::uint32_t value);
        bool Broken() const { return m_failed || m_overrun; }

        static constexpr unsigned kMaxNesting = 8;

        unsigned char* m_out = nullptr;
        std::size_t m_cap = 0;
        std::size_t m_size = 0;
        std::size_t m_planned = 0;
        std::size_t m_open[kMaxNesting] = {};
        unsigned m_depth = 0;
        bool m_failed = false;
        bool m_overrun = false;
    };

    class Reader
    {
    public:
        Reader(const unsigned char* data, std::size_t len) : m_data(data), m_len(len) {}

        bool AtEnd() const { return m_pos >= m_len; }
        std::size_t Position() const { return m_pos; }

        bool Next(std::uint16_t& tag, const unsigned char*& payload, std::size_t& payloadLen);

        bool Failed() const { return m_failed; }

        static bool AsU32(const unsigned char* p, std::size_t len, std::uint32_t& out);
        static bool AsU64(const unsigned char* p, std::size_t len, std::uint64_t& out);
        static bool AsString(const unsigned char* p, std::size_t len, std::string& out);
        static bool AsString(const unsigned char* p, std::size_t len, SecureString& out);

    private:
        const unsigned char* m_data = nullptr;
        std::size_t m_len = 0;
        std::size_t m_pos = 0;
        bool m_failed = false;
    };
}
