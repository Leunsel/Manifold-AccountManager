#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Bunker
{
    class Application;
    class VaultSession;

    namespace Vault
    {
        struct AuditFinding;
    }

    namespace UI::Audit
    {
        bool IsOpen();
        void Open();
        void Close();

        bool Draw(Application& app, VaultSession& session);

        std::size_t FindingCount(VaultSession& session);

        const Vault::AuditFinding* FindingFor(VaultSession& session, std::uint64_t entryId);

        bool IsSerious(const Vault::AuditFinding& finding);

        std::string TagsOf(const Vault::AuditFinding& finding);

        enum class FindingKind : std::uint8_t
        {
            None,
            Reused,
            Weak,
            Common,
            NoTotp,
            Insecure,
        };
        FindingKind TakeFilterRequest();

        void EntriesWith(VaultSession& session, FindingKind kind,
                         std::vector<std::uint64_t>& out);

        const char* KindName(FindingKind kind);

        std::uint64_t TakeJumpTarget();

        void Forget();
    }
}
