#include "Core/Mutate.h"

#include "Core/SettingsPolicy.h"

#include "Core/Base32.h"
#include "Core/Folder.h"
#include "Core/PasswordHistory.h"
#include "Core/Random.h"
#include "Core/Secret.h"

#include <algorithm>
#include <chrono>

namespace Bunker::Vault
{
    std::uint64_t SystemUtcNow()
    {
        using namespace std::chrono;
        const auto now = system_clock::now().time_since_epoch();
        const auto seconds = duration_cast<std::chrono::seconds>(now).count();
        return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0;
    }

    std::uint64_t SystemUtcMillis()
    {
        using namespace std::chrono;
        const auto now = system_clock::now().time_since_epoch();
        const auto ms = duration_cast<milliseconds>(now).count();
        return ms > 0 ? static_cast<std::uint64_t>(ms) : 0;
    }

    namespace
    {
        bool IsTrimmable(unsigned char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                   c == '\v' || c == '\f';
        }

        void Trim(std::string& s)
        {
            std::size_t begin = 0;
            while (begin < s.size() && IsTrimmable(static_cast<unsigned char>(s[begin])))
                ++begin;
            std::size_t end = s.size();
            while (end > begin && IsTrimmable(static_cast<unsigned char>(s[end - 1])))
                --end;
            s = s.substr(begin, end - begin);
        }

        std::size_t TrimmedLength(const char* text)
        {
            if (!text)
                return 0;
            std::size_t begin = 0;
            while (text[begin] != '\0' &&
                   IsTrimmable(static_cast<unsigned char>(text[begin])))
                ++begin;
            std::size_t end = begin;
            std::size_t last = begin;
            while (text[end] != '\0')
            {
                if (!IsTrimmable(static_cast<unsigned char>(text[end])))
                    last = end + 1;
                ++end;
            }
            return last - begin;
        }

        bool CarriesNothing(const Field& f)
        {
            return f.Key.empty() && f.Value.Empty() && f.Unknown.empty();
        }

        bool ListItemCarriesNothing(const ListItem& item)
        {
            return item.Label.empty() && item.Secret.Empty() && item.Unknown.empty();
        }

        bool HasControlByte(std::string_view text)
        {
            for (const char ch : text)
            {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x20 || c == 0x7F)
                    return true;
            }
            return false;
        }

        void NormaliseListItems(std::vector<ListItem>& items)
        {
            for (ListItem& item : items)
                Trim(item.Label);
            const auto tail = std::remove_if(
                items.begin(), items.end(),
                [](const ListItem& item) { return ListItemCarriesNothing(item); });
            for (auto it = tail; it != items.end(); ++it)
                WipeListItem(*it);
            items.erase(tail, items.end());
        }
    }

    Status ValidateListShape(const DraftListItem* items, std::size_t count)
    {
        if (count > kMaxListItemsPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many list items" };
        for (std::size_t i = 0; i < count; ++i)
        {
            const DraftListItem& item = items[i];
            if (TrimmedLength(item.Label) > kMaxListLabelBytes)
                return Error{ ErrorCode::InvalidArgument, "list description too long" };
            if (item.Label && HasControlByte(item.Label))
                return Error{ ErrorCode::InvalidArgument,
                              "list description with a control character" };
            if (item.SecretBytes > kMaxListSecretBytes)
                return Error{ ErrorCode::InvalidArgument, "list value too long" };
            if (item.SecretBytes == 0 && !item.CarriesUnknown)
                return Error{ ErrorCode::InvalidArgument, "list item without a value" };
        }
        return Status{};
    }

    Status ValidateListItems(const std::vector<ListItem>& items)
    {
        if (items.size() > kMaxListItemsPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many list items" };
        std::vector<DraftListItem> shape(items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            shape[i].Label = items[i].Label.c_str();
            shape[i].SecretBytes = items[i].Secret.Size();
            shape[i].CarriesUnknown = !items[i].Unknown.empty();
        }
        const Status shaped = ValidateListShape(shape.data(), shape.size());
        if (!shaped.IsOk())
            return shaped;
        for (const ListItem& item : items)
            if (HasControlByte(item.Secret.View()))
                return Error{ ErrorCode::InvalidArgument,
                              "list value with a control character" };
        return Status{};
    }

    void Normalise(Entry& entry)
    {
        Trim(entry.Title);
        Trim(entry.PlatformId);
        Trim(entry.Symbol);
        Trim(entry.Folder);
        for (Field& f : entry.Fields)
        {
            Trim(f.Key);
        }
        const auto tail =
            std::remove_if(entry.Fields.begin(), entry.Fields.end(), CarriesNothing);
        for (auto it = tail; it != entry.Fields.end(); ++it)
            WipeField(*it);
        entry.Fields.erase(tail, entry.Fields.end());

        NormaliseListItems(entry.ListItems);
    }

    Status ValidateTotp(const TotpConfig& config)
    {
        if (config.Digits < kMinDigits || config.Digits > kMaxDigits)
            return Error{ ErrorCode::InvalidArgument, "digit count outside 6 to 8" };
        if (config.Period < kMinPeriodSeconds || config.Period > kMaxPeriodSeconds)
            return Error{ ErrorCode::InvalidArgument, "time step out of range" };
        if (config.SecretBase32.Size() > kMaxTotpSecretBytes)
            return Error{ ErrorCode::InvalidArgument, "secret too long" };

        unsigned char key[kMaxTotpKeyBytes];
        return WithDecodedTotpKey<Status>(
            config.SecretBase32, key, sizeof(key),
            [](const unsigned char*, std::size_t keyBytes) -> Status {
                if (keyBytes < kMinTotpSecretBytes)
                    return Error{ ErrorCode::OtpSecretInvalid, "secret too short" };
                return Status{};
            });
    }

    Status ValidateShape(const DraftShape& draft)
    {
        if (TrimmedLength(draft.Title) == 0)
            return Error{ ErrorCode::InvalidArgument, "entry without a title" };
        if (TrimmedLength(draft.Title) > kMaxTitleBytes)
            return Error{ ErrorCode::InvalidArgument, "title too long" };
        if (TrimmedLength(draft.PlatformId) > kMaxPlatformIdBytes)
            return Error{ ErrorCode::InvalidArgument, "platform id too long" };
        if (draft.FieldCount > kMaxFieldsPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many fields" };

        if (draft.Folder && TrimmedLength(draft.Folder) != 0)
        {
            std::string folder = draft.Folder;
            Trim(folder);
            const Status valid = ValidateFolderName(folder);
            if (!valid.IsOk())
                return valid;
        }

        for (std::size_t i = 0; i < draft.FieldCount; ++i)
        {
            const DraftField& f = draft.Fields[i];
            const std::size_t keyLen = TrimmedLength(f.Key);
            if (keyLen == 0 && !f.HasRole && f.ValueBytes != 0)
                return Error{ ErrorCode::InvalidArgument, "field without a name" };
            if (keyLen > kMaxFieldKeyBytes)
                return Error{ ErrorCode::InvalidArgument, "field name too long" };
            if (f.ValueBytes > kMaxFieldValueBytes)
                return Error{ ErrorCode::InvalidArgument, "field value too long" };
        }
        return Status{};
    }

    Status Validate(const Entry& entry)
    {
        if (entry.Fields.size() > kMaxFieldsPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many fields" };

        DraftField fields[kMaxFieldsPerEntry] = {};
        for (std::size_t i = 0; i < entry.Fields.size(); ++i)
        {
            fields[i].Key = entry.Fields[i].Key.c_str();
            fields[i].ValueBytes = entry.Fields[i].Value.Size();
            fields[i].HasRole = entry.Fields[i].Role != FieldRole::Custom;
        }

        DraftShape shape;
        shape.Title = entry.Title.c_str();
        shape.PlatformId = entry.PlatformId.c_str();
        shape.Folder = entry.Folder.c_str();
        shape.Fields = fields;
        shape.FieldCount = entry.Fields.size();

        const Status shaped = ValidateShape(shape);
        if (!shaped.IsOk())
            return shaped;

        if (entry.HasTotp)
        {
            const Status totp = ValidateTotp(entry.Totp.Config);
            if (!totp.IsOk())
                return totp;
        }

        const Status lists = ValidateListItems(entry.ListItems);
        if (!lists.IsOk())
            return lists;

        if (entry.RecoveryCodes.size() > kMaxRecoveryCodesPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many recovery codes" };
        for (const RecoveryCode& c : entry.RecoveryCodes)
        {
            if (c.Value.Empty())
                return Error{ ErrorCode::InvalidArgument, "empty recovery code" };
            if (c.Value.Size() > kMaxRecoveryCodeBytes)
                return Error{ ErrorCode::InvalidArgument, "recovery code too long" };
        }
        return Status{};
    }

    Entry* FindEntry(Document& doc, std::uint64_t id)
    {
        for (Entry& e : doc.Entries)
            if (e.Id == id)
                return &e;
        return nullptr;
    }

    const Entry* FindEntry(const Document& doc, std::uint64_t id)
    {
        for (const Entry& e : doc.Entries)
            if (e.Id == id)
                return &e;
        return nullptr;
    }

    Result<std::uint64_t> AddEntry(Document& doc, const Entry& source, std::uint64_t nowUtc)
    {
        Entry copy;
        if (!CopyEntry(source, copy))
            return Error{ ErrorCode::OutOfMemory, "value could not be stored" };
        return AddEntry(doc, std::move(copy), nowUtc);
    }

    Result<std::uint64_t> AddEntry(Document& doc, Entry&& incoming, std::uint64_t nowUtc)
    {
        Entry draft = std::move(incoming);
        if (doc.Entries.size() >= kMaxEntries)
            return Error{ ErrorCode::InvalidArgument, "vault is full" };

        Normalise(draft);
        const Status valid = Validate(draft);
        if (!valid.IsOk())
            return valid.GetError();

        if (!draft.Folder.empty())
        {
            const std::string existing = ExistingSpelling(doc, draft.Folder);
            if (!existing.empty())
                draft.Folder = existing;
        }

        draft.Id = doc.NextId;
        ++doc.NextId;
        draft.CreatedUtc = nowUtc;
        draft.ModifiedUtc = nowUtc;

        draft.PasswordHistory.clear();

        doc.Entries.push_back(std::move(draft));
        return doc.Entries.back().Id;
    }

    bool EnsureHistoryKey(Document& doc)
    {
        if (doc.HistoryKey.size() == kHistoryKeyBytes)
            return true;
        doc.HistoryKey.assign(kHistoryKeyBytes, 0);
        SystemRandom().Fill(doc.HistoryKey.data(), doc.HistoryKey.size());
        return true;
    }

    namespace
    {
        const Field* PasswordField(const Entry& e)
        {
            for (const Field& f : e.Fields)
                if (f.Role == FieldRole::Password && !f.Value.Empty())
                    return &f;
            return nullptr;
        }

        void RememberOldPassword(Document& doc, Entry& stored,
                                 const Entry& replacement, std::uint64_t nowUtc)
        {
            const Field* before = PasswordField(stored);
            const Field* after = PasswordField(replacement);
            if (!before || !after)
                return;

            if (before->Value == after->Value)
                return;

            if (!EnsureHistoryKey(doc))
                return;

            PasswordMemory memory;
            memory.RetiredUtc = nowUtc;
            const Status made = HistoryMac(doc, stored.Id, before->Value.Bytes(),
                                           before->Value.Size(), memory.Mac);
            if (!made.IsOk())
                return;

            for (const PasswordMemory& old : stored.PasswordHistory)
                if (std::memcmp(old.Mac, memory.Mac, Crypto::kHashBytes) == 0)
                    return;

            stored.PasswordHistory.push_back(memory);
            while (stored.PasswordHistory.size() > kMaxPasswordHistory)
                stored.PasswordHistory.erase(stored.PasswordHistory.begin());
        }
    }

    Status UpdateEntry(Document& doc, std::uint64_t id, const Entry& source,
                       std::uint64_t nowUtc)
    {
        Entry copy;
        if (!CopyEntry(source, copy))
            return Error{ ErrorCode::OutOfMemory, "value could not be stored" };
        return UpdateEntry(doc, id, std::move(copy), nowUtc);
    }

    Status UpdateEntry(Document& doc, std::uint64_t id, Entry&& incoming,
                       std::uint64_t nowUtc)
    {
        Entry replacement = std::move(incoming);
        Entry* stored = FindEntry(doc, id);
        if (!stored)
            return Error{ ErrorCode::InvalidArgument, "entry not found" };

        Normalise(replacement);
        const Status valid = Validate(replacement);
        if (!valid.IsOk())
            return valid;

        RememberOldPassword(doc, *stored, replacement, nowUtc);

        stored->Title = std::move(replacement.Title);
        stored->PlatformId = std::move(replacement.PlatformId);

        for (Field& old : stored->Fields)
            WipeField(old);
        stored->Fields = std::move(replacement.Fields);
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    namespace
    {
        Entry* Require(Document& doc, std::uint64_t id, Status& failure)
        {
            Entry* e = FindEntry(doc, id);
            if (!e)
                failure = Error{ ErrorCode::InvalidArgument, "entry not found" };
            return e;
        }
    }

    Status SetTotp(Document& doc, std::uint64_t id, const TotpConfig& config,
                   std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;

        const Status valid = ValidateTotp(config);
        if (!valid.IsOk())
            return valid;

        TotpConfig incoming = config;
        if (incoming.SecretBase32.Size() != config.SecretBase32.Size())
            return Error{ ErrorCode::OutOfMemory, "value could not be stored" };

        std::vector<UnknownRecord> carried;
        if (stored->HasTotp)
            carried = stored->Totp.Unknown;

        stored->Totp.Config = std::move(incoming);
        stored->Totp.Unknown = std::move(carried);
        stored->HasTotp = true;
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status ClearTotp(Document& doc, std::uint64_t id, std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (!stored->HasTotp)
            return Status{};

        stored->HasTotp = false;
        stored->Totp = StoredTotp{};
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetListItems(Document& doc, std::uint64_t id, std::vector<ListItem>&& items,
                        std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;

        NormaliseListItems(items);
        const Status valid = ValidateListItems(items);
        if (!valid.IsOk())
            return valid;

        if (SameListItems(stored->ListItems, items))
            return Status{};

        for (ListItem& old : stored->ListItems)
            WipeListItem(old);
        stored->ListItems = std::move(items);
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status AddRecoveryCodes(Document& doc, std::uint64_t id,
                            const std::vector<std::string>& codes,
                            std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (codes.empty())
            return Error{ ErrorCode::InvalidArgument, "no codes given" };
        if (stored->RecoveryCodes.size() + codes.size() > kMaxRecoveryCodesPerEntry)
            return Error{ ErrorCode::InvalidArgument, "too many recovery codes" };

        for (const std::string& code : codes)
        {
            if (code.empty())
                return Error{ ErrorCode::InvalidArgument, "empty recovery code" };
            if (code.size() > kMaxRecoveryCodeBytes)
                return Error{ ErrorCode::InvalidArgument, "recovery code too long" };
        }

        std::vector<RecoveryCode> fresh;
        fresh.reserve(codes.size());
        for (const std::string& code : codes)
        {
            RecoveryCode c;
            c.Value = code;
            if (c.Value.Size() != code.size())
                return Error{ ErrorCode::OutOfMemory, "recovery code could not be stored" };
            fresh.push_back(std::move(c));
        }
        for (RecoveryCode& c : fresh)
            stored->RecoveryCodes.push_back(std::move(c));
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status DeleteRecoveryCode(Document& doc, std::uint64_t id, std::size_t index,
                              std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (index >= stored->RecoveryCodes.size())
            return Error{ ErrorCode::InvalidArgument, "index out of range" };

        stored->RecoveryCodes.erase(
            stored->RecoveryCodes.begin() + static_cast<std::ptrdiff_t>(index));
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    void SetVaultSettings(Document& doc, Settings draft)
    {
        draft.Unknown = doc.VaultSettings.Unknown;
        ClampSettings(draft);
        doc.VaultSettings = std::move(draft);
    }

    void RecordExport(Document& doc, std::uint64_t whenUtc, std::string path)
    {
        doc.VaultSettings.LastExportUtc = whenUtc;
        doc.VaultSettings.LastExportPath = std::move(path);
    }

    Status ClearRecoveryCodes(Document& doc, std::uint64_t id, std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (stored->RecoveryCodes.empty())
            return Status{};

        stored->RecoveryCodes.clear();
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetRecoveryCodeUsed(Document& doc, std::uint64_t id, std::size_t index,
                               bool used, std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (index >= stored->RecoveryCodes.size())
            return Error{ ErrorCode::InvalidArgument, "index out of range" };

        stored->RecoveryCodes[index].UsedUtc = used ? nowUtc : 0;
        return Status{};
    }

    Status ValidateFolderName(const std::string& name)
    {
        if (name.empty())
            return Error{ ErrorCode::InvalidArgument, "empty folder name" };
        if (name.size() > kMaxFolderNameBytes)
            return Error{ ErrorCode::InvalidArgument, "folder name too long" };

        for (const char c : name)
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
                return Error{ ErrorCode::InvalidArgument, "folder name contains control characters" };
        return Status{};
    }

    Status SetEntryFolder(Document& doc, std::uint64_t id, std::string folder,
                          std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;

        Trim(folder);
        if (!folder.empty())
        {
            const Status valid = ValidateFolderName(folder);
            if (!valid.IsOk())
                return valid;

            const std::string existing = ExistingSpelling(doc, folder);
            if (!existing.empty())
                folder = existing;
        }

        if (stored->Folder == folder)
            return Status{};

        stored->Folder = std::move(folder);
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetFavourite(Document& doc, std::uint64_t id, bool favourite,
                        std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (stored->Favourite == favourite)
            return Status{};
        stored->Favourite = favourite;
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetEntryTitle(Document& doc, std::uint64_t id, std::string title,
                         std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        Trim(title);
        if (title.empty())
            return Error{ ErrorCode::InvalidArgument, "entry without a title" };
        if (title.size() > kMaxTitleBytes)
            return Error{ ErrorCode::InvalidArgument, "title too long" };
        if (stored->Title == title)
            return Status{};
        stored->Title = std::move(title);
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetAccent(Document& doc, std::uint64_t id, std::uint32_t accent,
                     std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        const std::uint32_t wanted = accent & 0x00FFFFFFu;
        if (stored->Accent == wanted)
            return Status{};
        stored->Accent = wanted;
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetSymbol(Document& doc, std::uint64_t id, std::string symbol,
                     std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        Trim(symbol);
        if (symbol.size() > kMaxSymbolBytes)
            return Error{ ErrorCode::InvalidArgument, "symbol name too long" };
        if (stored->Symbol == symbol)
            return Status{};
        stored->Symbol = std::move(symbol);
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Status SetSecondFactorState(Document& doc, std::uint64_t id,
                                std::uint32_t state, std::uint64_t nowUtc)
    {
        Status failure;
        Entry* stored = Require(doc, id, failure);
        if (!stored)
            return failure;
        if (stored->SecondFactorState == state)
            return Status{};
        stored->SecondFactorState = state;
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }

    Result<std::size_t> RenameFolder(Document& doc, const std::string& from,
                                     const std::string& to, std::uint64_t nowUtc)
    {
        if (from.empty())
            return Error{ ErrorCode::InvalidArgument, "no folder given" };

        std::string target = to;
        Trim(target);
        const Status valid = ValidateFolderName(target);
        if (!valid.IsOk())
            return valid.GetError();

        if (!SameFolder(from, target))
        {
            const std::string existing = ExistingSpelling(doc, target);
            if (!existing.empty())
                target = existing;
        }

        std::size_t moved = 0;
        for (Entry& e : doc.Entries)
        {
            if (e.Folder.empty() || !SameFolder(e.Folder, from))
                continue;
            if (e.Folder == target)
                continue;

            e.Folder = target;
            e.ModifiedUtc = nowUtc;
            ++moved;
        }
        return moved;
    }

    Result<std::size_t> DissolveFolder(Document& doc, const std::string& name,
                                       std::uint64_t nowUtc)
    {
        if (name.empty())
            return Error{ ErrorCode::InvalidArgument, "no folder given" };

        std::size_t moved = 0;
        for (Entry& e : doc.Entries)
        {
            if (e.Folder.empty() || !SameFolder(e.Folder, name))
                continue;
            e.Folder.clear();
            e.ModifiedUtc = nowUtc;
            ++moved;
        }
        return moved;
    }

    Status DeleteEntry(Document& doc, std::uint64_t id)
    {
        for (std::size_t i = 0; i < doc.Entries.size(); ++i)
        {
            if (doc.Entries[i].Id == id)
            {
                doc.Entries.erase(doc.Entries.begin() + static_cast<std::ptrdiff_t>(i));
                return Status{};
            }
        }
        return Error{ ErrorCode::InvalidArgument, "entry not found" };
    }

    Status TakeEntry(Document& doc, std::uint64_t id, Entry& out, std::size_t& index)
    {
        for (std::size_t i = 0; i < doc.Entries.size(); ++i)
        {
            if (doc.Entries[i].Id != id)
                continue;
            out = std::move(doc.Entries[i]);
            doc.Entries.erase(doc.Entries.begin() + static_cast<std::ptrdiff_t>(i));
            index = i;
            return Status{};
        }
        return Error{ ErrorCode::InvalidArgument, "entry not found" };
    }

    Status RestoreEntry(Document& doc, Entry&& incoming, std::size_t index)
    {
        Entry entry = std::move(incoming);
        if (entry.Id == 0 || entry.Id >= doc.NextId)
            return Error{ ErrorCode::InvalidArgument, "entry id was never issued here" };
        if (FindEntry(doc, entry.Id))
            return Error{ ErrorCode::InvalidArgument, "entry id is taken" };
        if (doc.Entries.size() >= kMaxEntries)
            return Error{ ErrorCode::InvalidArgument, "vault is full" };
        const Status valid = Validate(entry);
        if (!valid.IsOk())
            return valid.GetError();

        if (!entry.Folder.empty())
        {
            const std::string existing = ExistingSpelling(doc, entry.Folder);
            if (!existing.empty())
                entry.Folder = existing;
        }

        const std::size_t at = index < doc.Entries.size() ? index : doc.Entries.size();
        doc.Entries.insert(doc.Entries.begin() + static_cast<std::ptrdiff_t>(at),
                           std::move(entry));
        return Status{};
    }

    Status MoveField(Document& doc, std::uint64_t id, std::size_t from,
                     std::size_t to, std::uint64_t nowUtc)
    {
        Entry* stored = FindEntry(doc, id);
        if (!stored)
            return Error{ ErrorCode::InvalidArgument, "entry not found" };
        if (from >= stored->Fields.size() || to >= stored->Fields.size())
            return Error{ ErrorCode::InvalidArgument, "field index out of range" };
        if (from == to)
            return Status{};

        Field moved = std::move(stored->Fields[from]);
        stored->Fields.erase(stored->Fields.begin() + static_cast<std::ptrdiff_t>(from));
        stored->Fields.insert(stored->Fields.begin() + static_cast<std::ptrdiff_t>(to),
                              std::move(moved));
        stored->ModifiedUtc = nowUtc;
        return Status{};
    }
}
