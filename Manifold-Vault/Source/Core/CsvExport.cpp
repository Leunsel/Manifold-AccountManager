#include "Core/CsvExport.h"

#include "Core/ListKind.h"

#include "Core/FieldRole.h"
#include "Core/Otp.h"

#include <cstdio>

#include <sodium.h>
#include <cstring>
#include <vector>

namespace Bunker::Vault
{
    namespace
    {
        bool NeedsCsvQuotes(std::string_view value)
        {
            for (const char c : value)
                if (c == ',' || c == '"' || c == '\r' || c == '\n')
                    return true;
            return !value.empty() && (value.front() == ' ' || value.back() == ' ');
        }

        std::size_t QuotedBound(std::size_t n) { return n * 2 + 2; }

        template <class Put>
        void PutQuoted(std::string_view value, Put put)
        {
            put('"');
            for (const char c : value)
            {
                if (c == '"')
                    put('"');
                put(c);
            }
            put('"');
        }

        class SecureText
        {
        public:
            bool Reserve(std::size_t capacity)
            {
                if (capacity <= m_buffer.Size())
                    return true;

                std::size_t want = m_buffer.Size() ? m_buffer.Size() : 4096;
                while (want < capacity)
                    want *= 2;

                SecureBuffer grown(want);
                if (!grown.IsValid())
                    return false;
                if (m_length != 0)
                    std::memcpy(grown.Data(), m_buffer.Data(), m_length);
                m_buffer.Wipe();
                m_buffer = std::move(grown);
                return true;
            }

            bool Append(const char* text, std::size_t len)
            {
                if (len == 0)
                    return true;
                if (!Reserve(m_length + len))
                    return false;
                std::memcpy(m_buffer.Data() + m_length, text, len);
                m_length += len;
                return true;
            }

            bool Append(const std::string& text) { return Append(text.data(), text.size()); }
            bool Append(const char* text) { return Append(text, std::strlen(text)); }
            bool Append(std::string_view text) { return Append(text.data(), text.size()); }

            bool AppendQuoted(std::string_view value)
            {
                if (!NeedsCsvQuotes(value))
                    return Append(value);
                if (!Reserve(m_length + QuotedBound(value.size())))
                    return false;
                unsigned char* out = m_buffer.Data();
                PutQuoted(value, [this, out](char c) {
                    out[m_length++] = static_cast<unsigned char>(c);
                });
                return true;
            }
            bool AppendQuoted(const std::string& value)
            {
                return AppendQuoted(std::string_view(value));
            }

            std::string_view View() const
            {
                return { reinterpret_cast<const char*>(m_buffer.Data()), m_length };
            }
            void Clear() { m_length = 0; }

            bool AppendPercentEncoded(std::string_view value)
            {
                static constexpr char kHex[] = "0123456789ABCDEF";
                for (const char ch : value)
                {
                    const unsigned char c = static_cast<unsigned char>(ch);
                    const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                       (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                                       c == '_' || c == '~';
                    if (plain)
                    {
                        if (!Append(&ch, 1))
                            return false;
                        continue;
                    }
                    const char encoded[3] = { '%', kHex[c >> 4], kHex[c & 0x0Fu] };
                    if (!Append(encoded, 3))
                        return false;
                }
                return true;
            }

            std::size_t Length() const { return m_length; }
            SecureBuffer Take() { m_length = 0; return std::move(m_buffer); }

        private:
            SecureBuffer m_buffer;
            std::size_t m_length = 0;
        };

        const char* const kHeader =
            "id,title,platform,folder,username,password,url,notes,extra_fields,"
            "totp_secret,totp_issuer,recovery_codes,created_utc,modified_utc,"
            "list_items\r\n";

        struct Mapping { FieldRole Role; int Column; };
        constexpr Mapping kMappings[] = {
            { FieldRole::Username, 0 },
            { FieldRole::Password, 1 },
            { FieldRole::Website, 2 },
            { FieldRole::Notes, 3 },
        };

        int ColumnFor(FieldRole role)
        {
            for (const Mapping& m : kMappings)
                if (m.Role == role)
                    return m.Column;
            return -1;
        }

        bool TravelsAsUri(const TotpConfig& c)
        {
            return c.Digits != kDefaultDigits || c.Period != kDefaultPeriodSeconds ||
                   c.Algorithm != HashAlgorithm::Sha1;
        }

        std::string U64(std::uint64_t value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%llu",
                          static_cast<unsigned long long>(value));
            return buffer;
        }
    }

    std::string QuoteCsvField(const std::string& value)
    {
        if (!NeedsCsvQuotes(value))
            return value;

        std::string out;
        out.reserve(QuotedBound(value.size()));
        PutQuoted(value, [&out](char c) { out.push_back(c); });
        return out;
    }

    Status WriteCsv(const Document& doc, const CsvOptions& options, SecureBuffer& out,
                    std::size_t& outLength)
    {
        out.Reset();
        outLength = 0;

        SecureText text;
        if (options.WriteBom && !text.Append("\xEF\xBB\xBF", 3))
            return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };
        if (!text.Append(kHeader))
            return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };

        SecureText extra;
        SecureText codes;
        SecureText totpUri;
        SecureText lists;

        for (const Entry& entry : doc.Entries)
        {
            const SecureString* mapped[4] = {};
            extra.Clear();
            codes.Clear();
            totpUri.Clear();
            lists.Clear();
            bool assembled = true;

            bool hasUsername = false;
            for (const Field& field : entry.Fields)
            {
                if (field.Role == FieldRole::Username)
                    hasUsername = true;
            }

            for (const Field& field : entry.Fields)
            {
                int column = ColumnFor(field.Role);
                if (column < 0 && field.Role == FieldRole::Email && !hasUsername)
                    column = 0;

                if (column >= 0 && mapped[column] == nullptr)
                {
                    mapped[column] = &field.Value;
                    continue;
                }

                const char* name = field.Role == FieldRole::Custom
                                       ? field.Key.c_str()
                                       : CanonicalFieldName(field.Role);
                if (extra.Length() != 0)
                    assembled = assembled && extra.Append("\n", 1);
                assembled = assembled && extra.Append(name);
                assembled = assembled && extra.Append("=", 1);
                assembled = assembled && extra.Append(field.Value.View());
            }

            if (options.IncludeSecondFactor)
            {
                for (const RecoveryCode& code : entry.RecoveryCodes)
                {
                    if (codes.Length() != 0)
                        assembled = assembled && codes.Append("\n", 1);
                    assembled = assembled && codes.Append(code.Value.View());
                    if (code.IsUsed())
                        assembled = assembled && codes.Append(" (used)");
                }
            }

            for (const ListItem& item : entry.ListItems)
            {
                char scratch[12];
                if (lists.Length() != 0)
                    assembled = assembled && lists.Append("\n", 1);
                assembled = assembled && lists.Append(ListKindCsvName(item.Kind, scratch));
                assembled = assembled && lists.Append("\t", 1);
                assembled = assembled && lists.Append(item.Label);
                assembled = assembled && lists.Append("\t", 1);
                assembled = assembled && lists.Append(item.Secret.View());
            }

            const std::string empty;
            const bool second = options.IncludeSecondFactor && entry.HasTotp;

            const TotpConfig& totp = entry.Totp.Config;
            const bool asUri = second && TravelsAsUri(totp);
            if (asUri)
            {
                assembled = assembled && totpUri.Append("otpauth://totp/");
                assembled = assembled && totpUri.AppendPercentEncoded(totp.AccountName);
                assembled = assembled && totpUri.Append("?secret=");
                assembled = assembled && totpUri.AppendPercentEncoded(totp.SecretBase32.View());
                if (!totp.Issuer.empty())
                {
                    assembled = assembled && totpUri.Append("&issuer=");
                    assembled = assembled && totpUri.AppendPercentEncoded(totp.Issuer);
                }
                assembled = assembled && totpUri.Append("&digits=") &&
                            totpUri.Append(U64(totp.Digits));
                assembled = assembled && totpUri.Append("&period=") &&
                            totpUri.Append(U64(totp.Period));
                assembled = assembled && totpUri.Append("&algorithm=") &&
                            totpUri.Append(AlgorithmName(totp.Algorithm));
            }

            bool ok = assembled && text.Append(U64(entry.Id));
            ok = ok && text.Append(",", 1) && text.AppendQuoted(entry.Title);
            ok = ok && text.Append(",", 1) && text.AppendQuoted(entry.PlatformId);
            ok = ok && text.Append(",", 1) && text.AppendQuoted(entry.Folder);
            for (const SecureString* value : mapped)
                ok = ok && text.Append(",", 1) &&
                     text.AppendQuoted(value ? value->View() : std::string_view{});
            ok = ok && text.Append(",", 1) && text.AppendQuoted(extra.View());
            ok = ok && text.Append(",", 1) &&
                 (!second || text.AppendQuoted(asUri ? totpUri.View()
                                                     : totp.SecretBase32.View()));
            ok = ok && text.Append(",", 1) &&
                 text.AppendQuoted(second ? entry.Totp.Config.Issuer : empty);
            ok = ok && text.Append(",", 1) && text.AppendQuoted(codes.View());
            ok = ok && text.Append(",", 1) && text.Append(U64(entry.CreatedUtc));
            ok = ok && text.Append(",", 1) && text.Append(U64(entry.ModifiedUtc));
            ok = ok && text.Append(",", 1) && text.AppendQuoted(lists.View());
            ok = ok && text.Append("\r\n", 2);

            if (!ok)
                return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };
        }

        outLength = text.Length();
        out = text.Take();
        return Ok();
    }
}
