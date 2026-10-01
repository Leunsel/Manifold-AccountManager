#include "TestRunner.h"

#include "Core/CsvExport.h"
#include "Core/Model.h"
#include "Generated/CsvKat.h"

#include <string>
#include <vector>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    Field Make(const char* key, const char* value, bool secret = false,
               FieldRole role = FieldRole::Custom)
    {
        Field f;
        f.Key = key;
        f.Value = value;
        f.Role = role;
        if (secret)
            f.Flags |= kFieldFlagSecret;
        return f;
    }

    Document OneEntry()
    {
        Entry e;
        e.Id = 7;
        e.Title = "Beispielbank";
        e.PlatformId = "bank";
        e.Folder = "Banken";
        e.CreatedUtc = 1000;
        e.ModifiedUtc = 2000;
        e.Fields.push_back(Make("Username", "kunde@example.de", false,
                                FieldRole::Username));
        e.Fields.push_back(Make("Password", "secret,with\"commas", true,
                                FieldRole::Password));
        e.Fields.push_back(Make("Website", "https://example.de", false,
                                FieldRole::Website));

        Document d;
        d.Entries.push_back(e);
        return d;
    }

    std::string Export(const Document& doc, const CsvOptions& options)
    {
        SecureBuffer buffer;
        std::size_t length = 0;
        const Status st = WriteCsv(doc, options, buffer, length);
        CHECK(st.IsOk());
        if (!st.IsOk() || !buffer.IsValid())
            return {};
        return std::string(reinterpret_cast<const char*>(buffer.Data()), length);
    }

    CsvOptions Plain()
    {
        CsvOptions o;
        o.WriteBom = false;
        return o;
    }

    std::vector<std::string> ParseRow(const std::string& text, std::size_t& at)
    {
        std::vector<std::string> cells;
        std::string cell;
        bool quoted = false;
        while (at < text.size())
        {
            const char c = text[at];
            if (quoted)
            {
                if (c == '"')
                {
                    if (at + 1 < text.size() && text[at + 1] == '"')
                    {
                        cell.push_back('"');
                        at += 2;
                        continue;
                    }
                    quoted = false;
                    ++at;
                    continue;
                }
                cell.push_back(c);
                ++at;
                continue;
            }
            if (c == '"' && cell.empty())
            {
                quoted = true;
                ++at;
                continue;
            }
            if (c == ',')
            {
                cells.push_back(cell);
                cell.clear();
                ++at;
                continue;
            }
            if (c == '\r' && at + 1 < text.size() && text[at + 1] == '\n')
            {
                at += 2;
                cells.push_back(cell);
                return cells;
            }
            cell.push_back(c);
            ++at;
        }
        if (!cell.empty())
            cells.push_back(cell);
        return cells;
    }

    std::size_t Col(const std::vector<std::string>& header, const char* name)
    {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name)
                return i;
        CHECK_MSG(false, std::string("no column named ") + name);
        return 0;
    }

    struct Sheet
    {
        std::vector<std::string> Header;
        std::vector<std::string> Row;

        const std::string& operator[](const char* name) const
        {
            static const std::string none;
            const std::size_t i = Col(Header, name);
            return i < Row.size() ? Row[i] : none;
        }
    };

    Sheet FirstRow(const std::string& text)
    {
        std::size_t at = 0;
        Sheet sheet;
        sheet.Header = ParseRow(text, at);
        sheet.Row = ParseRow(text, at);
        return sheet;
    }
}

TEST_CASE(Csv_QuotingMatchesAnIndependentImplementation)
{
    for (const Kat::CsvCase& c : Kat::kCsvQuoting)
        CHECK_MSG(QuoteCsvField(c.Raw) == c.Quoted, c.Raw);
}

TEST_CASE(Csv_QuotesEdgeSpacesEvenThoughRfcDoesNot)
{
    CHECK(QuoteCsvField(" fuehrend") == "\" fuehrend\"");
    CHECK(QuoteCsvField("folgend ") == "\"folgend \"");
    CHECK(QuoteCsvField(" beides ") == "\" beides \"");
    CHECK_MSG(QuoteCsvField("innen drin") == "innen drin",
              "a space in the middle needs no quotes");
}

TEST_CASE(Csv_FormulaLikeValuesAreLeftExactlyAsTheyAre)
{
    CHECK(QuoteCsvField("=SUMME(A1)") == "=SUMME(A1)");
    CHECK(QuoteCsvField("+491701234567") == "+491701234567");
    CHECK(QuoteCsvField("-Minus") == "-Minus");
    CHECK(QuoteCsvField("@Erwaehnung") == "@Erwaehnung");
}

TEST_CASE(Csv_EveryRowHasTheSameNumberOfCells)
{
    Document doc = OneEntry();
    Entry awkward;
    awkward.Id = 8;
    awkward.Title = "Mit\r\nUmbruch";
    awkward.Fields.push_back(Make("Notes", "one, two\nthree \"four\"", false,
                                  FieldRole::Notes));
    doc.Entries.push_back(awkward);

    const std::string text = Export(doc, Plain());
    std::size_t at = 0;
    const std::vector<std::string> header = ParseRow(text, at);
    CHECK_EQ(header.size(), std::size_t{ 15 });
    CHECK(header[0] == "id");
    CHECK(header[Col(header, "password")] == "password");

    std::size_t rows = 0;
    while (at < text.size())
    {
        const std::vector<std::string> row = ParseRow(text, at);
        if (row.empty())
            break;
        CHECK_MSG(row.size() == header.size(), "a row has a different number of columns");
        ++rows;
    }
    CHECK_EQ(rows, std::size_t{ 2 });
}

TEST_CASE(Csv_ValuesComeBackUnchanged)
{
    const Sheet sheet = FirstRow(Export(OneEntry(), Plain()));
    CHECK_EQ(sheet.Row.size(), std::size_t{ 15 });
    if (sheet.Row.size() != 14)
        return;

    CHECK(sheet["id"] == "7");
    CHECK(sheet["title"] == "Beispielbank");
    CHECK(sheet["platform"] == "bank");
    CHECK(sheet["folder"] == "Banken");
    CHECK(sheet["username"] == "kunde@example.de");
    CHECK_MSG(sheet["password"] == "secret,with\"commas",
              "the password came back changed");
    CHECK(sheet["url"] == "https://example.de");
    CHECK(sheet["created_utc"] == "1000");
    CHECK(sheet["modified_utc"] == "2000");
}

TEST_CASE(Csv_ListItemsTravelAsKindTabDescriptionTabValue)
{
    Document doc;
    Entry e;
    e.Title = "Steam";
    ListItem key;
    key.Kind = kListKindProductKey;
    key.Label = "Portal";
    key.Secret = "AAAAA-BBBBB";
    e.ListItems.push_back(std::move(key));
    ListItem future;
    future.Kind = 77;
    future.Label = "Zukunft";
    future.Secret = "ZZZ";
    e.ListItems.push_back(std::move(future));
    doc.Entries.push_back(std::move(e));

    const std::string text = Export(doc, Plain());
    std::size_t at = 0;
    const std::vector<std::string> header = ParseRow(text, at);
    const std::vector<std::string> row = ParseRow(text, at);
    CHECK(header.back() == "list_items");
    CHECK_EQ(row.size(), header.size());
    if (row.size() == header.size())
        CHECK(row.back() == "product_key\tPortal\tAAAAA-BBBBB\n77\tZukunft\tZZZ");
}

TEST_CASE(Csv_TheSecondFactorStaysOutUnlessAsked)
{
    Document doc = OneEntry();
    doc.Entries[0].HasTotp = true;
    doc.Entries[0].Totp.Config.SecretBase32 = "JBSWY3DPEHPK3PXP";
    doc.Entries[0].Totp.Config.Issuer = "Beispielbank";
    doc.Entries[0].RecoveryCodes.push_back(RecoveryCode{ "aaaa-1111", 0, {} });
    doc.Entries[0].RecoveryCodes.push_back(RecoveryCode{ "bbbb-2222", 1700000000ull, {} });

    const std::string without = Export(doc, Plain());
    CHECK_MSG(without.find("JBSWY3DPEHPK3PXP") == std::string::npos,
              "the TOTP secret was in the file without being asked for");
    CHECK_MSG(without.find("aaaa-1111") == std::string::npos,
              "a recovery code was in the file without being asked for");
    CHECK(without.find("secret") != std::string::npos);

    CsvOptions asked = Plain();
    asked.IncludeSecondFactor = true;
    const std::string with = Export(doc, asked);
    CHECK(with.find("JBSWY3DPEHPK3PXP") != std::string::npos);
    CHECK(with.find("aaaa-1111") != std::string::npos);
    CHECK_MSG(with.find("bbbb-2222 (used)") != std::string::npos,
              "a used code was not marked as used");
    CHECK_MSG(with.find("aaaa-1111 (") == std::string::npos,
              "an unused code was marked");
}

TEST_CASE(Csv_CustomFieldsAreNotDropped)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Sonderfall";
    e.Fields.push_back(Make("Username", "erster", false, FieldRole::Username));
    e.Fields.push_back(Make("Nutzername", "zweiter", false, FieldRole::Username));
    e.Fields.push_back(Make("CustomerNo", "12345"));
    e.Fields.push_back(Make("PIN", "9876", true));
    doc.Entries.push_back(e);

    const Sheet sheet = FirstRow(Export(doc, Plain()));
    CHECK_EQ(sheet.Row.size(), std::size_t{ 15 });
    if (sheet.Row.size() != 14)
        return;

    CHECK(sheet["username"] == "erster");
    const std::string& extra = sheet["extra_fields"];
    CHECK_MSG(extra.find("CustomerNo=12345") != std::string::npos,
              "a custom field is missing from the export");
    CHECK_MSG(extra.find("PIN=9876") != std::string::npos,
              "a custom secret field is missing from the export");
    CHECK_MSG(extra.find("Username=zweiter") != std::string::npos,
              "the second field with the same role was swallowed");
    CHECK_MSG(extra.find("Nutzername=") == std::string::npos,
              "extra_fields carries the label instead of the fixed name");
}

TEST_CASE(Csv_ByteOrderMarkIsWrittenOnRequestOnly)
{
    const std::string with = Export(OneEntry(), CsvOptions{});
    CHECK_MSG(with.compare(0, 3, "\xEF\xBB\xBF") == 0,
              "without a BOM, Excel reads every file in the system code page");
    CHECK(with.compare(3, 2, "id") == 0);

    const std::string without = Export(OneEntry(), Plain());
    CHECK(without.compare(0, 2, "id") == 0);
}

TEST_CASE(Csv_AnEmptyVaultIsAHeaderAndNothingElse)
{
    Document empty;
    const std::string text = Export(empty, Plain());
    CHECK(text.find("id,title") == 0);
    CHECK_MSG(text.find("\r\n") == text.size() - 2,
              "an empty export has more than the header row");
}

TEST_CASE(Csv_AnEmailFillsTheUsernameColumnWhenThereIsNoUsername)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Postfach";
    e.Fields.push_back(Make("Email", "wer@example.de", false, FieldRole::Email));
    e.Fields.push_back(Make("Password", "secret", true, FieldRole::Password));
    doc.Entries.push_back(e);

    const Sheet sheet = FirstRow(Export(doc, Plain()));
    CHECK_EQ(sheet.Row.size(), std::size_t{ 15 });
    if (sheet.Row.size() != 14)
        return;
    CHECK(sheet["username"] == "wer@example.de");
    CHECK_MSG(sheet["extra_fields"].empty(),
              "the address is also in extra_fields");
}

TEST_CASE(Csv_AnEmailNeverDisplacesARealUsername)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Beides";
    e.Fields.push_back(Make("Email", "wer@example.de", false, FieldRole::Email));
    e.Fields.push_back(Make("Username", "wer", false, FieldRole::Username));
    doc.Entries.push_back(e);

    const Sheet sheet = FirstRow(Export(doc, Plain()));
    CHECK_EQ(sheet.Row.size(), std::size_t{ 15 });
    if (sheet.Row.size() != 14)
        return;
    CHECK_MSG(sheet["username"] == "wer", "the address displaced the login name");
    CHECK_MSG(sheet["extra_fields"].find("Email=wer@example.de") != std::string::npos,
              "the address was lost in the export");
}

TEST_CASE(Csv_TheFolderHasItsOwnColumn)
{
    Document doc;
    Entry sorted;
    sorted.Id = 1;
    sorted.Title = "Erster";
    sorted.Folder = "Arbeit, privat";
    doc.Entries.push_back(sorted);

    Entry loose;
    loose.Id = 2;
    loose.Title = "Zweiter";
    doc.Entries.push_back(loose);

    const std::string text = Export(doc, Plain());
    std::size_t at = 0;
    const std::vector<std::string> header = ParseRow(text, at);
    const std::vector<std::string> first = ParseRow(text, at);
    const std::vector<std::string> second = ParseRow(text, at);
    CHECK_EQ(header.size(), std::size_t{ 15 });
    CHECK_EQ(first.size(), std::size_t{ 15 });
    CHECK_EQ(second.size(), std::size_t{ 15 });
    if (first.size() != 14 || second.size() != 14)
        return;

    const std::size_t folder = Col(header, "folder");
    CHECK(first[folder] == "Arbeit, privat");
    CHECK_MSG(second[folder].empty(), "an entry without a folder got one");
    CHECK_MSG(first[Col(header, "extra_fields")].empty(),
              "the folder is also in extra_fields");
}

TEST_CASE(Csv_AFieldCalledFolderDoesNotLandInTheFolderColumn)
{
    Document doc;
    Entry e;
    e.Id = 1;
    e.Title = "Sonderfall";
    e.Fields.push_back(Make("Folder", "not the folder"));
    doc.Entries.push_back(e);

    const Sheet sheet = FirstRow(Export(doc, Plain()));
    CHECK_MSG(sheet["folder"].empty(),
              "a field named Folder was taken for the folder");
    CHECK(sheet["extra_fields"].find("Folder=not the folder") != std::string::npos);
}
