#include "Core/SecureString.h"

#include <sodium.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace Bunker
{
    const unsigned char SecureString::kEmptyByte = 0;

    SecureArena& SecureArena::Instance() noexcept
    {
        static SecureArena* const instance = new SecureArena();
        return *instance;
    }

    SecureArena::Chunk* SecureArena::ChunkOf(const unsigned char* p) noexcept
    {
        for (const std::unique_ptr<Chunk>& c : m_chunks)
        {
            const unsigned char* base = c->Memory.Data();
            if (p >= base && p < base + kChunkBytes)
                return c.get();
        }
        return nullptr;
    }

    unsigned char* SecureArena::Acquire(std::size_t bytes, std::uint32_t& classOut) noexcept
    {
        std::size_t cls = kClassCount;
        for (std::size_t c = 0; c < kClassCount; ++c)
            if (kClasses[c] >= bytes + 1)
            {
                cls = c;
                break;
            }
        if (cls == kClassCount)
            return nullptr;

        const std::size_t slot = kClasses[cls];

        try
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (!m_free[cls].empty())
            {
                unsigned char* p = m_free[cls].back();
                m_free[cls].pop_back();
                if (Chunk* c = ChunkOf(p))
                    ++c->Live;
                m_bytesInUse += slot;
                classOut = static_cast<std::uint32_t>(cls);
                return p;
            }

            Chunk* target = nullptr;
            for (const std::unique_ptr<Chunk>& c : m_chunks)
                if (c->Used + slot <= kChunkBytes)
                {
                    target = c.get();
                    break;
                }

            if (!target)
            {
                if (m_chunks.size() >= m_chunkLimit)
                    return nullptr;
                auto chunk = std::make_unique<Chunk>();
                chunk->Memory = SecureBuffer(kChunkBytes);
                if (!chunk->Memory.IsValid())
                    return nullptr;
                target = chunk.get();
                m_chunks.push_back(std::move(chunk));
            }

            unsigned char* p = target->Memory.Data() + target->Used;
            target->Used += slot;
            ++target->Live;
            m_bytesInUse += slot;
            classOut = static_cast<std::uint32_t>(cls);
            return p;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    void SecureArena::Release(unsigned char* p, std::uint32_t cls) noexcept
    {
        if (!p || cls >= kClassCount)
            return;

        sodium_memzero(p, kClasses[cls]);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_bytesInUse -= kClasses[cls];

        Chunk* c = ChunkOf(p);
        if (c && c->Live > 0 && --c->Live == 0)
        {
            const unsigned char* base = c->Memory.Data();
            for (std::vector<unsigned char*>& list : m_free)
                list.erase(std::remove_if(list.begin(), list.end(),
                                          [base](const unsigned char* q) {
                                              return q >= base && q < base + kChunkBytes;
                                          }),
                           list.end());
            c->Used = 0;
            return;
        }

        try
        {
            m_free[cls].push_back(p);
        }
        catch (...)
        {
        }
    }

    std::size_t SecureArena::ChunkCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_chunks.size();
    }

    std::size_t SecureArena::BytesInUse() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_bytesInUse;
    }

    std::size_t SecureArena::OverflowCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_overflow;
    }

    std::size_t SecureArena::LostCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lost;
    }

    void SecureArena::NoteOverflow() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_overflow;
    }

    void SecureArena::NoteLoss() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_lost;
    }

    void SecureArena::SetChunkLimitForTests(std::size_t maxChunks) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_chunkLimit = maxChunks;
    }

    void SecureArena::SetHeapFallbackForTests(bool allowed) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_heapFallback = allowed;
    }

    bool SecureArena::HeapFallbackAllowed() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_heapFallback;
    }

    SecureString& SecureString::operator=(const char* z)
    {
        Assign(z ? z : "", z ? std::strlen(z) : 0);
        return *this;
    }

    void SecureString::Assign(const char* p, std::size_t n)
    {
        if (n == 0 || !p)
        {
            Release();
            return;
        }
        if (n > kMaxLength)
        {
            Release();
            SecureArena::Instance().NoteLoss();
            return;
        }

        std::uint32_t cls = kClassNone;
        unsigned char* dst = SecureArena::Instance().Acquire(n, cls);
        if (!dst)
        {
            if (SecureArena::Instance().HeapFallbackAllowed())
                dst = static_cast<unsigned char*>(std::malloc(n + 1));
            if (!dst)
            {
                Release();
                SecureArena::Instance().NoteLoss();
                return;
            }
            cls = kClassHeap;
            SecureArena::Instance().NoteOverflow();
        }

        std::memcpy(dst, p, n);
        dst[n] = 0;

        Release();
        m_data = dst;
        m_len = static_cast<std::uint32_t>(n);
        m_class = cls;
    }

    void SecureString::Wipe() noexcept
    {
        if (m_data)
            sodium_memzero(m_data, static_cast<std::size_t>(m_len) + 1);
        m_len = 0;
    }

    void SecureString::Release() noexcept
    {
        if (!m_data)
        {
            m_len = 0;
            m_class = kClassNone;
            return;
        }
        if (m_class == kClassHeap)
        {
            sodium_memzero(m_data, static_cast<std::size_t>(m_len) + 1);
            std::free(m_data);
        }
        else
        {
            SecureArena::Instance().Release(m_data, m_class);
        }
        m_data = nullptr;
        m_len = 0;
        m_class = kClassNone;
    }
}
