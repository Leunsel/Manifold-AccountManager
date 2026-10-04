#pragma once

#include "Core/Model.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Bunker::Vault
{
    inline constexpr double kWeakEntropyBits = 50.0;

    inline constexpr std::uint64_t kOldPasswordSeconds = 3ull * 365 * 24 * 3600;

    bool IsCommonPassword(const char* password, std::size_t length);

    struct AuditFinding
    {
        std::uint64_t EntryId = 0;
        bool ReusedPassword = false;
        bool WeakPassword = false;
        bool CommonPassword = false;
        bool NoSecondFactor = false;
        bool OldPassword = false;
        bool InsecureLink = false;

        bool Any() const
        {
            return ReusedPassword || WeakPassword || CommonPassword || NoSecondFactor ||
                   InsecureLink;
        }
    };

    struct AuditReport
    {
        std::size_t Entries = 0;
        std::size_t WithPassword = 0;
        std::size_t WithoutSecondFactor = 0;
        std::size_t SecondFactorNotOffered = 0;
        std::size_t SecondFactorElsewhere = 0;
        std::size_t WeakPasswords = 0;
        std::size_t CommonPasswords = 0;
        std::size_t OldPasswords = 0;
        std::size_t InsecureLinks = 0;

        std::size_t ReusedEntries = 0;
        std::size_t ReuseGroups = 0;

        std::size_t UnprotectedValues = 0;

        std::vector<AuditFinding> Findings;

        bool Clean() const { return Findings.empty(); }
    };

    AuditReport Audit(const Document& doc, std::uint64_t nowUtc);

    constexpr std::size_t kAuditSaltBytes = 32;
    constexpr std::size_t kReusePrintBytes = 32;

    bool ReusePrint(const unsigned char (&salt)[kAuditSaltBytes], const SecureString& value,
                    unsigned char (&out)[kReusePrintBytes]) noexcept;

    AuditReport AuditWithSalt(const Document& doc, std::uint64_t nowUtc,
                              const unsigned char (&salt)[kAuditSaltBytes]);
}
