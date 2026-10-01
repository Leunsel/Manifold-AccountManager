#include "Core/PasswordHistory.h"

#include "Core/Endian.h"

#include <sodium.h>

#include <cstring>

namespace Bunker::Vault
{
    Status HistoryMac(const Document& doc, std::uint64_t entryId,
                      const unsigned char* password, std::size_t length,
                      unsigned char out[Crypto::kHashBytes])
    {
        if (doc.HistoryKey.size() != kHistoryKeyBytes)
            return Error{ ErrorCode::InvalidArgument, "no history key" };
        if (!password || length == 0)
            return Error{ ErrorCode::InvalidArgument, "empty password" };

        crypto_generichash_state state;
        if (crypto_generichash_init(&state, doc.HistoryKey.data(),
                                    doc.HistoryKey.size(), Crypto::kHashBytes) != 0)
            return Error{ ErrorCode::EncryptFailed, "generichash_init" };

        unsigned char id[8];
        StoreU64Le(id, entryId);
        crypto_generichash_update(&state, id, sizeof(id));
        crypto_generichash_update(&state, password, length);

        if (crypto_generichash_final(&state, out, Crypto::kHashBytes) != 0)
            return Error{ ErrorCode::EncryptFailed, "generichash_final" };
        return Status{};
    }

    bool WasUsedBefore(const Document& doc, const Entry& entry,
                       const unsigned char* password, std::size_t length)
    {
        if (entry.PasswordHistory.empty() || !password || length == 0)
            return false;

        unsigned char mac[Crypto::kHashBytes] = {};
        if (!HistoryMac(doc, entry.Id, password, length, mac).IsOk())
            return false;

        for (const PasswordMemory& old : entry.PasswordHistory)
        {
            if (sodium_memcmp(old.Mac, mac, Crypto::kHashBytes) == 0)
                return true;
        }
        return false;
    }

    std::uint64_t CurrentPasswordSince(const Entry& entry)
    {
        std::uint64_t newest = 0;
        for (const PasswordMemory& old : entry.PasswordHistory)
            if (old.RetiredUtc > newest)
                newest = old.RetiredUtc;
        return newest;
    }
}
