#pragma once

#include "Lang/Strings.h"

#include <cstdint>

namespace Bunker::Vault
{
    enum class FieldRole : std::uint32_t
    {
        Custom = 0,
        Username = 1,
        Email = 2,
        Password = 3,
        Website = 4,
        Notes = 5,
    };

    inline constexpr std::uint32_t kFieldRoleMax = 5;

    const char* CanonicalFieldName(FieldRole role);

    Str FieldRoleText(FieldRole role);

    FieldRole RoleFromLegacyName(const char* name);
}
