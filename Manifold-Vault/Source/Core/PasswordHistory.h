#pragma once

#include "Core/Crypto.h"
#include "Core/Model.h"

#include <cstddef>
#include <cstdint>

namespace Bunker::Vault
{
    inline constexpr std::size_t kMaxPasswordHistory = 12;

    inline constexpr std::size_t kHistoryKeyBytes = 32;

    Status HistoryMac(const Document& doc, std::uint64_t entryId,
                      const unsigned char* password, std::size_t length,
                      unsigned char out[Crypto::kHashBytes]);

    bool WasUsedBefore(const Document& doc, const Entry& entry,
                       const unsigned char* password, std::size_t length);

    std::uint64_t CurrentPasswordSince(const Entry& entry);
}
