#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Secret.h"

namespace Bunker
{
    class SecureArena
    {
    public:
        static SecureArena& Instance() noexcept;

        static constexpr std::size_t kChunkBytes = 256u * 1024u;

        static constexpr std::size_t kBudgetBytes = 4u * 1024u * 1024u;
        static constexpr std::size_t kDefaultChunkLimit = kBudgetBytes / kChunkBytes;

        static constexpr std::size_t kClassCount = 18;
        static constexpr std::size_t kClasses[kClassCount] = {
            16, 32, 48, 64, 96, 128, 192, 256, 384, 512,
            768, 1024, 1536, 2048, 3072, 4096, 6144, 8448 };

        unsigned char* Acquire(std::size_t bytes, std::uint32_t& classOut) noexcept;

        void Release(unsigned char* p, std::uint32_t cls) noexcept;

        std::size_t ChunkCount() const noexcept;
        std::size_t BytesInUse() const noexcept;
        std::size_t OverflowCount() const noexcept;
        std::size_t LostCount() const noexcept;
        void NoteOverflow() noexcept;
        void NoteLoss() noexcept;

        void SetChunkLimitForTests(std::size_t maxChunks) noexcept;

        void SetHeapFallbackForTests(bool allowed) noexcept;
        bool HeapFallbackAllowed() const noexcept;

    private:
        SecureArena() = default;

        struct Chunk
        {
            SecureBuffer Memory;
            std::size_t Used = 0;
            std::size_t Live = 0;
        };

        Chunk* ChunkOf(const unsigned char* p) noexcept;

        mutable std::mutex m_mutex;
        std::vector<std::unique_ptr<Chunk>> m_chunks;
        std::vector<unsigned char*> m_free[kClassCount];
        std::size_t m_bytesInUse = 0;
        std::size_t m_overflow = 0;
        std::size_t m_lost = 0;
        std::size_t m_chunkLimit = kDefaultChunkLimit;
        bool m_heapFallback = true;
    };

    class SecureString
    {
    public:
        SecureString() noexcept = default;

        SecureString(const char* z) { *this = z; }

        SecureString(const SecureString& other) { Assign(other.CStr(), other.m_len); }
        SecureString(SecureString&& other) noexcept
            : m_data(other.m_data), m_len(other.m_len), m_class(other.m_class)
        {
            other.m_data = nullptr;
            other.m_len = 0;
            other.m_class = kClassNone;
        }
        SecureString& operator=(const SecureString& other)
        {
            if (this != &other)
                Assign(other.CStr(), other.m_len);
            return *this;
        }
        SecureString& operator=(SecureString&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_data = other.m_data;
                m_len = other.m_len;
                m_class = other.m_class;
                other.m_data = nullptr;
                other.m_len = 0;
                other.m_class = kClassNone;
            }
            return *this;
        }
        ~SecureString() { Release(); }

        SecureString& operator=(const char* z);
        SecureString& operator=(const std::string& s)
        {
            Assign(s.data(), s.size());
            return *this;
        }
        SecureString& operator=(std::string_view s)
        {
            Assign(s.data(), s.size());
            return *this;
        }

        void Assign(const char* p, std::size_t n);
        void Assign(const unsigned char* p, std::size_t n)
        {
            Assign(reinterpret_cast<const char*>(p), n);
        }

        void Wipe() noexcept;

        std::size_t Size() const noexcept { return m_len; }
        bool Empty() const noexcept { return m_len == 0; }

        const char* CStr() const noexcept
        {
            return m_data ? reinterpret_cast<const char*>(m_data) : "";
        }
        const unsigned char* Bytes() const noexcept
        {
            return m_data ? m_data : &kEmptyByte;
        }
        std::string_view View() const noexcept { return { CStr(), m_len }; }

        std::string ToStdString() const { return std::string(CStr(), m_len); }

        bool operator==(const SecureString& o) const noexcept
        {
            return m_len == o.m_len &&
                   (m_len == 0 || SecretEquals(Bytes(), o.Bytes(), m_len));
        }
        bool operator==(std::string_view s) const noexcept
        {
            return m_len == s.size() &&
                   (m_len == 0 || SecretEquals(Bytes(), s.data(), m_len));
        }
        bool operator==(const char* z) const noexcept
        {
            return *this == std::string_view(z ? z : "");
        }

        const unsigned char* PayloadForTests() const noexcept { return m_data; }
        std::size_t SlotBytesForTests() const noexcept
        {
            return m_class < SecureArena::kClassCount ? SecureArena::kClasses[m_class] : 0;
        }

        bool InArena() const noexcept { return m_data && m_class < SecureArena::kClassCount; }

        static constexpr std::uint32_t kClassNone = 0xFFFFFFFFu;
        static constexpr std::uint32_t kClassHeap = 0xFFFFFFFEu;

        static constexpr std::size_t kMaxLength = 0xFFFFFFFEu;

    private:
        void Release() noexcept;
        static const unsigned char kEmptyByte;

        unsigned char* m_data = nullptr;
        std::uint32_t m_len = 0;
        std::uint32_t m_class = kClassNone;
    };
}
