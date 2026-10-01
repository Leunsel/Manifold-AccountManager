#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Bunker
{
    enum class ErrorCode : std::uint16_t
    {
        Ok = 0,

        OutOfMemory,
        InvalidArgument,
        NotInitialised,

        CryptoSelfTestFailed,
        KdfFailed,
        EncryptFailed,
        DecryptFailed,

        WrongPassword,
        FileTamperedOrCorrupt,
        UnsupportedFormatVersion,
        FileTooLarge,
        FileWriteFailed,
        VaultChangedOnDisk,

        OtpSecretInvalid,
        OtpUriUnsupported,
    };

    const char* ToString(ErrorCode code);

    const char* ToUserMessage(ErrorCode code);

    struct Error
    {
        ErrorCode Code = ErrorCode::Ok;
        const char* Detail = nullptr;

        constexpr Error() = default;
        constexpr Error(ErrorCode code, const char* detail = nullptr)
            : Code(code), Detail(detail) {}

        constexpr bool IsOk() const { return Code == ErrorCode::Ok; }
    };

    template <typename T>
    class Result
    {
    public:
        Result(T value) : m_value(std::move(value)), m_error() {}
        Result(Error error) : m_value(), m_error(error) {}

        bool IsOk() const { return m_error.IsOk(); }
        explicit operator bool() const { return IsOk(); }

        const Error& GetError() const { return m_error; }
        ErrorCode Code() const { return m_error.Code; }

        T& Value() { return m_value; }
        const T& Value() const { return m_value; }

    private:
        T m_value;
        Error m_error;
    };

    template <>
    class Result<void>
    {
    public:
        Result() : m_error() {}
        Result(Error error) : m_error(error) {}

        bool IsOk() const { return m_error.IsOk(); }
        explicit operator bool() const { return IsOk(); }

        const Error& GetError() const { return m_error; }
        ErrorCode Code() const { return m_error.Code; }

    private:
        Error m_error;
    };

    using Status = Result<void>;

    inline Status Ok() { return Status{}; }
    inline Status Fail(ErrorCode code, const char* detail = nullptr)
    {
        return Status{ Error{ code, detail } };
    }
}
