#include "Core/CsvImport.h"

#include "Core/FieldRole.h"
#include "Core/ListKind.h"
#include "Core/Folder.h"
#include "Core/GermanText.h"
#include "Core/Mutate.h"
#include "Core/OtpUri.h"
#include "Core/Search.h"
#include "Core/Secret.h"

#include <sodium.h>

#include <algorithm>
#include <string_view>

namespace Bunker::Vault
{
    namespace
    {
        enum class Column : std::uint8_t
        {
            Ignored = 0,
            Title,
            Platform,
            Folder,
            Username,
            Email,
            Password,
            Url,
            Notes,
            Extra,
            TotpSecret,
            TotpIssuer,
            RecoveryCodes,
            ListItems,
            Custom,
        };

        struct KnownColumn { const char* Name; Column Meaning; };

        constexpr KnownColumn kColumns[] = {
            { "id",             Column::Ignored },
            { "created_utc",    Column::Ignored },
            { "modified_utc",   Column::Ignored },
            { "title",          Column::Title },
            { "platform",       Column::Platform },
            { "folder",         Column::Folder },
            { "username",       Column::Username },
            { "password",       Column::Password },
            { "url",            Column::Url },
            { "notes",          Column::Notes },
            { "extra_fields",   Column::Extra },
            { "totp_secret",    Column::TotpSecret },
            { "totp_issuer",    Column::TotpIssuer },
            { "recovery_codes", Column::RecoveryCodes },
            { "list_items",     Column::ListItems },

            { "account",        Column::Title },
            { "login name",     Column::Username },
            { "web site",       Column::Url },
            { "comments",       Column::Notes },
            { "group",          Column::Folder },

            { "name",           Column::Title },
            { "login_username", Column::Username },
            { "login_password", Column::Password },
            { "login_uri",      Column::Url },
            { "login_totp",     Column::TotpSecret },

            { "grouping",       Column::Folder },
            { "extra",          Column::Notes },
            { "totp",           Column::TotpSecret },

            { "website",        Column::Url },
            { "note",           Column::Notes },
            { "login",          Column::Username },
            { "email",          Column::Email },
            { "otpsecret",      Column::TotpSecret },
            { "otpauth",        Column::TotpSecret },
            { "type",           Column::Ignored },

            { "titel",          Column::Title },
            { "benutzername",   Column::Username },
            { "benutzer",       Column::Username },
            { "passwort",       Column::Password },
            { "kennwort",       Column::Password },
            { "adresse",        Column::Url },
            { "webseite",       Column::Url },
            { "notizen",        Column::Notes },
            { "ordner",         Column::Folder },
            { "gruppe",         Column::Folder },
            { "e-mail",         Column::Email },
        };

        Column MeaningOf(const std::string& folded)
        {
            for (const KnownColumn& k : kColumns)
                if (folded == k.Name)
                    return k.Meaning;

            switch (RoleFromLegacyName(folded.c_str()))
            {
                case FieldRole::Username: return Column::Username;
                case FieldRole::Email:    return Column::Email;
                case FieldRole::Password: return Column::Password;
                case FieldRole::Website:  return Column::Url;
                case FieldRole::Notes:    return Column::Notes;
                default:                  return Column::Custom;
            }
        }

        std::string_view TrimView(std::string_view s)
        {
            std::size_t a = 0;
            std::size_t b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t'))
                ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' ||
                             s[b - 1] == '\r' || s[b - 1] == '\n'))
                --b;
            return s.substr(a, b - a);
        }

        bool AllBlank(const std::vector<std::string_view>& cells)
        {
            for (const std::string_view c : cells)
                if (!TrimView(c).empty())
                    return false;
            return true;
        }

        bool AddField(Entry& e, FieldRole role, const char* key,
                      std::string_view value, bool secret)
        {
            if (value.empty())
                return true;
            Field f;
            f.Role = role;
            f.Key = key;
            f.Value = value;
            if (f.Value.Size() != value.size())
                return false;
            if (secret)
                f.Flags |= kFieldFlagSecret;
            e.Fields.push_back(std::move(f));
            return true;
        }

        bool ParseExtraFields(std::string_view blob, Entry& e)
        {
            std::size_t at = 0;
            while (at < blob.size())
            {
                std::size_t end = blob.find('\n', at);
                if (end == std::string_view::npos)
                    end = blob.size();
                const std::string_view line = TrimView(blob.substr(at, end - at));
                at = end + 1;
                if (line.empty())
                    continue;
                const std::size_t eq = line.find('=');
                if (eq == std::string_view::npos || eq == 0)
                    continue;

                const std::string_view key = TrimView(line.substr(0, eq));
                const std::string_view value = line.substr(eq + 1);
                if (key.empty() || value.empty())
                    continue;

                Field f;
                f.Key = key;
                f.Role = RoleFromLegacyName(LowercaseGerman(f.Key).c_str());
                f.Value = value;
                if (f.Value.Size() != value.size())
                    return false;
                if (f.Role == FieldRole::Password)
                    f.Flags |= kFieldFlagSecret;
                e.Fields.push_back(std::move(f));
            }
            return true;
        }

        bool ParseListItems(std::string_view blob, Entry& e)
        {
            std::size_t at = 0;
            while (at < blob.size())
            {
                std::size_t end = blob.find('\n', at);
                if (end == std::string_view::npos)
                    end = blob.size();
                std::string_view line = blob.substr(at, end - at);
                at = end + 1;
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                if (TrimView(line).empty())
                    continue;

                ListItem item;
                std::string_view value = line;
                const std::size_t tab1 = line.find('\t');
                if (tab1 != std::string_view::npos)
                {
                    std::uint32_t kind = kListKindOther;
                    if (!ListKindFromCsvName(TrimView(line.substr(0, tab1)), kind))
                        kind = kListKindOther;
                    item.Kind = kind;
                    const std::string_view rest = line.substr(tab1 + 1);
                    const std::size_t tab2 = rest.find('\t');
                    if (tab2 != std::string_view::npos)
                    {
                        item.Label = TrimView(rest.substr(0, tab2));
                        value = rest.substr(tab2 + 1);
                    }
                    else
                    {
                        value = rest;
                    }
                }
                if (value.empty())
                    continue;
                item.Secret = value;
                if (item.Secret.Size() != value.size())
                    return false;
                e.ListItems.push_back(std::move(item));
            }
            return true;
        }

        bool ParseRecoveryCodes(std::string_view blob, Entry& e)
        {
            bool stored = true;
            std::size_t at = 0;
            while (at < blob.size())
            {
                std::size_t end = blob.find('\n', at);
                if (end == std::string_view::npos)
                    end = blob.size();
                std::string_view line = TrimView(blob.substr(at, end - at));
                at = end + 1;
                if (line.empty())
                    continue;

                RecoveryCode code;
                constexpr std::string_view kUsed = " (used)";
                if (line.ends_with(kUsed))
                {
                    line = TrimView(line.substr(0, line.size() - kUsed.size()));
                    code.UsedUtc = 1;
                }
                if (line.empty())
                    continue;
                code.Value = line;
                if (code.Value.Size() != line.size())
                    stored = false;
                e.RecoveryCodes.push_back(std::move(code));
            }
            return stored;
        }
    }

    void WipeEntrySecrets(Entry& entry)
    {
        auto scrub = [](std::string& s)
        {
            if (s.empty())
                return;
            sodium_memzero(&s[0], s.size());
            s.clear();
        };

        for (Field& f : entry.Fields)
            WipeField(f);
        entry.Totp.Config.SecretBase32.Wipe();
        scrub(entry.Totp.Config.Issuer);
        scrub(entry.Totp.Config.AccountName);
        scrub(entry.Title);
        scrub(entry.PlatformId);
        scrub(entry.Folder);
        entry.Fields.clear();
        entry.RecoveryCodes.clear();
        DropListItems(entry);
        entry.HasTotp = false;
    }

    const char* RowVerdictName(RowVerdict verdict)
    {
        switch (verdict)
        {
            case RowVerdict::Ready:     return "ready";
            case RowVerdict::Blank:     return "blank";
            case RowVerdict::NoTitle:   return "no title";
            case RowVerdict::Invalid:   return "invalid";
            case RowVerdict::Duplicate: return "duplicate";
        }
        return "?";
    }

    bool ReadCsvField(const char* text, std::size_t length, std::size_t& at,
                      CsvField& out, bool& endOfRecord)
    {
        out = CsvField{};
        endOfRecord = false;
        if (at >= length)
            return false;

        auto delimiter = [&](std::size_t i)
        {
            return text[i] == ',' || text[i] == '\n' || text[i] == '\r';
        };

        if (text[at] == '"')
        {
            ++at;
            const std::size_t from = at;
            for (;;)
            {
                if (at >= length)
                    return false;
                if (text[at] == '"')
                {
                    if (at + 1 < length && text[at + 1] == '"')
                    {
                        ++out.DoubledQuotes;
                        at += 2;
                        continue;
                    }
                    break;
                }
                ++at;
            }
            out.Raw = std::string_view(text + from, at - from);
            ++at;

            if (at < length && !delimiter(at))
            {
                out = CsvField{};
                out.Malformed = true;
                while (at < length && !delimiter(at))
                    ++at;
            }
        }
        else
        {
            const std::size_t from = at;
            while (at < length && !delimiter(at))
                ++at;
            out.Raw = std::string_view(text + from, at - from);
        }

        if (at >= length)
        {
            endOfRecord = true;
            return true;
        }
        if (text[at] == ',')
        {
            ++at;
            return true;
        }
        if (text[at] == '\r')
        {
            ++at;
            if (at < length && text[at] == '\n')
                ++at;
        }
        else if (text[at] == '\n')
        {
            ++at;
        }
        endOfRecord = true;
        return true;
    }

    std::size_t UnescapeCsvField(std::string_view raw, char* dest)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            dest[n++] = raw[i];
            if (raw[i] == '"' && i + 1 < raw.size() && raw[i + 1] == '"')
                ++i;
        }
        return n;
    }

    Status ParseCsv(const char* text, std::size_t length, ImportReport& out)
    {
        out = ImportReport{};
        if (!text || length == 0)
            return Error{ ErrorCode::InvalidArgument, "empty file" };

        std::size_t at = 0;
        if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
        {
            at = 3;
        }

        SecureBuffer unescaped;
        std::vector<std::size_t> doubled;

        enum class Record : std::uint8_t { Read, Malformed, Unterminated, NoMemory };
        auto readRecord = [&](std::vector<std::string_view>& cells) -> Record
        {
            cells.clear();
            doubled.clear();
            bool end = false;
            bool malformed = false;
            std::size_t needed = 0;
            CsvField field;
            while (!end)
            {
                if (!ReadCsvField(text, length, at, field, end))
                    return Record::Unterminated;
                malformed = malformed || field.Malformed;
                if (field.DoubledQuotes != 0)
                {
                    doubled.push_back(cells.size());
                    needed += field.Raw.size() - field.DoubledQuotes;
                }
                cells.push_back(field.Raw);
            }
            if (malformed)
                return Record::Malformed;

            if (unescaped.Size() < needed)
            {
                unescaped = SecureBuffer(std::max(needed, 2 * unescaped.Size()));
                if (!unescaped.IsValid())
                    return Record::NoMemory;
            }
            char* dest = reinterpret_cast<char*>(unescaped.Data());
            for (const std::size_t i : doubled)
            {
                const std::size_t n = UnescapeCsvField(cells[i], dest);
                cells[i] = std::string_view(dest, n);
                dest += n;
            }
            return Record::Read;
        };

        {
            std::vector<std::string_view> header;
            switch (readRecord(header))
            {
                case Record::Read:
                    break;
                case Record::Malformed:
                    return Error{ ErrorCode::InvalidArgument,
                                  "text after a closing quote in the header" };
                case Record::NoMemory:
                    return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };
                case Record::Unterminated:
                default:
                    return Error{ ErrorCode::InvalidArgument,
                                  "unterminated quote in the header" };
            }
            if (header.empty())
                return Error{ ErrorCode::InvalidArgument, "no header row" };
            out.Columns.reserve(header.size());
            for (const std::string_view name : header)
                out.Columns.emplace_back(name);
        }

        std::vector<Column> meaning;
        meaning.reserve(out.Columns.size());
        bool sawAnything = false;
        for (const std::string& name : out.Columns)
        {
            const std::string folded = LowercaseGerman(TrimView(name));
            const Column m = folded.empty() ? Column::Ignored : MeaningOf(folded);
            meaning.push_back(m);
            if (m == Column::Custom)
                out.UnmappedColumns.emplace_back(TrimView(name));
            else if (m != Column::Ignored)
                sawAnything = true;
        }
        if (!sawAnything)
            return Error{ ErrorCode::InvalidArgument,
                          "no column in the header could be recognised" };

        auto lineAt = [&](std::size_t offset) -> std::size_t
        {
            std::size_t line = 1;
            for (std::size_t i = 0; i < offset && i < length; ++i)
                if (text[i] == '\n')
                    ++line;
            return line;
        };

        std::vector<std::string_view> cells;
        while (at < length)
        {
            if (out.Rows.size() >= kMaxImportRows)
                return Error{ ErrorCode::InvalidArgument, "too many rows" };

            const std::size_t startedAt = at;
            const Record read = readRecord(cells);
            if (read == Record::Unterminated)
                return Error{ ErrorCode::InvalidArgument, "unterminated quote" };
            if (read == Record::NoMemory)
                return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };

            ImportRow row;
            row.Line = lineAt(startedAt);

            if (read == Record::Malformed)
            {
                row.Verdict = RowVerdict::Invalid;
                row.Detail = "text after a closing quote";
                ++out.Problems;
                out.Rows.push_back(std::move(row));
                continue;
            }

            if (AllBlank(cells))
            {
                row.Verdict = RowVerdict::Blank;
                ++out.Skipped;
                out.Rows.push_back(std::move(row));
                continue;
            }

            Entry& e = row.Draft;
            std::string_view username, email, password, url, notes, extra;
            std::string_view totpSecret, totpIssuer, codes, lists;
            bool stored = true;

            for (std::size_t i = 0; i < cells.size(); ++i)
            {
                const Column m = i < meaning.size() ? meaning[i] : Column::Ignored;
                const std::string_view value = TrimView(cells[i]);
                if (value.empty())
                    continue;

                switch (m)
                {
                    case Column::Title:         e.Title = value; break;
                    case Column::Platform:      e.PlatformId = LowercaseGerman(value); break;
                    case Column::Folder:        e.Folder = value; break;
                    case Column::Username:      username = value; break;
                    case Column::Email:         email = value; break;
                    case Column::Password:      password = value; break;
                    case Column::Url:           url = value; break;
                    case Column::Notes:         notes = value; break;
                    case Column::Extra:         extra = cells[i]; break;
                    case Column::TotpSecret:    totpSecret = value; break;
                    case Column::TotpIssuer:    totpIssuer = value; break;
                    case Column::RecoveryCodes: codes = cells[i]; break;
                    case Column::ListItems:     lists = cells[i]; break;
                    case Column::Custom:
                    {
                        Field f;
                        f.Role = FieldRole::Custom;
                        f.Key = i < out.Columns.size() ? TrimView(out.Columns[i])
                                                       : std::string_view("field");
                        f.Value = value;
                        stored = stored && f.Value.Size() == value.size();
                        if (!f.Key.empty())
                            e.Fields.push_back(std::move(f));
                        break;
                    }
                    case Column::Ignored:
                    default:
                        break;
                }
            }

            stored = AddField(e, FieldRole::Username, CanonicalFieldName(FieldRole::Username),
                              username, false) && stored;
            stored = AddField(e, FieldRole::Email, CanonicalFieldName(FieldRole::Email),
                              email, false) && stored;
            stored = AddField(e, FieldRole::Password, CanonicalFieldName(FieldRole::Password),
                              password, true) && stored;
            stored = AddField(e, FieldRole::Website, CanonicalFieldName(FieldRole::Website),
                              url, false) && stored;
            stored = AddField(e, FieldRole::Notes, CanonicalFieldName(FieldRole::Notes),
                              notes, false) && stored;
            if (!extra.empty())
                stored = ParseExtraFields(extra, e) && stored;
            if (!codes.empty())
                stored = ParseRecoveryCodes(codes, e) && stored;
            if (!lists.empty())
                stored = ParseListItems(lists, e) && stored;

            if (!totpSecret.empty())
            {
                e.HasTotp = true;
                auto parsed = ParsePastedSecret(totpSecret);
                if (parsed.IsOk())
                {
                    e.Totp.Config = std::move(parsed.Value());
                    if (!totpIssuer.empty())
                        e.Totp.Config.Issuer = totpIssuer;

                    const Status usable = ValidateTotp(e.Totp.Config);
                    if (!usable.IsOk())
                    {
                        e.Totp.Config = TotpConfig{};
                        e.HasTotp = false;
                        row.SecondFactorDropped = true;
                        row.SecondFactorProblem = usable.GetError().Detail
                                                      ? usable.GetError().Detail
                                                      : "not usable";
                    }
                }
                else if (parsed.Code() == ErrorCode::OutOfMemory)
                {
                    e.HasTotp = false;
                    stored = false;
                }
                else
                {
                    e.HasTotp = false;
                    row.SecondFactorDropped = true;
                    row.SecondFactorProblem = parsed.GetError().Detail
                                                  ? parsed.GetError().Detail
                                                  : "not readable";
                }
            }

            if (e.Title.empty())
            {
                const std::string_view* from = nullptr;
                if (!url.empty())          from = &url;
                else if (!username.empty()) from = &username;
                else if (!email.empty())    from = &email;
                if (from)
                {
                    e.Title = *from;
                    row.TitleDerived = true;
                }
            }

            const Status valid = stored
                ? Validate(e)
                : Status{ Error{ ErrorCode::OutOfMemory, "value could not be stored" } };
            if (!valid.IsOk())
            {
                row.SecondFactorDropped = false;
                row.SecondFactorProblem.clear();
                row.Verdict = e.Title.empty() ? RowVerdict::NoTitle : RowVerdict::Invalid;
                row.Detail = valid.GetError().Detail ? valid.GetError().Detail : "rejected";
                WipeEntrySecrets(e);
                row.Draft = Entry{};
                ++out.Problems;
                out.Rows.push_back(std::move(row));
                continue;
            }

            row.Verdict = RowVerdict::Ready;
            ++out.Ready;
            out.Rows.push_back(std::move(row));
        }

        return Status{};
    }

    ImportCommit CommitImport(Document& doc, ImportReport& report, std::uint64_t nowUtc)
    {
        ImportCommit result;
        for (ImportRow& row : report.Rows)
        {
            if (row.Verdict != RowVerdict::Ready)
                continue;
            if (AddEntry(doc, std::move(row.Draft), nowUtc).IsOk())
                ++result.Added;
            else
                ++result.Refused;
            row.Draft = Entry{};
        }
        return result;
    }

    std::size_t MarkDuplicates(const Document& doc, ImportReport& report)
    {
        auto loginOf = [](const Entry& e) -> std::string_view
        {
            for (const Field& f : e.Fields)
                if (f.Role == FieldRole::Username && !f.Value.Empty())
                    return f.Value.View();
            for (const Field& f : e.Fields)
                if (f.Role == FieldRole::Email && !f.Value.Empty())
                    return f.Value.View();
            return {};
        };

        std::size_t marked = 0;
        for (ImportRow& row : report.Rows)
        {
            if (row.Verdict != RowVerdict::Ready)
                continue;

            const std::string_view login = loginOf(row.Draft);

            for (const Entry& e : doc.Entries)
            {
                if (!FoldedEquals(e.Title, row.Draft.Title))
                    continue;
                if (!FoldedEquals(e.PlatformId, row.Draft.PlatformId))
                    continue;
                if (!FoldedEquals(loginOf(e), login))
                    continue;

                row.Verdict = RowVerdict::Duplicate;
                row.Detail = "an entry with this title, platform and login already exists";
                ++marked;
                if (report.Ready > 0)
                    --report.Ready;
                ++report.Duplicates;
                break;
            }
        }
        return marked;
    }
}
