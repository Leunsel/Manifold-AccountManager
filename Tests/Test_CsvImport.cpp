#include "TestRunner.h"

#include "HeapWatch.h"

#include "Core/CsvExport.h"
#include "Core/CsvImport.h"
#include "Core/Model.h"
#include "Core/Mutate.h"

#include <cstring>
#include <string>
#include <string_view>

using namespace Bunker;
using namespace Bunker::Vault;
using Test::HeapCopiesDuring;

namespace
{
    constexpr std::uint64_t kNow = 1'700'000'000;

    std::string ValueOfField(const CsvField& f)
    {
        if (f.DoubledQuotes == 0)
            return std::string(f.Raw);
        std::string v(f.Raw.size(), '\0');
        v.resize(UnescapeCsvField(f.Raw, v.data()));
        return v;
    }

    ImportReport Parse(const std::string& text)
    {
        ImportReport report;
        const Status st = ParseCsv(text.c_str(), text.size(), report);
        CHECK_MSG(st.IsOk(), std::string("ParseCsv failed: ") +
                             (st.IsOk() ? "" : (st.GetError().Detail
                                                    ? st.GetError().Detail : "?")));
        return report;
    }

    const Field* FieldWithRole(const Entry& e, FieldRole role)
    {
        for (const Field& f : e.Fields)
            if (f.Role == role)
                return &f;
        return nullptr;
    }

    std::string ValueOf(const Entry& e, FieldRole role)
    {
        const Field* f = FieldWithRole(e, role);
        return f ? f->Value.ToStdString() : std::string("<missing>");
    }

    const ImportRow* FirstReady(const ImportReport& r)
    {
        for (const ImportRow& row : r.Rows)
            if (row.Verdict == RowVerdict::Ready)
                return &row;
        return nullptr;
    }
}

TEST_CASE(CsvIn_UnquotesTheWayTheRfcSays)
{
    struct Case { const char* Text; const char* Want; bool EndsRecord; std::size_t Doubled; };
    const Case cases[] = {
        { "plain,",                   "plain",           false, 0 },
        { "plain",                    "plain",           true,  0 },
        { "\"with,comma\",",          "with,comma",      false, 0 },
        { "\"with\"\"quote\",",       "with\"quote",     false, 1 },
        { "\"\"\"\"\"\",",            "\"\"",            false, 2 },
        { "\"\"\"edge\"\"\"\r\n",     "\"edge\"",        true,  2 },
        { "\"line\nbreak\",",         "line\nbreak",     false, 0 },
        { "\"bare\rcarriage\",",      "bare\rcarriage",  false, 0 },
        { "\"\",",                    "",                false, 0 },
        { ",",                        "",                false, 0 },
        { "last\r\n",                 "last",            true,  0 },
        { "last\n",                   "last",            true,  0 },
        { "last\r",                   "last",            true,  0 },
        { "\"closed\"",               "closed",          true,  0 },
    };

    for (const Case& c : cases)
    {
        const std::size_t length = std::strlen(c.Text);
        std::size_t at = 0;
        CsvField got;
        bool end = false;
        const bool ok = ReadCsvField(c.Text, length, at, got, end);
        CHECK_MSG(ok, std::string("not read: ") + c.Text);
        CHECK_MSG(!got.Malformed, std::string(c.Text) + " counted as broken");
        CHECK_MSG(ValueOfField(got) == c.Want,
                  std::string(c.Text) + " gave '" + ValueOfField(got) + "'");
        CHECK_MSG(got.DoubledQuotes == c.Doubled,
                  std::string(c.Text) + ": " + std::to_string(got.DoubledQuotes) +
                      " pairs counted instead of " + std::to_string(c.Doubled));
        CHECK_MSG(end == c.EndsRecord,
                  std::string(c.Text) + ": line end detected wrongly");
        CHECK_MSG(at == length, std::string(c.Text) + ": not read to the end");
        CHECK(got.Raw.data() >= c.Text && got.Raw.data() + got.Raw.size() <= c.Text + length);
    }
}

TEST_CASE(CsvIn_TextAfterAClosingQuoteMakesTheFieldUnreadable)
{
    struct Case { const char* Text; std::size_t After; bool EndsRecord; };
    const Case cases[] = {
        { "\"JBSW\"Y3DP,next",      11, false },
        { "\"JBSW\"Y3DP\r\nnext",   12, true  },
        { "\"JBSW\"Y3DP\nnext",     11, true  },
        { "\"JBSW\"Y3DP",           10, true  },
        { "\"a\"b\"c\",next",        8, false },
        { "\"a\"\"b\"c\r\nnext",     9, true  },
        { "\"a\" ,next",             5, false },
    };
    for (const Case& c : cases)
    {
        std::size_t at = 0;
        CsvField got;
        bool end = false;
        const bool ok = ReadCsvField(c.Text, std::strlen(c.Text), at, got, end);
        CHECK_MSG(ok, std::string("rejected instead of read: ") + c.Text);
        CHECK_MSG(got.Malformed, std::string(c.Text) + " counted as readable");
        CHECK_MSG(got.Raw.empty() && got.DoubledQuotes == 0,
                  std::string(c.Text) + " still carries a value: '" +
                      std::string(got.Raw) + "'");
        CHECK_MSG(at == c.After, std::string(c.Text) + ": ends at " +
                                     std::to_string(at) + " instead of " +
                                     std::to_string(c.After));
        CHECK_MSG(end == c.EndsRecord,
                  std::string(c.Text) + ": line end detected wrongly");
    }
}

TEST_CASE(CsvIn_AnUnterminatedQuoteIsRefused)
{
    const char* text = "\"never closed";
    std::size_t at = 0;
    CsvField got;
    bool end = false;
    CHECK(!ReadCsvField(text, std::strlen(text), at, got, end));

    ImportReport report;
    const std::string file = "title,password\r\n\"open,secret\r\n";
    CHECK(!ParseCsv(file.c_str(), file.size(), report).IsOk());
}

TEST_CASE(CsvIn_TextAfterAClosingQuoteRejectsTheRowAndNothingElse)
{
    const char* broken[] = {
        "Acme,secret,\"JBSW\"Y3DPEHPK3PXP",
        "\"Ac\"me,secret,",
        "Acme,\"geh\"eim,x,y",
        "Acme,secret,\"JBSW\"Y3,DP\"EHPK3PXP\"",
    };
    for (const char* line : broken)
    {
        const ImportReport r = Parse(std::string("title,password,totp_secret\r\n") + line +
                                     "\r\nZwei,pw2,\r\n");
        CHECK_MSG(r.Rows.size() == 2, std::to_string(r.Rows.size()) +
                                          " rows instead of two from: " + line);
        for (const ImportRow& row : r.Rows)
            CHECK_MSG(row.Draft.Title.find("Y3") == std::string::npos &&
                          row.Draft.Title.find("DP") == std::string::npos,
                      "a piece of the cell became a title: " + row.Draft.Title);
        if (r.Rows.size() != 2)
            continue;

        const ImportRow& bad = r.Rows[0];
        CHECK_MSG(bad.Verdict == RowVerdict::Invalid,
                  std::string("not rejected: ") + line + " was " +
                      RowVerdictName(bad.Verdict));
        CHECK_EQ(bad.Line, std::size_t{ 2 });
        CHECK_MSG(bad.Detail == "text after a closing quote",
                  "wrong reason: " + bad.Detail);
        CHECK_MSG(bad.Draft.Title.empty() && bad.Draft.Fields.empty() && !bad.Draft.HasTotp,
                  std::string("the broken row was read anyway: ") + line);

        const ImportRow& next = r.Rows[1];
        CHECK(next.Verdict == RowVerdict::Ready);
        CHECK(next.Draft.Title == "Zwei");
        CHECK_EQ(next.Line, std::size_t{ 3 });
        CHECK_EQ(r.Ready, std::size_t{ 1 });
        CHECK_EQ(r.Problems, std::size_t{ 1 });
    }
}

TEST_CASE(CsvIn_TextAfterAClosingQuoteInTheHeaderRefusesTheFile)
{
    ImportReport report;
    const std::string file = "\"title\"x,password\r\nAccount,secret\r\n";
    const Status st = ParseCsv(file.c_str(), file.size(), report);
    CHECK(!st.IsOk());
    if (!st.IsOk())
        CHECK_MSG(std::string(st.GetError().Detail ? st.GetError().Detail : "") ==
                      "text after a closing quote in the header",
                  std::string("wrong reason: ") +
                      (st.GetError().Detail ? st.GetError().Detail : "?"));
}

TEST_CASE(CsvIn_WithoutAKnownColumnItRefuses)
{
    ImportReport report;
    const std::string file = "alpha,beta,gamma\r\n1,2,3\r\n";
    CHECK(!ParseCsv(file.c_str(), file.size(), report).IsOk());
}

TEST_CASE(CsvIn_ForeignHeadersAreRecognised)
{
    const ImportReport keepass = Parse(
        "\"Account\",\"Login Name\",\"Password\",\"Web Site\",\"Comments\"\r\n"
        "GitHub,alex.doe,secret,https://github.com,a note\r\n");
    const ImportRow* row = FirstReady(keepass);
    CHECK(row != nullptr);
    if (!row)
        return;
    CHECK(row->Draft.Title == "GitHub");
    CHECK(ValueOf(row->Draft, FieldRole::Username) == "alex.doe");
    CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
    CHECK(ValueOf(row->Draft, FieldRole::Website) == "https://github.com");
    CHECK(ValueOf(row->Draft, FieldRole::Notes) == "a note");

    const ImportReport bitwarden = Parse(
        "folder,name,login_uri,login_username,login_password\r\n"
        "Arbeit,GitLab,https://gitlab.com,alex.doe,secret\r\n");
    const ImportRow* b = FirstReady(bitwarden);
    CHECK(b != nullptr);
    if (!b)
        return;
    CHECK(b->Draft.Title == "GitLab");
    CHECK(b->Draft.Folder == "Arbeit");
    CHECK(ValueOf(b->Draft, FieldRole::Username) == "alex.doe");
}

TEST_CASE(CsvIn_GermanHeadersWork)
{
    const ImportReport r = Parse(
        "Titel,Benutzername,Passwort,Adresse,Ordner\r\n"
        "Steam,alex.doe,secret,https://steampowered.com,Spiele\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    CHECK(row->Draft.Title == "Steam");
    CHECK(row->Draft.Folder == "Spiele");
    CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
}

TEST_CASE(CsvIn_AnUnknownColumnBecomesAFieldAndIsReported)
{
    const ImportReport r = Parse(
        "title,password,CustomerNo\r\n"
        "Bank,secret,4711\r\n");
    CHECK_EQ(r.UnmappedColumns.size(), std::size_t{ 1 });
    if (!r.UnmappedColumns.empty())
        CHECK(r.UnmappedColumns[0] == "CustomerNo");

    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    const Field* extra = nullptr;
    for (const Field& f : row->Draft.Fields)
        if (f.Key == "CustomerNo")
            extra = &f;
    CHECK_MSG(extra != nullptr, "the unknown column was lost");
    if (extra)
        CHECK(extra->Value == "4711");
}

TEST_CASE(CsvIn_ABlankLineIsSkippedNotRejected)
{
    const ImportReport r = Parse(
        "title,password\r\n"
        "Eins,a\r\n"
        "\r\n"
        ",\r\n"
        "Zwei,b\r\n");
    CHECK_EQ(r.Ready, std::size_t{ 2 });
    CHECK_EQ(r.Skipped, std::size_t{ 2 });
    CHECK_EQ(r.Problems, std::size_t{ 0 });
}

TEST_CASE(CsvIn_ABadLineDoesNotStopTheRest)
{
    std::string longTitle(kMaxTitleBytes + 10, 'x');
    const ImportReport r = Parse(
        "title,password\r\n"
        "Gut,a\r\n" +
        longTitle + ",b\r\n"
        "Auch gut,c\r\n");

    CHECK_EQ(r.Ready, std::size_t{ 2 });
    CHECK_EQ(r.Problems, std::size_t{ 1 });

    bool sawProblem = false;
    for (const ImportRow& row : r.Rows)
        if (row.Verdict == RowVerdict::Invalid)
        {
            sawProblem = true;
            CHECK_MSG(!row.Detail.empty(), "the rejection gives no reason");
            CHECK_MSG(row.Draft.Title.empty(),
                      "a rejected row still has a draft");
        }
    CHECK(sawProblem);
}

TEST_CASE(CsvIn_ATitleIsDerivedAndSaidSo)
{
    const ImportReport r = Parse(
        "url,username,password\r\n"
        "https://example.com,alex.doe,secret\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    CHECK(row->Draft.Title == "https://example.com");
    CHECK_MSG(row->TitleDerived, "the made-up title was not reported as such");
}

TEST_CASE(CsvIn_WithNothingToNameItThereIsNoEntry)
{
    const ImportReport r = Parse(
        "title,password,notes\r\n"
        ",secret,only a note\r\n");
    CHECK_EQ(r.Ready, std::size_t{ 0 });
    CHECK_EQ(r.Problems, std::size_t{ 1 });
    CHECK(r.Rows.size() == 1 && r.Rows[0].Verdict == RowVerdict::NoTitle);
}

TEST_CASE(CsvIn_TheLineNumberPointsAtTheRightLine)
{
    const ImportReport r = Parse(
        "title,notes,password\r\n"
        "One,\"two\nlines\",a\r\n"
        ",,\r\n"
        "Drei,x,c\r\n");

    CHECK_EQ(r.Rows.size(), std::size_t{ 3 });
    if (r.Rows.size() < 3)
        return;
    CHECK_EQ(r.Rows[0].Line, std::size_t{ 2 });
    CHECK_EQ(r.Rows[1].Line, std::size_t{ 4 });
    CHECK_MSG(r.Rows[2].Line == 5,
              "the line number counts records instead of lines");
}

TEST_CASE(CsvIn_ValuesAreNotAltered)
{
    const ImportReport r = Parse(
        "title,password\r\n"
        "Tabelle,\"=SUM(A1:A9)\"\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (row)
        CHECK(ValueOf(row->Draft, FieldRole::Password) == "=SUM(A1:A9)");
}

TEST_CASE(CsvIn_APasswordArrivesAsASecret)
{
    const ImportReport r = Parse("title,password\r\nAccount,secret\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    const Field* pw = FieldWithRole(row->Draft, FieldRole::Password);
    CHECK(pw != nullptr);
    if (pw)
        CHECK_MSG(pw->IsSecret(), "the imported password is not secret");
}

TEST_CASE(CsvIn_IdsAndTimestampsAreIgnored)
{
    const ImportReport r = Parse(
        "id,title,password,created_utc,modified_utc\r\n"
        "9999,Account,secret,111,222\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;

    CHECK_EQ(row->Draft.Id, std::uint64_t{ 0 });
    CHECK_EQ(row->Draft.CreatedUtc, std::uint64_t{ 0 });
    CHECK_EQ(row->Draft.ModifiedUtc, std::uint64_t{ 0 });

    CHECK_MSG(r.UnmappedColumns.empty(),
              "id or a timestamp counted as an unknown column");
    for (const Field& f : row->Draft.Fields)
    {
        CHECK_MSG(f.Key != "id" && f.Key != "created_utc" && f.Key != "modified_utc",
                  "'" + f.Key + "' became a field on the entry");
        CHECK_MSG(f.Value != "9999" && f.Value != "111" && f.Value != "222",
                  "the content of id or a timestamp ended up in '" +
                      f.Key + "'");
    }
}

TEST_CASE(CsvIn_TheBomDoesNotEatTheFirstColumn)
{
    const std::string file =
        std::string("\xEF\xBB\xBF") + "title,password\r\nAccount,secret\r\n";
    const ImportReport r = Parse(file);
    CHECK_EQ(r.Ready, std::size_t{ 1 });
    CHECK_MSG(r.UnmappedColumns.empty(), "the BOM made the first column unknown");
}

TEST_CASE(CsvIn_TheSameFileTwiceDoesNotDouble)
{
    Document doc;
    Entry e;
    e.Title = "GitHub";
    e.PlatformId = "github";
    Field u;
    u.Role = FieldRole::Username;
    u.Key = CanonicalFieldName(FieldRole::Username);
    u.Value = "alex.doe";
    e.Fields.push_back(u);
    CHECK(AddEntry(doc, e, kNow).IsOk());

    ImportReport r = Parse(
        "title,platform,username,password\r\n"
        "GitHub,github,alex.doe,secret\r\n"
        "GitLab,gitlab,alex.doe,secret\r\n");
    CHECK_EQ(r.Ready, std::size_t{ 2 });

    CHECK_EQ(MarkDuplicates(doc, r), std::size_t{ 1 });
    CHECK_EQ(r.Duplicates, std::size_t{ 1 });
    CHECK_MSG(r.Ready == 1, "the number of importable rows was not adjusted");
}

TEST_CASE(CsvIn_ADuplicateIsFoundAcrossCaseAndUmlautSpelling)
{
    Document doc;
    Entry e;
    e.Title = "GitHub";
    e.PlatformId = "github";
    Field u;
    u.Role = FieldRole::Username;
    u.Key = CanonicalFieldName(FieldRole::Username);
    u.Value = "M\xC3\xBCller.Anna";
    e.Fields.push_back(u);
    CHECK(AddEntry(doc, e, kNow).IsOk());

    ImportReport r = Parse(
        "title,platform,username,password\r\n"
        "GITHUB,GitHub,mueller.anna,secret\r\n");
    CHECK_EQ(r.Ready, std::size_t{ 1 });
    CHECK_EQ(MarkDuplicates(doc, r), std::size_t{ 1 });
    CHECK_EQ(r.Duplicates, std::size_t{ 1 });
}

TEST_CASE(CsvIn_ADifferentLoginIsNotADuplicate)
{
    Document doc;
    Entry e;
    e.Title = "GitHub";
    e.PlatformId = "github";
    Field u;
    u.Role = FieldRole::Username;
    u.Key = CanonicalFieldName(FieldRole::Username);
    u.Value = "alex.doe";
    e.Fields.push_back(u);
    CHECK(AddEntry(doc, e, kNow).IsOk());

    ImportReport r = Parse(
        "title,platform,username,password\r\n"
        "GitHub,github,someone-else,secret\r\n");
    CHECK_EQ(MarkDuplicates(doc, r), std::size_t{ 0 });
    CHECK_EQ(r.Ready, std::size_t{ 1 });
}

TEST_CASE(CsvIn_TheOwnExportComesBackWhole)
{
    Document doc;

    Entry a;
    a.Title = "Battle.net";
    a.PlatformId = "battle-net";
    a.Folder = "Gaming";
    for (const auto& pair : { std::pair<FieldRole, const char*>{ FieldRole::Username, "AlexDoe#1234" },
                              std::pair<FieldRole, const char*>{ FieldRole::Password, "a,secret\"password" },
                              std::pair<FieldRole, const char*>{ FieldRole::Website, "https://account.battle.net" },
                              std::pair<FieldRole, const char*>{ FieldRole::Notes, "two\nlines" } })
    {
        Field f;
        f.Role = pair.first;
        f.Key = CanonicalFieldName(pair.first);
        f.Value = pair.second;
        if (pair.first == FieldRole::Password)
            f.Flags |= kFieldFlagSecret;
        a.Fields.push_back(std::move(f));
    }
    Field custom;
    custom.Role = FieldRole::Custom;
    custom.Key = "CustomerNo";
    custom.Value = "4711";
    a.Fields.push_back(std::move(custom));

    a.HasTotp = true;
    a.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    a.Totp.Config.Issuer = "Battle.net";
    RecoveryCode used;
    used.Value = "AAAA-BBBB";
    used.UsedUtc = kNow;
    a.RecoveryCodes.push_back(used);
    RecoveryCode fresh;
    fresh.Value = "CCCC-DDDD";
    a.RecoveryCodes.push_back(fresh);

    ListItem key;
    key.Kind = kListKindProductKey;
    key.Label = "Half-Life 2, \"Orange Box\"";
    key.Secret = " AAAAA-BBBBB \"x\" ";
    a.ListItems.push_back(std::move(key));
    ListItem future;
    future.Kind = 77;
    future.Label = "Zukunft";
    future.Secret = "ZUKUNFT-1";
    a.ListItems.push_back(std::move(future));
    ListItem bare;
    bare.Kind = kListKindOrderNumber;
    bare.Secret = "ORD-4711";
    a.ListItems.push_back(std::move(bare));

    CHECK(AddEntry(doc, a, kNow).IsOk());

    CsvOptions options;
    options.IncludeSecondFactor = true;
    SecureBuffer text;
    std::size_t length = 0;
    CHECK(WriteCsv(doc, options, text, length).IsOk());
    CHECK(text.IsValid());
    if (!text.IsValid())
        return;

    ImportReport back;
    const Status st = ParseCsv(reinterpret_cast<const char*>(text.Data()), length, back);
    CHECK_MSG(st.IsOk(), "our own export was not readable");
    CHECK_EQ(back.Ready, std::size_t{ 1 });
    CHECK_MSG(back.UnmappedColumns.empty(),
              "one of our own columns was not recognised");

    const ImportRow* row = FirstReady(back);
    CHECK(row != nullptr);
    if (!row)
        return;
    const Entry& got = row->Draft;

    CHECK(got.Title == "Battle.net");
    CHECK(got.PlatformId == "battle-net");
    CHECK(got.Folder == "Gaming");
    CHECK_MSG(ValueOf(got, FieldRole::Username) == "AlexDoe#1234",
              "the username did not come back");
    CHECK_MSG(ValueOf(got, FieldRole::Password) == "a,secret\"password",
              "the password came back changed: " +
                  ValueOf(got, FieldRole::Password));
    CHECK_MSG(ValueOf(got, FieldRole::Website) == "https://account.battle.net",
              "the address did not come back");
    CHECK_MSG(ValueOf(got, FieldRole::Notes) == "two\nlines",
              "the line break in the notes was lost");

    const Field* custom2 = nullptr;
    for (const Field& f : got.Fields)
        if (f.Key == "CustomerNo")
            custom2 = &f;
    CHECK_MSG(custom2 != nullptr, "the custom field was lost");
    if (custom2)
        CHECK(custom2->Value == "4711");

    CHECK_MSG(got.HasTotp, "the second factor was lost");
    CHECK(got.Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP");
    CHECK(got.Totp.Config.Issuer == "Battle.net");

    CHECK_EQ(got.RecoveryCodes.size(), std::size_t{ 2 });
    if (got.RecoveryCodes.size() == 2)
    {
        CHECK(got.RecoveryCodes[0].Value == "AAAA-BBBB");
        CHECK_MSG(got.RecoveryCodes[0].IsUsed(),
                  "a used code came back as fresh");
        CHECK(got.RecoveryCodes[1].Value == "CCCC-DDDD");
        CHECK_MSG(!got.RecoveryCodes[1].IsUsed(),
                  "a fresh code came back as used");
    }

    CHECK_EQ(got.ListItems.size(), std::size_t{ 3 });
    if (got.ListItems.size() == 3)
    {
        CHECK_EQ(got.ListItems[0].Kind, kListKindProductKey);
        CHECK(got.ListItems[0].Label == "Half-Life 2, \"Orange Box\"");
        CHECK_MSG(got.ListItems[0].Secret == " AAAAA-BBBBB \"x\" ",
                  "a list value came back changed");
        CHECK_EQ(got.ListItems[1].Kind, std::uint32_t{ 77 });
        CHECK(got.ListItems[1].Secret == "ZUKUNFT-1");
        CHECK_EQ(got.ListItems[2].Kind, kListKindOrderNumber);
        CHECK(got.ListItems[2].Label.empty());
        CHECK(got.ListItems[2].Secret == "ORD-4711");
    }
}

TEST_CASE(CsvIn_ListLinesAreReadForgivingly)
{
    const ImportReport r = Parse(
        "title,list_items\r\n"
        "Account,\"only-one-value\n"
        "steam_key\tGame\tKEY-1\r\n"
        "\n"
        "9\tNeunte Art\tNEUN\n"
        "security_question\tQuestion?\tAnswer with spaces\"\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    const std::vector<ListItem>& items = row->Draft.ListItems;
    CHECK_EQ(items.size(), std::size_t{ 4 });
    if (items.size() != 4)
        return;
    CHECK_EQ(items[0].Kind, kListKindOther);
    CHECK(items[0].Label.empty());
    CHECK(items[0].Secret == "only-one-value");
    CHECK_EQ(items[1].Kind, kListKindOther);
    CHECK(items[1].Label == "Game");
    CHECK(items[1].Secret == "KEY-1");
    CHECK_EQ(items[2].Kind, std::uint32_t{ 9 });
    CHECK_EQ(items[3].Kind, kListKindSecurityQuestion);
    CHECK(items[3].Label == "Question?");
    CHECK(items[3].Secret == "Answer with spaces");
}

TEST_CASE(CsvIn_AnOldColumnLayoutStillReads)
{
    const ImportReport r = Parse(
        "title,password,recovery_codes,created_utc,modified_utc\r\n"
        "Account,secret,,1,2\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (row)
        CHECK(row->Draft.ListItems.empty());
}

TEST_CASE(CsvIn_WithoutTheSecondFactorTheExportStillComesBack)
{
    Document doc;
    Entry a;
    a.Title = "Account";
    Field pw;
    pw.Role = FieldRole::Password;
    pw.Key = CanonicalFieldName(FieldRole::Password);
    pw.Value = "secret";
    pw.Flags |= kFieldFlagSecret;
    a.Fields.push_back(std::move(pw));
    a.HasTotp = true;
    a.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    CHECK(AddEntry(doc, a, kNow).IsOk());

    CsvOptions options;
    options.IncludeSecondFactor = false;
    SecureBuffer text;
    std::size_t length = 0;
    CHECK(WriteCsv(doc, options, text, length).IsOk());

    ImportReport back;
    CHECK(ParseCsv(reinterpret_cast<const char*>(text.Data()), length, back).IsOk());
    CHECK_EQ(back.Ready, std::size_t{ 1 });

    const ImportRow* row = FirstReady(back);
    CHECK(row != nullptr);
    if (row)
        CHECK_MSG(!row->Draft.HasTotp,
                  "a second factor arrived although the export had no secret");
}

TEST_CASE(CsvIn_TheTotpSecretIsStoredCanonically)
{
    const ImportReport r =
        Parse("title,password,totp_secret\r\nAccount,secret,jbsw y3dp ehpk-3pxp\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK_MSG(row != nullptr, "the row was rejected");
    if (!row)
        return;
    CHECK(row->Draft.HasTotp);
    CHECK_MSG(row->Draft.Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP",
              std::string("not stored canonically: ")
                  .append(row->Draft.Totp.Config.SecretBase32.View()));
}

TEST_CASE(CsvIn_ASharpSIsNotRepairedIntoAKey)
{
    const ImportReport r =
        Parse("title,password,totp_secret\r\nAccount,secret,JBSWY3DPEHPK3P\xC3\x9F\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK_MSG(row != nullptr, "the whole row was dropped, password included");
    CHECK_EQ(r.Problems, std::size_t{ 0 });
    if (!row)
        return;
    CHECK_MSG(!row->Draft.HasTotp, "a sharp s turned into a key");
    CHECK(row->Draft.Totp.Config.SecretBase32.Empty());
    CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
    CHECK(row->SecondFactorDropped);
    CHECK_MSG(row->SecondFactorProblem == "character is not in the Base32 alphabet",
              "the hint names a follow-up error: " + row->SecondFactorProblem);
}

TEST_CASE(CsvIn_AnUnusableSecondFactorKeepsTheRest)
{
    const char* cells[] = {
        "steam://JBSWY3DPEHPK3PXP",
        "otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP&counter=1",
        "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=5",
    };
    for (const char* cell : cells)
    {
        const ImportReport r =
            Parse(std::string("title,password,totp_secret\r\nAccount,secret,") + cell + "\r\n");
        const ImportRow* row = FirstReady(r);
        CHECK_MSG(row != nullptr, std::string("the whole row was dropped: ") + cell);
        if (!row)
            continue;
        CHECK(!row->Draft.HasTotp);
        CHECK(row->SecondFactorDropped);
        CHECK(!row->SecondFactorProblem.empty());
        CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
        CHECK_MSG(row->SecondFactorProblem.find("JBSW") == std::string::npos,
                  "the hint repeats the secret");
    }
}

TEST_CASE(CsvIn_AReadableButUnusableSecretKeepsTheRest)
{
    const std::string cells[] = {
        "JBSWY3DP",
        std::string(520, 'A'),
    };
    for (const std::string& cell : cells)
    {
        const ImportReport r = Parse("title,password,totp_secret\r\nAccount,secret," + cell + "\r\n");
        const ImportRow* row = FirstReady(r);
        CHECK_MSG(row != nullptr, "the whole row was dropped at " +
                                      std::to_string(cell.size()) + " characters");
        if (!row)
            continue;
        CHECK(!row->Draft.HasTotp);
        CHECK(row->Draft.Totp.Config.SecretBase32.Empty());
        CHECK(row->SecondFactorDropped);
        CHECK(!row->SecondFactorProblem.empty());
        CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
    }
}

TEST_CASE(CsvIn_ARowRejectedForAnotherReasonIsNotAlsoListedAsNoSecondFactor)
{
    const ImportReport r = Parse("notes,totp_secret\r\nx,JBSW0\r\n");
    CHECK_EQ(r.Rows.size(), std::size_t{ 1 });
    if (r.Rows.size() != 1)
        return;
    CHECK(r.Rows[0].Verdict == RowVerdict::NoTitle);
    CHECK(!r.Rows[0].SecondFactorDropped);
    CHECK(r.Rows[0].SecondFactorProblem.empty());
}

TEST_CASE(CsvIn_AGoodSecondFactorIsNotFlagged)
{
    const ImportReport r =
        Parse("title,password,totp_secret\r\nAccount,secret,JBSWY3DPEHPK3PXP\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    CHECK(row->Draft.HasTotp);
    CHECK(!row->SecondFactorDropped);
    CHECK(row->SecondFactorProblem.empty());
}

TEST_CASE(CsvIn_AnOtpauthUriInTheTotpColumnIsTakenWhole)
{
    const ImportReport r = Parse(
        "title,password,totp_secret\r\n"
        "Account,secret,otpauth://totp/Acme:alex?secret=JBSWY3DPEHPK3PXP&digits=8&issuer=Acme\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK_MSG(row != nullptr, "a row with an otpauth address was rejected");
    if (!row)
        return;
    CHECK(row->Draft.HasTotp);
    CHECK(row->Draft.Totp.Config.SecretBase32 == "JBSWY3DPEHPK3PXP");
    CHECK_EQ(row->Draft.Totp.Config.Digits, std::uint32_t{ 8 });
    CHECK(row->Draft.Totp.Config.Issuer == "Acme");
    CHECK(ValueOf(row->Draft, FieldRole::Password) == "secret");
}

TEST_CASE(CsvIn_TheIssuerColumnWinsOverTheUri)
{
    const ImportReport r = Parse(
        "title,password,totp_secret,totp_issuer\r\n"
        "Account,secret,otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&issuer=Address,Column\r\n");
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (row)
        CHECK(row->Draft.Totp.Config.Issuer == "Column");
}

TEST_CASE(CsvIn_WipeEntrySecretsReachesTheTotpSecret)
{
    Entry e;
    e.HasTotp = true;
    e.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    const unsigned char* slot = e.Totp.Config.SecretBase32.PayloadForTests();
    const std::size_t slotBytes = e.Totp.Config.SecretBase32.SlotBytesForTests();
    CHECK(slot != nullptr);

    WipeEntrySecrets(e);

    CHECK(!e.HasTotp);
    CHECK(e.Totp.Config.SecretBase32.Empty());
    std::size_t left = 0;
    for (std::size_t i = 0; slot && i < slotBytes; ++i)
        if (slot[i] != 0)
            ++left;
    CHECK_MSG(left == 0, std::to_string(left) + " bytes of the secret remained");
}

TEST_CASE(CsvIn_AMissingTitleWinsOverABrokenSecret)
{
    const ImportReport r = Parse("notes,totp_secret\r\nx,JBSW0\r\n");
    CHECK_EQ(r.Rows.size(), std::size_t{ 1 });
    if (r.Rows.size() != 1)
        return;
    CHECK(r.Rows[0].Verdict == RowVerdict::NoTitle);
    CHECK_MSG(r.Rows[0].Detail == "entry without a title",
              "the message names a different column: " + r.Rows[0].Detail);
}

TEST_CASE(CsvIn_ARawNulInATotpUriDropsTheSecondFactor)
{
    std::string file = "title,password,totp_secret\r\nAccount,secret,otpauth://totp/a";
    file.push_back('\0');
    file += "b?secret=JBSWY3DPEHPK3PXP\r\n";
    const ImportReport r = Parse(file);
    const ImportRow* row = FirstReady(r);
    CHECK(row != nullptr);
    if (!row)
        return;
    CHECK_MSG(!row->Draft.HasTotp, "an address with a NUL byte was taken over");
    CHECK(row->Draft.Totp.Config.AccountName.find('\0') == std::string::npos);
    CHECK(row->SecondFactorDropped);
}

namespace
{
    ImportReport ExportAndReadBack(const Document& doc)
    {
        CsvOptions options;
        options.IncludeSecondFactor = true;
        SecureBuffer text;
        std::size_t length = 0;
        ImportReport back;
        CHECK(WriteCsv(doc, options, text, length).IsOk());
        if (!text.IsValid())
            return back;
        CHECK(ParseCsv(reinterpret_cast<const char*>(text.Data()), length, back).IsOk());
        return back;
    }
}

TEST_CASE(CsvIn_ANonDefaultTotpSurvivesTheRoundTrip)
{
    Document doc;
    Entry a;
    a.Title = "Account";
    a.HasTotp = true;
    a.Totp.Config.SecretBase32 = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";
    a.Totp.Config.Digits = 8;
    a.Totp.Config.Period = 60;
    a.Totp.Config.Algorithm = HashAlgorithm::Sha256;
    a.Totp.Config.Issuer = "Acme: Konzern";
    a.Totp.Config.AccountName = "alex:doe@example.com";
    CHECK(AddEntry(doc, a, kNow).IsOk());

    {
        CsvOptions options;
        options.IncludeSecondFactor = true;
        SecureBuffer text;
        std::size_t length = 0;
        CHECK(WriteCsv(doc, options, text, length).IsOk());
        if (text.IsValid())
        {
            const std::string_view csv(reinterpret_cast<const char*>(text.Data()), length);
            CHECK_MSG(csv.find("&issuer=Acme%3A%20Konzern") != std::string_view::npos,
                      "the address does not carry the issuer");
        }
    }

    const ImportReport back = ExportAndReadBack(doc);
    const ImportRow* row = FirstReady(back);
    CHECK_MSG(row != nullptr, "our own address did not come back");
    if (!row)
        return;
    const TotpConfig& got = row->Draft.Totp.Config;
    CHECK(got.SecretBase32 == "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
    CHECK_EQ(got.Digits, std::uint32_t{ 8 });
    CHECK_EQ(got.Period, std::uint32_t{ 60 });
    CHECK(got.Algorithm == HashAlgorithm::Sha256);
    CHECK(got.Issuer == "Acme: Konzern");
    CHECK(got.AccountName == "alex:doe@example.com");

    const auto before = TotpFromConfig(a.Totp.Config, 1'700'000'000);
    const auto after = TotpFromConfig(got, 1'700'000'000);
    CHECK(before.IsOk() && after.IsOk());
    if (before.IsOk() && after.IsOk())
        CHECK_MSG(before.Value() == after.Value(),
                  "after the round trip the codes no longer match");
}

TEST_CASE(CsvIn_ADefaultTotpStaysABareSecret)
{
    Document doc;
    Entry a;
    a.Title = "Account";
    a.HasTotp = true;
    a.Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    a.Totp.Config.AccountName = "alex";
    CHECK(AddEntry(doc, a, kNow).IsOk());

    CsvOptions options;
    options.IncludeSecondFactor = true;
    SecureBuffer text;
    std::size_t length = 0;
    CHECK(WriteCsv(doc, options, text, length).IsOk());
    if (!text.IsValid())
        return;
    const std::string_view csv(reinterpret_cast<const char*>(text.Data()), length);
    CHECK_MSG(csv.find(",JBSWY3DPEHPK3PXP,") != std::string_view::npos,
              "the default configuration is not stored as a bare secret");
    CHECK(csv.find("otpauth://") == std::string_view::npos);
}

TEST_CASE(CsvIn_AQuoteHeavyExportCrossesEveryGrowth)
{
    Document doc;
    {
        Entry first;
        first.Title = "Vorne viele Zeichen";
        Field notes;
        notes.Role = FieldRole::Notes;
        notes.Key = CanonicalFieldName(FieldRole::Notes);
        notes.Value = std::string(3000, '"');
        first.Fields.push_back(std::move(notes));
        CHECK(AddEntry(doc, std::move(first), kNow).IsOk());
    }
    for (int i = 0; i < 50; ++i)
    {
        Entry e;
        e.Title = "Account " + std::to_string(i);
        Field pw;
        pw.Role = FieldRole::Password;
        pw.Key = CanonicalFieldName(FieldRole::Password);
        pw.Value = "password,with\"chars-" + std::to_string(i);
        pw.Flags |= kFieldFlagSecret;
        e.Fields.push_back(std::move(pw));
        CHECK(AddEntry(doc, std::move(e), kNow).IsOk());
    }
    Entry heavy;
    heavy.Title = "Viele Zeichen";
    Field notes;
    notes.Role = FieldRole::Notes;
    notes.Key = CanonicalFieldName(FieldRole::Notes);
    notes.Value = std::string(5000, '"');
    heavy.Fields.push_back(std::move(notes));
    CHECK(AddEntry(doc, std::move(heavy), kNow).IsOk());

    const ImportReport back = ExportAndReadBack(doc);
    CHECK_EQ(back.Ready, std::size_t{ 52 });

    std::size_t intact = 0;
    bool heavyIntact = false;
    bool firstIntact = false;
    for (const ImportRow& row : back.Rows)
    {
        if (row.Verdict != RowVerdict::Ready)
            continue;
        if (row.Draft.Title == "Vorne viele Zeichen")
            firstIntact = ValueOf(row.Draft, FieldRole::Notes) == std::string(3000, '"');
        else if (row.Draft.Title == "Viele Zeichen")
            heavyIntact = ValueOf(row.Draft, FieldRole::Notes) == std::string(5000, '"');
        else if (ValueOf(row.Draft, FieldRole::Password)
                     .starts_with("password,with\"chars-"))
            ++intact;
    }
    CHECK_MSG(firstIntact, "the first note made of quotes came back changed");
    CHECK_MSG(heavyIntact, "the note made of quotes came back changed");
    CHECK_EQ(intact, std::size_t{ 50 });
}

TEST_CASE(CsvIn_ReadingAFileCopiesNoCellIntoTheHeap)
{
    constexpr std::string_view kUser = "canary.user-0123456789";
    constexpr std::string_view kQuoted = "Canary\"Quote\"Password-0123";
    constexpr std::string_view kNotes = "Canary-note-line-one\nline-two-0123";
    constexpr std::string_view kExtra = "Canary-Extra-0123456789";
    constexpr std::string_view kCode = "CANARY-CODE-0123456789";
    constexpr std::string_view kListItem = "CANARY-KEY-0123456789";
    constexpr std::string_view kCustom = "Canary-CustomerNo-0123";
    constexpr std::string_view kTotp = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";

    constexpr std::string_view kTitle = "Canary-Title-0123456789";
    constexpr std::string_view kPlatform = "canary-plattform-0123";

    const std::string file =
        "title,platform,username,password,notes,extra_fields,recovery_codes,list_items,"
        "CustomerNo,totp_secret\r\n"
        "Canary-Title-0123456789,canary-plattform-0123,"
        "canary.user-0123456789,\"Canary\"\"Quote\"\"Password-0123\","
        "\"Canary-note-line-one\nline-two-0123\",PIN=Canary-Extra-0123456789,"
        "\"CANARY-CODE-0123456789 (used)\nCANARY-CODE-9876543210\","
        "product_key\tGame\tCANARY-KEY-0123456789,Canary-CustomerNo-0123,"
        "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ\r\n";

    bool arrived = false;
    auto readAndForget = [&] {
        ImportReport r;
        const Status st = ParseCsv(file.c_str(), file.size(), r);
        if (st.IsOk() && r.Ready == 1 && r.Rows.size() == 1)
        {
            const Entry& e = r.Rows[0].Draft;
            std::size_t found = 0;
            for (const Field& f : e.Fields)
            {
                if ((f.Role == FieldRole::Username && f.Value == kUser) ||
                    (f.Role == FieldRole::Password && f.Value == kQuoted) ||
                    (f.Role == FieldRole::Notes && f.Value == kNotes) ||
                    f.Value == kExtra || f.Value == kCustom)
                    ++found;
            }
            arrived = found == 5 && e.Title == kTitle && e.PlatformId == kPlatform &&
                      e.RecoveryCodes.size() == 2 && e.RecoveryCodes[0].Value == kCode &&
                      e.RecoveryCodes[0].IsUsed() && e.ListItems.size() == 1 &&
                      e.ListItems[0].Secret == kListItem && e.HasTotp &&
                      e.Totp.Config.SecretBase32 == kTotp;
        }
        for (ImportRow& row : r.Rows)
            WipeEntrySecrets(row.Draft);
    };

    const std::size_t values = HeapCopiesDuring(
        { kUser, kQuoted, kNotes, kExtra, kCode, kListItem, kCustom, kTotp }, readAndForget);
    CHECK_MSG(arrived, "the row did not arrive complete -- the case measures nothing");
    CHECK_MSG(values == 0, std::to_string(values) + " heap blocks held a value");

    arrived = false;
    const std::size_t names = HeapCopiesDuring({ kTitle, kPlatform }, readAndForget);
    CHECK(arrived);
    CHECK_MSG(names == 0, std::to_string(names) + " heap blocks held a title or platform");
}

TEST_CASE(CsvIn_ARejectedRowLeavesNoCopyInTheHeap)
{
    std::string longTitle = "Canary-Title-";
    while (longTitle.size() <= kMaxTitleBytes)
        longTitle += "0123456789";
    constexpr std::string_view kFolder = "Canary-Folder-0123456789";
    constexpr std::string_view kLabel = "Canary-Label-0123";
    const std::string file = "title,folder,password,list_items\r\n" + longTitle +
                             ",Arbeit,Pw-with-twenty-chars,\r\n"
                             ",Canary-Folder-0123456789,Pw-with-twenty-chars,"
                             "product_key\tCanary-Label-0123\tKEY-1\r\n";

    bool rejected = false;
    const std::size_t hits = HeapCopiesDuring({ longTitle, kFolder, kLabel }, [&] {
        ImportReport r;
        const Status st = ParseCsv(file.c_str(), file.size(), r);
        rejected = st.IsOk() && r.Rows.size() == 2 && r.Problems == 2 &&
                   r.Rows[0].Verdict == RowVerdict::Invalid &&
                   r.Rows[0].Detail == "title too long" &&
                   r.Rows[1].Verdict == RowVerdict::NoTitle;
    });
    CHECK_MSG(rejected, "the rows were not rejected as intended -- the "
                        "case measures nothing");
    CHECK_MSG(hits == 0, std::to_string(hits) +
                             " heap blocks held the title, folder or label "
                             "of a rejected row");
}
