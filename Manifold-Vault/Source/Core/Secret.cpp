#include "Core/Secret.h"

#include <sodium.h>

#include <atomic>
#include <mutex>

namespace Bunker
{
    namespace
    {
        std::mutex g_registryMutex;
        SecureBuffer* g_head = nullptr;
        std::size_t g_liveCount = 0;
        std::atomic<bool> g_poisoned{ false };

        std::atomic<SecretRegionHook> g_regionHook{ nullptr };

        void TellRegion(const void* address, std::size_t size, bool live) noexcept
        {
            if (const SecretRegionHook hook = g_regionHook.load(std::memory_order_acquire))
                hook(address, size, live);
        }
    }

    void SetSecretRegionHook(SecretRegionHook hook) noexcept
    {
        g_regionHook.store(hook, std::memory_order_release);
    }

    class SecretRegistry
    {
    public:
        static void Add(SecureBuffer* b)
        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            b->m_prev = nullptr;
            b->m_next = g_head;
            if (g_head)
                g_head->m_prev = b;
            g_head = b;
            ++g_liveCount;
        }

        static void Remove(SecureBuffer* b)
        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            if (b->m_prev)
                b->m_prev->m_next = b->m_next;
            else if (g_head == b)
                g_head = b->m_next;
            if (b->m_next)
                b->m_next->m_prev = b->m_prev;
            b->m_prev = nullptr;
            b->m_next = nullptr;
            if (g_liveCount > 0)
                --g_liveCount;
        }

        static void Replace(SecureBuffer* oldB, SecureBuffer* newB)
        {
            std::lock_guard<std::mutex> lock(g_registryMutex);
            newB->m_prev = oldB->m_prev;
            newB->m_next = oldB->m_next;
            if (newB->m_prev)
                newB->m_prev->m_next = newB;
            else if (g_head == oldB)
                g_head = newB;
            if (newB->m_next)
                newB->m_next->m_prev = newB;
            oldB->m_prev = nullptr;
            oldB->m_next = nullptr;
        }

        static void WipeAllPayloads() noexcept
        {
            for (SecureBuffer* b = g_head; b != nullptr; b = b->m_next)
            {
                if (b->m_data && b->m_size)
                {
                    sodium_mprotect_readwrite(b->m_data);
                    sodium_memzero(b->m_data, b->m_size);
                }
            }
        }
    };

    SecureBuffer::SecureBuffer(std::size_t size)
    {
        if (size == 0)
            return;
        void* p = sodium_malloc(size);
        if (!p)
            return;
        m_data = static_cast<unsigned char*>(p);
        m_size = size;
        sodium_memzero(m_data, m_size);
        SecretRegistry::Add(this);
        TellRegion(m_data, m_size, true);
    }

    SecureBuffer::~SecureBuffer()
    {
        Reset();
    }

    SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size)
    {
        if (m_data)
        {
            SecretRegistry::Replace(&other, this);
            other.m_data = nullptr;
            other.m_size = 0;
        }
    }

    SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept
    {
        if (this == &other)
            return *this;
        Reset();
        m_data = other.m_data;
        m_size = other.m_size;
        if (m_data)
        {
            SecretRegistry::Replace(&other, this);
            other.m_data = nullptr;
            other.m_size = 0;
        }
        return *this;
    }

    void SecureBuffer::Wipe()
    {
        if (!m_data || m_size == 0)
            return;
        sodium_mprotect_readwrite(m_data);
        sodium_memzero(m_data, m_size);
    }

    void SecureBuffer::Reset()
    {
        if (!m_data)
            return;

        if (g_poisoned.load(std::memory_order_acquire))
        {
            m_data = nullptr;
            m_size = 0;
            return;
        }

        SecretRegistry::Remove(this);
        TellRegion(m_data, m_size, false);
        sodium_mprotect_readwrite(m_data);
        sodium_free(m_data);
        m_data = nullptr;
        m_size = 0;
    }

    void SecureBuffer::ProtectNoAccess()
    {
        if (m_data)
            sodium_mprotect_noaccess(m_data);
    }

    void SecureBuffer::Unprotect()
    {
        if (m_data)
            sodium_mprotect_readwrite(m_data);
    }

    void WipeString(std::string& s) noexcept
    {
        if (s.capacity() == 0)
            return;
        s.resize(s.capacity(), '\0');
        sodium_memzero(s.data(), s.size());
        s.clear();
    }

    void WipeBytes(std::vector<unsigned char>& v) noexcept
    {
        if (v.capacity() == 0)
            return;
        v.resize(v.capacity(), 0);
        sodium_memzero(v.data(), v.size());
        v.clear();
    }

    void EmergencyWipe() noexcept
    {
        SecretRegistry::WipeAllPayloads();
        g_poisoned.store(true, std::memory_order_release);
    }

    bool IsPoisoned() noexcept
    {
        return g_poisoned.load(std::memory_order_acquire);
    }

    std::size_t LiveSecretCount() noexcept
    {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        return g_liveCount;
    }

    bool SecretEquals(const void* a, const void* b, std::size_t len)
    {
        if (!a || !b)
            return false;
        return sodium_memcmp(a, b, len) == 0;
    }
}
