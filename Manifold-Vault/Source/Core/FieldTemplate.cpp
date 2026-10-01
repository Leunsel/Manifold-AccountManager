#include "Core/FieldTemplate.h"

namespace Bunker::Vault
{
    namespace
    {
        constexpr TemplateField kGeneric[] = {
            { FieldRole::Username, false },
            { FieldRole::Password, true },
            { FieldRole::Website, false },
        };

        constexpr TemplateField kByEmail[] = {
            { FieldRole::Email, false },
            { FieldRole::Password, true },
            { FieldRole::Website, false },
        };

        struct Entry { const char* PlatformId; const TemplateField* Fields; std::size_t Count; };

        constexpr Entry kCatalog[] = {
            { "google",    kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "microsoft", kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "apple",     kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "amazon",    kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "paypal",    kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "dropbox",   kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "spotify",   kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "ea",        kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
            { "ubisoft",   kByEmail, sizeof(kByEmail) / sizeof(kByEmail[0]) },
        };

        char Lower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsFolded(const char* a, const char* b)
        {
            for (;; ++a, ++b)
            {
                if (Lower(*a) != Lower(*b))
                    return false;
                if (*a == '\0')
                    return true;
            }
        }
    }

    FieldTemplate TemplateFor(const char* platformId)
    {
        if (platformId && *platformId != '\0')
        {
            for (const Entry& e : kCatalog)
                if (EqualsFolded(platformId, e.PlatformId))
                    return FieldTemplate{ e.Fields, e.Count };
        }
        return FieldTemplate{ kGeneric, sizeof(kGeneric) / sizeof(kGeneric[0]) };
    }
}
