#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/FieldRole.h"
#include "Core/FieldTemplate.h"
#include "Core/Model.h"
#include "Core/VaultFormat.h"

#include <cstring>
#include <string>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    Field Make(const char* key, FieldRole role = FieldRole::Custom)
    {
        Field f;
        f.Key = key;
        f.Value = "wert";
        f.Role = role;
        return f;
    }

    Document WithFields(std::vector<Field> fields)
    {
        Entry e;
        e.Id = 1;
        e.Title = "Entry";
        e.Fields = std::move(fields);
        Document d;
        d.Entries.push_back(std::move(e));
        return d;
    }

    Document RoundTrip(const Document& doc)
    {
        Body w([&](Writer& out) {
            Encode(doc, out);
        });
        Document back;
        CHECK(Decode(w.data(), w.size(), back));
        return back;
    }
}

TEST_CASE(Role_SurvivesTheFile)
{
    const Document back = RoundTrip(WithFields({
        Make("Username", FieldRole::Username),
        Make("Password", FieldRole::Password),
        Make("Key", FieldRole::Custom),
    }));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    const std::vector<Field>& f = back.Entries[0].Fields;
    CHECK_EQ(f.size(), std::size_t{ 3 });
    CHECK(f[0].Role == FieldRole::Username);
    CHECK(f[1].Role == FieldRole::Password);
    CHECK_MSG(f[2].Role == FieldRole::Custom, "a custom field got a role");
    CHECK(f[0].Key == "Username");
}

TEST_CASE(Role_CustomCostsNoRecord)
{
    Body with([&](Writer& out) {
        Encode(WithFields({ Make("Username", FieldRole::Username) }), out);
    });
    Body without([&](Writer& out) {
        Encode(WithFields({ Make("Username") }), out);
    });
    CHECK_MSG(without.size() < with.size(),
              "Custom wrote a record anyway");
}

TEST_CASE(Role_AnUnknownRoleFallsBackToTheName)
{
    Body inner([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::Entry));
        out.WriteU64(static_cast<std::uint16_t>(Tag::EntryId), 1);
        out.WriteString(static_cast<std::uint16_t>(Tag::EntryTitle), "Zukunft");
        out.BeginRecord(static_cast<std::uint16_t>(Tag::EntryField));
        out.WriteString(static_cast<std::uint16_t>(Tag::FieldKey), "Fingerabdruck");
        out.WriteString(static_cast<std::uint16_t>(Tag::FieldValue), "x");
        out.WriteU32(static_cast<std::uint16_t>(Tag::FieldRole), 9999);
        out.EndRecord();
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(inner.data(), inner.size(), back));
    CHECK_EQ(back.Entries.size(), std::size_t{ 1 });
    if (back.Entries.empty())
        return;
    CHECK(back.Entries[0].Fields[0].Role == FieldRole::Custom);
    CHECK(back.Entries[0].Fields[0].Key == "Fingerabdruck");
}

TEST_CASE(Role_OldVaultsGetTheirRolesDerived)
{
    Document old = WithFields({
        Make("Benutzername"), Make("Password"), Make("Website"),
        Make("Notizen"), Make("E-Mail"), Make("CustomerNo"),
    });
    const Document back = RoundTrip(old);
    const std::vector<Field>& f = back.Entries[0].Fields;

    CHECK(f[0].Role == FieldRole::Username);
    CHECK(f[1].Role == FieldRole::Password);
    CHECK(f[2].Role == FieldRole::Website);
    CHECK(f[3].Role == FieldRole::Notes);
    CHECK(f[4].Role == FieldRole::Email);
    CHECK_MSG(f[5].Role == FieldRole::Custom,
              "a self-named field was turned into a role");
}

TEST_CASE(Role_DerivationStopsAsSoonAsOneRoleIsThere)
{
    Document mixed = WithFields({
        Make("Username", FieldRole::Username),
        Make("Password"),
    });
    const Document back = RoundTrip(mixed);
    CHECK(back.Entries[0].Fields[0].Role == FieldRole::Username);
    CHECK_MSG(back.Entries[0].Fields[1].Role == FieldRole::Custom,
              "the inference ran although the entry already had roles");
}

TEST_CASE(Role_LegacyNamesAreRecognisedInBothLanguages)
{
    CHECK(RoleFromLegacyName("Benutzername") == FieldRole::Username);
    CHECK(RoleFromLegacyName("username") == FieldRole::Username);
    CHECK(RoleFromLegacyName("PASSWORT") == FieldRole::Password);
    CHECK(RoleFromLegacyName("Password") == FieldRole::Password);
    CHECK(RoleFromLegacyName("URL") == FieldRole::Website);
    CHECK(RoleFromLegacyName("E-Mail") == FieldRole::Email);

    for (const char* own : { "Key", "Secret", "PIN", "CustomerNo", "",
                             "Benutzernamen", "Pass", "Web" })
    {
        CHECK_MSG(RoleFromLegacyName(own) == FieldRole::Custom, own);
    }
}

TEST_CASE(Role_EveryRoleHasANameAndAText)
{
    for (std::uint32_t i = 1; i <= kFieldRoleMax; ++i)
    {
        const FieldRole role = static_cast<FieldRole>(i);
        CHECK(std::strlen(CanonicalFieldName(role)) > 0);
        CHECK(FieldRoleText(role) != Str::Count);
    }
    CHECK(std::strlen(CanonicalFieldName(FieldRole::Custom)) == 0);
}

TEST_CASE(Template_AlwaysOffersSomething)
{
    for (const char* platform : { "", "steam", "gibtsnicht", "GOOGLE" })
    {
        const FieldTemplate plan = TemplateFor(platform);
        CHECK_MSG(plan.Count > 0, platform);
        CHECK(plan.Fields != nullptr);
        for (std::size_t i = 0; i < plan.Count; ++i)
            CHECK_MSG(plan.Fields[i].Role != FieldRole::Custom, platform);
    }
}

TEST_CASE(Template_KnowsWhenAnEmailIsTheLogin)
{
    const FieldTemplate google = TemplateFor("google");
    CHECK(google.Fields[0].Role == FieldRole::Email);

    const FieldTemplate steam = TemplateFor("steam");
    CHECK(steam.Fields[0].Role == FieldRole::Username);

    CHECK(TemplateFor("Google").Fields[0].Role == FieldRole::Email);
}

TEST_CASE(Template_MarksThePasswordAsSecret)
{
    const FieldTemplate plan = TemplateFor("steam");
    bool sawPassword = false;
    for (std::size_t i = 0; i < plan.Count; ++i)
    {
        if (plan.Fields[i].Role != FieldRole::Password)
            continue;
        sawPassword = true;
        CHECK_MSG(plan.Fields[i].Secret, "the template's password is not secret");
    }
    CHECK(sawPassword);
}
