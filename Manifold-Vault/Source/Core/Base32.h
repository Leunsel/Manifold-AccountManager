#pragma once

#include "Core/Result.h"
#include "Core/Secret.h"
#include "Core/SecureString.h"

#include <cstddef>

namespace Bunker
{
    std::size_t MaxDecodedBase32Bytes(std::size_t encodedLength);

    std::size_t EncodedBase32Length(std::size_t bytes);

    Result<std::size_t> DecodeBase32(const char* text, std::size_t length,
                                     unsigned char* out, std::size_t capacity);
    Result<std::size_t> DecodeBase32(const char* text, std::size_t length,
                                     SecureBuffer& out);

    Status EncodeBase32(const unsigned char* data, std::size_t length, SecureString& out);
}
