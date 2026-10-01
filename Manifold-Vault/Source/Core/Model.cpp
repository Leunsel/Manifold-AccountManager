#include "Core/Model.h"

#include "Core/PasswordHistory.h"

#include <sodium.h>

#include <cstring>

#include "Core/VaultFormat.h"

namespace Bunker::Vault
{
    namespace
    {
        constexpr std::uint16_t T(Tag t) { return static_cast<std::uint16_t>(t); }
        constexpr std::uint16_t S(SettingsTag t) { return static_cast<std::uint16_t>(t); }

        void EncodeField(const Field& f, Writer& w)
        {
            w.BeginRecord(T(Tag::EntryField));
            w.WriteString(T(Tag::FieldKey), f.Key);
            w.WriteString(T(Tag::FieldValue), f.Value);
            w.WriteU32(T(Tag::FieldFlags), f.Flags);
            if (f.Role != FieldRole::Custom)
                w.WriteU32(T(Tag::FieldRole), static_cast<std::uint32_t>(f.Role));
            for (const UnknownRecord& u : f.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        void EncodeTotp(const StoredTotp& t, Writer& w)
        {
            w.BeginRecord(T(Tag::EntryTotp));
            w.WriteString(T(Tag::TotpSecret), t.Config.SecretBase32);
            w.WriteU32(T(Tag::TotpDigits), t.Config.Digits);
            w.WriteU32(T(Tag::TotpPeriod), t.Config.Period);
            w.WriteString(T(Tag::TotpAlgorithm), AlgorithmName(t.Config.Algorithm));
            w.WriteString(T(Tag::TotpIssuer), t.Config.Issuer);
            w.WriteString(T(Tag::TotpAccount), t.Config.AccountName);
            for (const UnknownRecord& u : t.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        void EncodePasswordMemory(const PasswordMemory& m, Writer& w)
        {
            w.BeginRecord(T(Tag::EntryPasswordHistory));
            w.WriteBytes(T(Tag::HistoryMacBytes), m.Mac, sizeof(m.Mac));
            w.WriteU64(T(Tag::HistoryRetiredUtc), m.RetiredUtc);
            for (const UnknownRecord& u : m.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        bool DecodePasswordMemory(const unsigned char* data, std::size_t len,
                                  PasswordMemory& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            bool sawMac = false;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::HistoryMacBytes:
                    if (n != sizeof(out.Mac)) return false;
                    std::memcpy(out.Mac, p, n);
                    sawMac = true;
                    break;
                case Tag::HistoryRetiredUtc:
                    if (!Reader::AsU64(p, n, out.RetiredUtc)) return false;
                    break;
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return sawMac && !r.Failed();
        }

        void EncodeRecoveryCode(const RecoveryCode& c, Writer& w)
        {
            w.BeginRecord(T(Tag::EntryRecoveryCode));
            w.WriteString(T(Tag::RecoveryCodeValue), c.Value);
            w.WriteU64(T(Tag::RecoveryCodeUsedUtc), c.UsedUtc);
            for (const UnknownRecord& u : c.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        void EncodeListItem(const ListItem& item, Writer& w)
        {
            w.BeginRecord(T(Tag::EntryListItem));
            if (item.Kind != kListKindOther)
                w.WriteU32(T(Tag::ListItemKind), item.Kind);
            if (!item.Label.empty())
                w.WriteString(T(Tag::ListItemLabel), item.Label);
            w.WriteString(T(Tag::ListItemSecret), item.Secret);
            if (item.AddedUtc != 0)
                w.WriteU64(T(Tag::ListItemAddedUtc), item.AddedUtc);
            for (const UnknownRecord& u : item.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        void EncodeEntry(const Entry& e, Writer& w)
        {
            w.BeginRecord(T(Tag::Entry));
            w.WriteU64(T(Tag::EntryId), e.Id);
            w.WriteString(T(Tag::EntryTitle), e.Title);
            w.WriteString(T(Tag::EntryPlatformId), e.PlatformId);
            w.WriteU64(T(Tag::EntryCreatedUtc), e.CreatedUtc);
            w.WriteU64(T(Tag::EntryModifiedUtc), e.ModifiedUtc);
        if (!e.Folder.empty())
            w.WriteString(T(Tag::EntryFolder), e.Folder);
        if (e.Favourite)
            w.WriteU32(T(Tag::EntryFavourite), 1);
        if (e.Accent != 0)
            w.WriteU32(T(Tag::EntryAccent), e.Accent);
        if (!e.Symbol.empty())
            w.WriteString(T(Tag::EntrySymbol), e.Symbol);
        if (e.SecondFactorState != 0)
            w.WriteU32(T(Tag::EntrySecondFactorState), e.SecondFactorState);
            for (const Field& f : e.Fields)
                EncodeField(f, w);
            if (e.HasTotp)
                EncodeTotp(e.Totp, w);
            for (const RecoveryCode& c : e.RecoveryCodes)
                EncodeRecoveryCode(c, w);
            for (const PasswordMemory& m : e.PasswordHistory)
                EncodePasswordMemory(m, w);
            for (const ListItem& item : e.ListItems)
                EncodeListItem(item, w);
            for (const UnknownRecord& u : e.Unknown)
                w.WriteUnknown(u);
            w.EndRecord();
        }

        bool DecodeField(const unsigned char* data, std::size_t len, Field& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::FieldKey:
                    if (!Reader::AsString(p, n, out.Key)) return false;
                    break;
                case Tag::FieldValue:
                    if (!Reader::AsString(p, n, out.Value)) return false;
                    break;
                case Tag::FieldFlags:
                    if (!Reader::AsU32(p, n, out.Flags)) return false;
                    break;
                case Tag::FieldRole:
                {
                    std::uint32_t role = 0;
                    if (!Reader::AsU32(p, n, role)) return false;
                    out.Role = role <= kFieldRoleMax ? static_cast<FieldRole>(role)
                                                     : FieldRole::Custom;
                    break;
                }
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return !r.Failed();
        }

        bool DecodeTotp(const unsigned char* data, std::size_t len, StoredTotp& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::TotpSecret:
                    if (!Reader::AsString(p, n, out.Config.SecretBase32)) return false;
                    break;
                case Tag::TotpDigits:
                    if (!Reader::AsU32(p, n, out.Config.Digits)) return false;
                    break;
                case Tag::TotpPeriod:
                    if (!Reader::AsU32(p, n, out.Config.Period)) return false;
                    break;
                case Tag::TotpAlgorithm:
                {
                    std::string name;
                    if (!Reader::AsString(p, n, name)) return false;
                    if (!ParseAlgorithm(name.c_str(), out.Config.Algorithm)) return false;
                    break;
                }
                case Tag::TotpIssuer:
                    if (!Reader::AsString(p, n, out.Config.Issuer)) return false;
                    break;
                case Tag::TotpAccount:
                    if (!Reader::AsString(p, n, out.Config.AccountName)) return false;
                    break;
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return !r.Failed();
        }

        bool DecodeRecoveryCode(const unsigned char* data, std::size_t len, RecoveryCode& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::RecoveryCodeValue:
                    if (!Reader::AsString(p, n, out.Value)) return false;
                    break;
                case Tag::RecoveryCodeUsedUtc:
                    if (!Reader::AsU64(p, n, out.UsedUtc)) return false;
                    break;
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return !r.Failed();
        }

        bool DecodeListItem(const unsigned char* data, std::size_t len, ListItem& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::ListItemKind:
                    if (!Reader::AsU32(p, n, out.Kind)) return false;
                    break;
                case Tag::ListItemLabel:
                    if (!Reader::AsString(p, n, out.Label)) return false;
                    break;
                case Tag::ListItemSecret:
                    if (!Reader::AsString(p, n, out.Secret)) return false;
                    break;
                case Tag::ListItemAddedUtc:
                    if (!Reader::AsU64(p, n, out.AddedUtc)) return false;
                    break;
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return !r.Failed();
        }

        bool DecodeEntry(const unsigned char* data, std::size_t len, Entry& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                switch (static_cast<Tag>(tag))
                {
                case Tag::EntryId:
                    if (!Reader::AsU64(p, n, out.Id)) return false;
                    break;
                case Tag::EntryTitle:
                    if (!Reader::AsString(p, n, out.Title)) return false;
                    break;
                case Tag::EntryPlatformId:
                    if (!Reader::AsString(p, n, out.PlatformId)) return false;
                    break;
                case Tag::EntryCreatedUtc:
                    if (!Reader::AsU64(p, n, out.CreatedUtc)) return false;
                    break;
                case Tag::EntryModifiedUtc:
                    if (!Reader::AsU64(p, n, out.ModifiedUtc)) return false;
                    break;
                case Tag::EntryFavourite:
                {
                    std::uint32_t flag = 0;
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.Favourite = flag != 0;
                    break;
                }
                case Tag::EntrySecondFactorState:
                {
                    std::uint32_t state = 0;
                    if (!Reader::AsU32(p, n, state)) return false;
                    out.SecondFactorState = state;
                    break;
                }
                case Tag::EntryAccent:
                {
                    std::uint32_t colour = 0;
                    if (!Reader::AsU32(p, n, colour)) return false;
                    out.Accent = colour & 0x00FFFFFFu;
                    break;
                }
                case Tag::EntrySymbol:
                    if (!Reader::AsString(p, n, out.Symbol)) return false;
                    break;
                case Tag::EntryFolder:
                    if (!Reader::AsString(p, n, out.Folder)) return false;
                    break;
                case Tag::EntryField:
                {
                    Field f;
                    if (!DecodeField(p, n, f)) return false;
                    out.Fields.push_back(std::move(f));
                    break;
                }
                case Tag::EntryTotp:
                {
                    if (out.HasTotp) return false;
                    StoredTotp t;
                    if (!DecodeTotp(p, n, t)) return false;
                    out.Totp = std::move(t);
                    out.HasTotp = true;
                    break;
                }
                case Tag::EntryRecoveryCode:
                {
                    RecoveryCode c;
                    if (!DecodeRecoveryCode(p, n, c)) return false;
                    out.RecoveryCodes.push_back(std::move(c));
                    break;
                }
                case Tag::EntryPasswordHistory:
                {
                    PasswordMemory m;
                    if (!DecodePasswordMemory(p, n, m)) return false;
                    out.PasswordHistory.push_back(std::move(m));
                    break;
                }
                case Tag::EntryListItem:
                {
                    ListItem item;
                    if (!DecodeListItem(p, n, item)) return false;
                    out.ListItems.push_back(std::move(item));
                    break;
                }
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            bool anyRole = false;
            for (const Field& f : out.Fields)
                if (f.Role != FieldRole::Custom)
                    anyRole = true;
            if (!anyRole)
                for (Field& f : out.Fields)
                    f.Role = RoleFromLegacyName(f.Key.c_str());

            return !r.Failed();
        }

        bool DecodeSettings(const unsigned char* data, std::size_t len, Settings& out)
        {
            Reader r(data, len);
            std::uint16_t tag = 0;
            const unsigned char* p = nullptr;
            std::size_t n = 0;
            while (r.Next(tag, p, n))
            {
                std::uint32_t flag = 0;
                switch (static_cast<SettingsTag>(tag))
                {
                case SettingsTag::AutoLockSeconds:
                    if (!Reader::AsU32(p, n, out.AutoLockSeconds)) return false;
                    break;
                case SettingsTag::ClipboardClearSeconds:
                    if (!Reader::AsU32(p, n, out.ClipboardClearSeconds)) return false;
                    break;
                case SettingsTag::LockOnFocusLost:
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.LockOnFocusLost = flag != 0;
                    break;
                case SettingsTag::FocusGraceSeconds:
                    if (!Reader::AsU32(p, n, out.FocusGraceSeconds)) return false;
                    break;
                case SettingsTag::LockOnMinimise:
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.LockOnMinimise = flag != 0;
                    break;
                case SettingsTag::BackupEnabled:
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.BackupEnabled = flag != 0;
                    break;
                case SettingsTag::BackupKeep:
                    if (!Reader::AsU32(p, n, out.BackupKeep)) return false;
                    break;
                case SettingsTag::BackupMinIntervalSeconds:
                    if (!Reader::AsU32(p, n, out.BackupMinIntervalSeconds)) return false;
                    break;
                case SettingsTag::LastExportUtc:
                    if (!Reader::AsU64(p, n, out.LastExportUtc)) return false;
                    break;
                case SettingsTag::LastExportPath:
                    if (!Reader::AsString(p, n, out.LastExportPath)) return false;
                    break;
                case SettingsTag::Language:
                    if (!Reader::AsString(p, n, out.Language)) return false;
                    break;
                case SettingsTag::SidebarCollapsed:
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.SidebarCollapsed = flag != 0;
                    break;
                case SettingsTag::Theme:
                    if (!Reader::AsString(p, n, out.Theme)) return false;
                    break;
                case SettingsTag::MonochromeIcons:
                    if (!Reader::AsU32(p, n, flag)) return false;
                    out.MonochromeIcons = flag != 0;
                    break;
                case SettingsTag::ListOrder:
                    if (!Reader::AsU32(p, n, out.ListOrder)) return false;
                    break;
                default:
                    out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                    break;
                }
            }
            return !r.Failed();
        }
    }

    void Encode(const Document& doc, Writer& w)
    {
        const Settings& s = doc.VaultSettings;
        w.BeginRecord(T(Tag::VaultSettings));
        w.WriteU32(S(SettingsTag::AutoLockSeconds), s.AutoLockSeconds);
        w.WriteU32(S(SettingsTag::ClipboardClearSeconds), s.ClipboardClearSeconds);
        w.WriteU32(S(SettingsTag::LockOnFocusLost), s.LockOnFocusLost ? 1u : 0u);
        w.WriteU32(S(SettingsTag::FocusGraceSeconds), s.FocusGraceSeconds);
        w.WriteU32(S(SettingsTag::LockOnMinimise), s.LockOnMinimise ? 1u : 0u);
        w.WriteU32(S(SettingsTag::BackupEnabled), s.BackupEnabled ? 1u : 0u);
        w.WriteU32(S(SettingsTag::BackupKeep), s.BackupKeep);
        w.WriteU32(S(SettingsTag::BackupMinIntervalSeconds), s.BackupMinIntervalSeconds);
        if (s.LastExportUtc != 0)
            w.WriteU64(S(SettingsTag::LastExportUtc), s.LastExportUtc);
        if (!s.LastExportPath.empty())
            w.WriteString(S(SettingsTag::LastExportPath), s.LastExportPath);
        if (!s.Language.empty())
            w.WriteString(S(SettingsTag::Language), s.Language);
        if (s.SidebarCollapsed)
            w.WriteU32(S(SettingsTag::SidebarCollapsed), 1u);
        if (!s.Theme.empty())
            w.WriteString(S(SettingsTag::Theme), s.Theme);
        if (s.MonochromeIcons)
            w.WriteU32(S(SettingsTag::MonochromeIcons), 1u);
        if (s.ListOrder != 0)
            w.WriteU32(S(SettingsTag::ListOrder), s.ListOrder);
        for (const UnknownRecord& u : doc.VaultSettings.Unknown)
            w.WriteUnknown(u);
        w.EndRecord();

        w.WriteU64(T(Tag::VaultNextEntryId), doc.NextId);
        if (!doc.HistoryKey.empty())
            w.WriteBytes(T(Tag::VaultHistoryKey), doc.HistoryKey.data(),
                         doc.HistoryKey.size());

        for (const Entry& e : doc.Entries)
            EncodeEntry(e, w);

        for (const UnknownRecord& u : doc.Unknown)
            w.WriteUnknown(u);
    }

    namespace
    {
        void WipeUnknown(std::vector<UnknownRecord>& unknown) noexcept
        {
            for (UnknownRecord& u : unknown)
                WipeBytes(u.Payload);
        }
    }

    void WipeField(Field& f) noexcept
    {
        WipeString(f.Key);
        f.Value.Wipe();
        for (UnknownRecord& u : f.Unknown)
            WipeBytes(u.Payload);
    }

    void WipeListItem(ListItem& item) noexcept
    {
        WipeString(item.Label);
        item.Secret.Wipe();
        WipeUnknown(item.Unknown);
    }

    void DropListItems(Entry& e) noexcept
    {
        for (ListItem& item : e.ListItems)
            WipeListItem(item);
        e.ListItems.clear();
    }

    bool CopyListItems(const std::vector<ListItem>& from, std::vector<ListItem>& out)
    {
        std::vector<ListItem> copy = from;
        bool whole = copy.size() == from.size();
        for (std::size_t i = 0; whole && i < from.size(); ++i)
            whole = copy[i].Secret.Size() == from[i].Secret.Size();
        if (!whole)
            return false;
        for (ListItem& old : out)
            WipeListItem(old);
        out = std::move(copy);
        return true;
    }

    bool SameListItems(const std::vector<ListItem>& a, const std::vector<ListItem>& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const ListItem& x = a[i];
            const ListItem& y = b[i];
            if (x.Kind != y.Kind || x.Label != y.Label || x.AddedUtc != y.AddedUtc ||
                x.Unknown.size() != y.Unknown.size())
                return false;
            for (std::size_t k = 0; k < x.Unknown.size(); ++k)
                if (x.Unknown[k].Tag != y.Unknown[k].Tag ||
                    x.Unknown[k].Payload != y.Unknown[k].Payload)
                    return false;
            if (!(x.Secret == y.Secret))
                return false;
        }
        return true;
    }

    bool CopyEntry(const Entry& from, Entry& out)
    {
        Entry copy = from;

        bool whole = copy.Fields.size() == from.Fields.size() &&
                     copy.RecoveryCodes.size() == from.RecoveryCodes.size() &&
                     copy.ListItems.size() == from.ListItems.size() &&
                     copy.Totp.Config.SecretBase32.Size() ==
                         from.Totp.Config.SecretBase32.Size();
        for (std::size_t i = 0; whole && i < from.Fields.size(); ++i)
            whole = copy.Fields[i].Value.Size() == from.Fields[i].Value.Size();
        for (std::size_t i = 0; whole && i < from.RecoveryCodes.size(); ++i)
            whole = copy.RecoveryCodes[i].Value.Size() == from.RecoveryCodes[i].Value.Size();
        for (std::size_t i = 0; whole && i < from.ListItems.size(); ++i)
            whole = copy.ListItems[i].Secret.Size() == from.ListItems[i].Secret.Size();

        if (!whole)
            return false;
        out = std::move(copy);
        return true;
    }

    void WipeDocument(Document& doc) noexcept
    {
        for (Entry& e : doc.Entries)
        {
            WipeString(e.Title);
            WipeString(e.PlatformId);
            WipeString(e.Symbol);
            WipeString(e.Folder);

            for (Field& f : e.Fields)
                WipeField(f);

            e.Totp.Config.SecretBase32.Wipe();
            WipeString(e.Totp.Config.Issuer);
            WipeString(e.Totp.Config.AccountName);
            WipeUnknown(e.Totp.Unknown);

            for (RecoveryCode& c : e.RecoveryCodes)
            {
                c.Value.Wipe();
                WipeUnknown(c.Unknown);
            }

            for (PasswordMemory& m : e.PasswordHistory)
            {
                sodium_memzero(m.Mac, sizeof(m.Mac));
                WipeUnknown(m.Unknown);
            }

            for (ListItem& item : e.ListItems)
                WipeListItem(item);

            WipeUnknown(e.Unknown);
        }

        WipeString(doc.VaultSettings.LastExportPath);
        WipeString(doc.VaultSettings.Language);
        WipeUnknown(doc.VaultSettings.Unknown);

        WipeBytes(doc.HistoryKey);
        WipeUnknown(doc.Unknown);
    }

    Status EncodeToSecure(const Document& doc, SecureBuffer& out, std::size_t& len,
                          std::size_t slack, std::size_t maxBytes)
    {
        len = 0;

        Writer measure = Writer::Measuring();
        Encode(doc, measure);
        if (!measure.IsBalanced())
            return Fail(ErrorCode::InvalidArgument, "unbalanced TLV records");
        if (measure.Failed())
            return Fail(ErrorCode::InvalidArgument, "content contains a null byte");

        Writer::Plan plan;
        if (!measure.Measured(plan))
            return Fail(ErrorCode::InvalidArgument, "measuring pass failed");
        if (plan.Bytes() > maxBytes)
            return Fail(ErrorCode::FileTooLarge, "content too large");
        if (slack > SIZE_MAX - plan.Bytes())
            return Fail(ErrorCode::InvalidArgument, "slack overflows the plan");

        SecureBuffer buf(plan.Bytes() + slack);
        if (!buf.IsValid())
            return Fail(ErrorCode::OutOfMemory, "vault body buffer");

        Writer fill = Writer::Filling(plan, buf.Data(), buf.Size());
        Encode(doc, fill);
        if (!fill.Filled())
        {
            return Fail(ErrorCode::InvalidArgument, "measure and fill disagree");
        }

        out = std::move(buf);
        len = plan.Bytes();
        return Ok();
    }

    bool Decode(const unsigned char* data, std::size_t len, Document& out)
    {
        WipeDocument(out);
        out = Document{};
        Reader r(data, len);
        std::uint16_t tag = 0;
        const unsigned char* p = nullptr;
        std::size_t n = 0;
        while (r.Next(tag, p, n))
        {
            switch (static_cast<Tag>(tag))
            {
            case Tag::VaultSettings:
                if (!DecodeSettings(p, n, out.VaultSettings)) return false;
                break;
            case Tag::VaultHistoryKey:
                if (n != kHistoryKeyBytes) return false;
                out.HistoryKey.assign(p, p + n);
                break;
            case Tag::VaultNextEntryId:
                if (!Reader::AsU64(p, n, out.NextId)) return false;
                break;
            case Tag::Entry:
            {
                Entry e;
                if (!DecodeEntry(p, n, e)) return false;
                out.Entries.push_back(std::move(e));
                break;
            }
            default:
                out.Unknown.push_back(UnknownRecord{ tag, { p, p + n } });
                break;
            }
        }
        if (r.Failed())
            return false;

        const std::uint64_t floor = out.DerivedNextEntryId();
        if (out.NextId < floor)
            out.NextId = floor;
        return true;
    }
}
