#include "Core/FieldRole.h"

#include <cstring>

namespace Bunker::Vault
{
    namespace
    {
        struct Known { const char* Name; FieldRole Role; };

        constexpr Known kLegacyNames[] = {
            { "benutzername",  FieldRole::Username },
            { "username",      FieldRole::Username },
            { "benutzer",      FieldRole::Username },
            { "anmeldename",   FieldRole::Username },
            { "login",         FieldRole::Username },
            { "e-mail",        FieldRole::Email },
            { "email",         FieldRole::Email },
            { "e-mail-adresse", FieldRole::Email },
            { "mail",          FieldRole::Email },
            { "passwort",      FieldRole::Password },
            { "password",      FieldRole::Password },
            { "kennwort",      FieldRole::Password },
            { "website",       FieldRole::Website },
            { "webseite",      FieldRole::Website },
            { "url",           FieldRole::Website },
            { "adresse",       FieldRole::Website },
            { "notizen",       FieldRole::Notes },
            { "notiz",         FieldRole::Notes },
            { "notes",         FieldRole::Notes },
            { "anmerkungen",   FieldRole::Notes },
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

    const char* CanonicalFieldName(FieldRole role)
    {
        switch (role)
        {
        case FieldRole::Username: return "Username";
        case FieldRole::Email:    return "Email";
        case FieldRole::Password: return "Password";
        case FieldRole::Website:  return "Website";
        case FieldRole::Notes:    return "Notes";
        case FieldRole::Custom:
        default:
            return "";
        }
    }

    Str FieldRoleText(FieldRole role)
    {
        switch (role)
        {
        case FieldRole::Username: return Str::Field_Username;
        case FieldRole::Email:    return Str::Field_Email;
        case FieldRole::Password: return Str::Field_Password;
        case FieldRole::Website:  return Str::Field_Website;
        case FieldRole::Notes:    return Str::Field_Notes;
        case FieldRole::Custom:
        default:
            return Str::Count;
        }
    }

    FieldRole RoleFromLegacyName(const char* name)
    {
        if (!name || *name == '\0')
            return FieldRole::Custom;
        for (const Known& k : kLegacyNames)
            if (EqualsFolded(name, k.Name))
                return k.Role;
        return FieldRole::Custom;
    }
}
