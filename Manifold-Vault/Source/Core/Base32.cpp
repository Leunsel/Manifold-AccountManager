#include "Core/Base32.h"

#include <sodium.h>

namespace Bunker
{
    namespace
    {
        constexpr const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

        constexpr int kSkip = -2;
        constexpr int kPad = -3;

        int SymbolValue(unsigned char c)
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a';
            if (c >= '2' && c <= '7') return 26 + (c - '2');
            if (c == '=') return kPad;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-')
                return kSkip;
            return -1;
        }

        struct WipedOnExit
        {
            std::uint32_t Accumulator = 0;
            int BitsHeld = 0;
            ~WipedOnExit() { sodium_memzero(this, sizeof(*this)); }
        };

        Result<std::size_t> Fail(unsigned char* out, std::size_t capacity, const char* detail)
        {
            if (out && capacity)
                sodium_memzero(out, capacity);
            return Error{ ErrorCode::InvalidArgument, detail };
        }
    }

    std::size_t MaxDecodedBase32Bytes(std::size_t encodedLength)
    {
        return (encodedLength * 5u) / 8u;
    }

    std::size_t EncodedBase32Length(std::size_t bytes)
    {
        return (bytes * 8u + 4u) / 5u;
    }

    Result<std::size_t> DecodeBase32(const char* text, std::size_t length,
                                     SecureBuffer& out)
    {
        if (!text)
            return Fail(out.Data(), out.Size(), "no text given");
        if (!out.IsValid())
            return Fail(nullptr, 0, "target buffer not available");
        return DecodeBase32(text, length, out.Data(), out.Size());
    }

    Result<std::size_t> DecodeBase32(const char* text, std::size_t length,
                                     unsigned char* out, std::size_t capacity)
    {
        if (!text)
            return Fail(out, capacity, "no text given");
        if (!out)
            return Fail(nullptr, 0, "target buffer not available");

        WipedOnExit state;
        std::uint32_t& accumulator = state.Accumulator;
        int& bitsHeld = state.BitsHeld;
        std::size_t written = 0;
        std::size_t symbols = 0;
        bool sawPadding = false;
        std::size_t padCount = 0;

        for (std::size_t i = 0; i < length; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const int value = SymbolValue(c);

            if (value == kSkip)
                continue;

            if (value == kPad)
            {
                sawPadding = true;
                ++padCount;
                continue;
            }
            if (value < 0)
                return Fail(out, capacity, "character is not in the Base32 alphabet");

            if (sawPadding)
                return Fail(out, capacity, "character after the padding");

            ++symbols;
            accumulator = (accumulator << 5) | static_cast<std::uint32_t>(value);
            bitsHeld += 5;

            if (bitsHeld >= 8)
            {
                bitsHeld -= 8;
                if (written >= capacity)
                    return Fail(out, capacity, "target buffer too small");
                out[written++] =
                    static_cast<unsigned char>((accumulator >> bitsHeld) & 0xFFu);
            }
        }

        const std::size_t tail = symbols % 8u;
        if (tail == 1 || tail == 3 || tail == 6)
            return Fail(out, capacity, "incomplete Base32 group");

        if (sawPadding && (symbols + padCount) % 8u != 0)
            return Fail(out, capacity, "misplaced padding character");

        if (symbols == 0)
            return Fail(out, capacity, "no Base32 content");

        return written;
    }

    Status EncodeBase32(const unsigned char* data, std::size_t length, SecureString& out)
    {
        if (length == 0)
        {
            out = SecureString();
            return Status{};
        }
        if (!data)
        {
            out = SecureString();
            return Error{ ErrorCode::InvalidArgument, "no data given" };
        }

        const std::size_t n = EncodedBase32Length(length);
        SecureBuffer staging(n + 1);
        if (!staging.IsValid())
        {
            out = SecureString();
            return Error{ ErrorCode::OutOfMemory, "no locked memory" };
        }
        char* text = reinterpret_cast<char*>(staging.Data());

        std::size_t written = 0;
        WipedOnExit state;
        std::uint32_t& accumulator = state.Accumulator;
        int& bitsHeld = state.BitsHeld;
        for (std::size_t i = 0; i < length; ++i)
        {
            accumulator = (accumulator << 8) | data[i];
            bitsHeld += 8;
            while (bitsHeld >= 5)
            {
                bitsHeld -= 5;
                text[written++] = kAlphabet[(accumulator >> bitsHeld) & 0x1Fu];
            }
        }
        if (bitsHeld > 0)
        {
            text[written++] = kAlphabet[(accumulator << (5 - bitsHeld)) & 0x1Fu];
        }

        out.Assign(text, written);
        if (out.Size() != written)
            return Error{ ErrorCode::OutOfMemory, "value could not be stored" };
        return Status{};
    }
}
