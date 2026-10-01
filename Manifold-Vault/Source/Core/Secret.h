#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bunker
{
    class SecureBuffer
    {
    public:
        SecureBuffer() = default;
        explicit SecureBuffer(std::size_t size);
        ~SecureBuffer();

        SecureBuffer(SecureBuffer&& other) noexcept;
        SecureBuffer& operator=(SecureBuffer&& other) noexcept;

        SecureBuffer(const SecureBuffer&) = delete;
        SecureBuffer& operator=(const SecureBuffer&) = delete;

        bool IsValid() const { return m_data != nullptr; }
        explicit operator bool() const { return IsValid(); }

        unsigned char* Data() { return m_data; }
        const unsigned char* Data() const { return m_data; }
        std::size_t Size() const { return m_size; }

        void Wipe();

        void Reset();

        void ProtectNoAccess();
        void Unprotect();

    private:
        friend class SecretRegistry;

        unsigned char* m_data = nullptr;
        std::size_t m_size = 0;
        SecureBuffer* m_prev = nullptr;
        SecureBuffer* m_next = nullptr;
    };

    void EmergencyWipe() noexcept;

    bool IsPoisoned() noexcept;

    std::size_t LiveSecretCount() noexcept;

    using SecretRegionHook = void (*)(const void* address, std::size_t size,
                                      bool live) noexcept;

    void SetSecretRegionHook(SecretRegionHook hook) noexcept;

    bool SecretEquals(const void* a, const void* b, std::size_t len);

    void WipeString(std::string& s) noexcept;
    void WipeBytes(std::vector<unsigned char>& v) noexcept;
}
