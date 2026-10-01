#include "Core/VaultAudit.h"

#include "Core/Crypto.h"
#include "Core/FieldRole.h"
#include "Core/PasswordHistory.h"
#include "Core/PasswordStrength.h"
#include "Core/Random.h"
#include "Generated/CommonPasswords.h"

#include <sodium.h>

#include <algorithm>
#include <cstring>

namespace Bunker::Vault
{
    namespace
    {
        const Field* PasswordOf(const Entry& e)
        {
            for (const Field& f : e.Fields)
                if (f.Role == FieldRole::Password && !f.Value.Empty())
                    return &f;
            return nullptr;
        }

        struct Print
        {
            unsigned char Bytes[kReusePrintBytes] = {};
            std::uint64_t EntryId = 0;
        };

        bool SamePrint(const Print& a, const Print& b)
        {
            return std::memcmp(a.Bytes, b.Bytes, Crypto::kHashBytes) == 0;
        }

        std::uint64_t PasswordSince(const Entry& e)
        {
            const std::uint64_t changed = CurrentPasswordSince(e);
            return changed != 0 ? changed : e.CreatedUtc;
        }
    }

    bool IsCommonPassword(const char* password, std::size_t length)
    {
        if (!password || length == 0)
            return false;

        std::uint64_t h = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < length; ++i)
        {
            auto c = static_cast<unsigned char>(password[i]);
            if (c >= 'A' && c <= 'Z')
                c = static_cast<unsigned char>(c - 'A' + 'a');
            h ^= c;
            h *= 0x100000001b3ull;
        }

        const std::uint64_t* first = kCommonPasswordHashes;
        const std::uint64_t* last =
            kCommonPasswordHashes +
            sizeof(kCommonPasswordHashes) / sizeof(kCommonPasswordHashes[0]);
        return std::binary_search(first, last, h);
    }

    static_assert(kReusePrintBytes == Crypto::kHashBytes,
                  "the fingerprint is a full BLAKE2b hash");

    bool ReusePrint(const unsigned char (&salt)[kAuditSaltBytes], const SecureString& value,
                    unsigned char (&out)[kReusePrintBytes]) noexcept
    {
        crypto_generichash_state state;
        const bool hashed =
            crypto_generichash_init(&state, nullptr, 0, sizeof(out)) == 0 &&
            crypto_generichash_update(&state, salt, sizeof(salt)) == 0 &&
            crypto_generichash_update(&state, value.Bytes(), value.Size()) == 0 &&
            crypto_generichash_final(&state, out, sizeof(out)) == 0;
        sodium_memzero(&state, sizeof(state));
        return hashed;
    }

    AuditReport Audit(const Document& doc, std::uint64_t nowUtc)
    {
        unsigned char salt[kAuditSaltBytes] = {};
        SystemRandom().Fill(salt, sizeof(salt));
        AuditReport report = AuditWithSalt(doc, nowUtc, salt);
        sodium_memzero(salt, sizeof(salt));
        return report;
    }

    AuditReport AuditWithSalt(const Document& doc, std::uint64_t nowUtc,
                              const unsigned char (&salt)[kAuditSaltBytes])
    {
        AuditReport report;
        report.Entries = doc.Entries.size();

        auto unprotected = [](const SecureString& s) {
            return !s.Empty() && !s.InArena();
        };
        for (const Entry& e : doc.Entries)
        {
            for (const Field& f : e.Fields)
                report.UnprotectedValues += unprotected(f.Value) ? 1 : 0;
            report.UnprotectedValues += unprotected(e.Totp.Config.SecretBase32) ? 1 : 0;
            for (const RecoveryCode& c : e.RecoveryCodes)
                report.UnprotectedValues += unprotected(c.Value) ? 1 : 0;
            for (const ListItem& item : e.ListItems)
                report.UnprotectedValues += unprotected(item.Secret) ? 1 : 0;
        }

        std::vector<Print> prints;
        prints.reserve(doc.Entries.size());

        std::vector<AuditFinding> perEntry;
        perEntry.reserve(doc.Entries.size());

        for (const Entry& e : doc.Entries)
        {
            AuditFinding finding;
            finding.EntryId = e.Id;
            if (e.SecondFactorState == SecondFactorNotOffered)
                ++report.SecondFactorNotOffered;
            else if (e.SecondFactorState == SecondFactorElsewhere)
                ++report.SecondFactorElsewhere;

            finding.NoSecondFactor =
                !e.HasTotp && !SecondFactorIsHandled(e.SecondFactorState);
            if (finding.NoSecondFactor)
                ++report.WithoutSecondFactor;

            if (const Field* pw = PasswordOf(e))
            {
                ++report.WithPassword;

                const PasswordStrength strength =
                    EstimateStrength(pw->Value.CStr(), pw->Value.Size(),
                                     Crypto::KdfParams::Moderate());
                if (strength.EntropyBits < kWeakEntropyBits)
                {
                    finding.WeakPassword = true;
                    ++report.WeakPasswords;
                }

                if (IsCommonPassword(pw->Value.CStr(), pw->Value.Size()))
                {
                    finding.CommonPassword = true;
                    ++report.CommonPasswords;
                }

                const std::uint64_t since = PasswordSince(e);
                if (nowUtc != 0 && since != 0 && nowUtc > since &&
                    (nowUtc - since) >= kOldPasswordSeconds)
                {
                    finding.OldPassword = true;
                    ++report.OldPasswords;
                }

                Print print;
                print.EntryId = e.Id;
                const bool hashed = ReusePrint(salt, pw->Value, print.Bytes);
                if (hashed)
                    prints.push_back(print);
                sodium_memzero(&print, sizeof(print));
            }

            perEntry.push_back(finding);
        }

        std::vector<bool> counted(prints.size(), false);
        for (std::size_t i = 0; i < prints.size(); ++i)
        {
            if (counted[i])
                continue;
            std::size_t group = 0;
            for (std::size_t k = i + 1; k < prints.size(); ++k)
            {
                if (counted[k] || !SamePrint(prints[i], prints[k]))
                    continue;
                counted[k] = true;
                ++group;
            }
            if (group == 0)
                continue;

            counted[i] = true;
            ++report.ReuseGroups;
            report.ReusedEntries += group + 1;

            for (std::size_t k = i; k < prints.size(); ++k)
            {
                if (!SamePrint(prints[i], prints[k]))
                    continue;
                for (AuditFinding& f : perEntry)
                    if (f.EntryId == prints[k].EntryId)
                        f.ReusedPassword = true;
            }
        }

        for (const AuditFinding& f : perEntry)
            if (f.Any())
                report.Findings.push_back(f);

        if (!prints.empty())
            sodium_memzero(prints.data(), prints.size() * sizeof(Print));
        return report;
    }
}
