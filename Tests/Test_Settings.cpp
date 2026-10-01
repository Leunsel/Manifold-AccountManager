#include "TestRunner.h"

#include "CodecTestUtil.h"

#include "Core/Codec.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Language.h"
#include "Core/SettingsPolicy.h"
#include "Core/VaultFormat.h"

#include <cstring>

using namespace Bunker;
using namespace Bunker::Vault;

namespace
{
    Settings Loud()
    {
        Settings s;
        s.AutoLockSeconds = 120;
        s.ClipboardClearSeconds = 45;
        s.LockOnFocusLost = true;
        s.FocusGraceSeconds = 33;
        s.LockOnMinimise = true;
        s.BackupEnabled = false;
        s.BackupKeep = 11;
        s.BackupMinIntervalSeconds = 7200;
        s.LastExportUtc = 1700000000ull;
        s.LastExportPath = "D:\\Woother\\Bunker-plaintext.csv";
        s.Language = "de";
        s.SidebarCollapsed = true;
        s.Theme = "fjord";
        s.MonochromeIcons = true;
        s.ListOrder = 1;
        return s;
    }

    Document WithSettings(const Settings& s)
    {
        Document d;
        d.VaultSettings = s;
        return d;
    }
}

TEST_CASE(Settings_EveryFieldSurvivesARoundTrip)
{
    Document doc = WithSettings(Loud());

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    CHECK(w.IsBalanced());
    CHECK(!w.Failed());

    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    const Settings& r = back.VaultSettings;

    CHECK_EQ(r.AutoLockSeconds, std::uint32_t{ 120 });
    CHECK_EQ(r.ClipboardClearSeconds, std::uint32_t{ 45 });
    CHECK_MSG(r.LockOnFocusLost, "LockOnFocusLost did not come back");
    CHECK_EQ(r.FocusGraceSeconds, std::uint32_t{ 33 });
    CHECK_MSG(r.LockOnMinimise, "LockOnMinimise did not come back");
    CHECK_MSG(!r.BackupEnabled, "BackupEnabled did not come back");
    CHECK_EQ(r.BackupKeep, std::uint32_t{ 11 });
    CHECK_EQ(r.BackupMinIntervalSeconds, std::uint32_t{ 7200 });
    CHECK_EQ(r.LastExportUtc, std::uint64_t{ 1700000000 });
    CHECK(r.LastExportPath == "D:\\Woother\\Bunker-plaintext.csv");
    CHECK(r.Language == "de");
    CHECK_MSG(r.SidebarCollapsed, "SidebarCollapsed did not come back");
    CHECK_MSG(r.Theme == "fjord", "Theme did not come back");
    CHECK_MSG(r.MonochromeIcons, "MonochromeIcons did not come back");
    CHECK_EQ(r.ListOrder, std::uint32_t{ 1 });
    CHECK_MSG(r.Unknown.empty(), "a known record ended up among the unknown ones");

    Body again([&](Writer& out) {
        Encode(back, out);
    });
    CHECK(again == w);
}

TEST_CASE(Settings_UnknownRecordsInsideTheSettingsSurvive)
{
    Settings s;
    s.Unknown.push_back(UnknownRecord{ 40, { 1, 2, 3 } });
    s.Unknown.push_back(UnknownRecord{ 41, { 9 } });
    Document doc = WithSettings(s);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));

    CHECK_MSG(back.VaultSettings.Unknown.size() == 2,
              "unknown records in the settings were lost");
    if (back.VaultSettings.Unknown.size() != 2)
        return;
    CHECK_EQ(back.VaultSettings.Unknown[0].Tag, std::uint16_t{ 40 });
    CHECK_EQ(back.VaultSettings.Unknown[1].Tag, std::uint16_t{ 41 });
}

TEST_CASE(Settings_AnAbsentRecordIsTheDefault)
{
    Body inner([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::VaultSettings));
        out.WriteU32(static_cast<std::uint16_t>(SettingsTag::AutoLockSeconds), 300);
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(inner.data(), inner.size(), back));

    const Settings& r = back.VaultSettings;
    const LockSettings fresh;
    CHECK_EQ(r.ClipboardClearSeconds, std::uint32_t{ 20 });
    CHECK_MSG(r.LockOnFocusLost == fresh.LockOnFocusLost,
              "default for LockOnFocusLost differs from LockSettings");
    CHECK_MSG(r.LockOnMinimise == fresh.LockOnMinimise,
              "default for LockOnMinimise differs from LockSettings");
    CHECK_MSG(static_cast<double>(r.FocusGraceSeconds) == fresh.FocusGraceSeconds,
              "default for FocusGraceSeconds differs from LockSettings");
    CHECK_MSG(r.AutoLockSeconds == fresh.IdleSeconds,
              "default for AutoLockSeconds differs from LockSettings");
    CHECK(r.BackupEnabled);
    CHECK_EQ(r.BackupKeep, std::uint32_t{ 7 });
    CHECK_MSG(!r.SidebarCollapsed, "without a record the sidebar must be expanded");
    CHECK_MSG(r.Theme.empty(), "without a record the default scheme applies");
    CHECK_MSG(!r.MonochromeIcons, "without a record the icons are coloured");
    CHECK_EQ(r.ListOrder, std::uint32_t{ 0 });
}

TEST_CASE(Settings_TheDefaultThemeWritesNoRecord)
{
    Document plain;
    Document chosen;
    chosen.VaultSettings.Theme = "light";
    Body a([&](Writer& out) {
        Encode(plain, out);
    });
    Body b([&](Writer& out) {
        Encode(chosen, out);
    });
    CHECK(b.size() > a.size());
    const std::string bytes(reinterpret_cast<const char*>(a.data()), a.size());
    CHECK(bytes.find("light") == std::string::npos);
}

TEST_CASE(Settings_AnExpandedSidebarWritesNoRecord)
{
    Document expanded;
    Document collapsed;
    collapsed.VaultSettings.SidebarCollapsed = true;

    Body plain([&](Writer& out) {
        Encode(expanded, out);
    });
    Body marked([&](Writer& out) {
        Encode(collapsed, out);
    });
    CHECK_MSG(marked.size() > plain.size(), "collapsed wrote no record");

    Document back;
    CHECK(Decode(plain.data(), plain.size(), back));
    CHECK(!back.VaultSettings.SidebarCollapsed);
    CHECK(back.VaultSettings.Unknown.empty());

    collapsed.VaultSettings.SidebarCollapsed = false;
    Body again([&](Writer& out) {
        Encode(collapsed, out);
    });
    CHECK(again == plain);
}

TEST_CASE(Settings_ColouredIconsWriteNoRecord)
{
    Document coloured;
    Document plain;
    plain.VaultSettings.MonochromeIcons = true;

    Body a([&](Writer& out) {
        Encode(coloured, out);
    });
    Body b([&](Writer& out) {
        Encode(plain, out);
    });
    CHECK_MSG(b.size() > a.size(), "grey wrote no record");

    Document back;
    CHECK(Decode(b.data(), b.size(), back));
    CHECK(back.VaultSettings.MonochromeIcons);
    CHECK(back.VaultSettings.Unknown.empty());

    plain.VaultSettings.MonochromeIcons = false;
    Body again([&](Writer& out) {
        Encode(plain, out);
    });
    CHECK(again == a);
}

TEST_CASE(Settings_TheListOrderByNameWritesNoRecord)
{
    Document byName;
    Document byChange;
    byChange.VaultSettings.ListOrder = 1;
    Body a([&](Writer& out) {
        Encode(byName, out);
    });
    Body b([&](Writer& out) {
        Encode(byChange, out);
    });
    CHECK_MSG(b.size() > a.size(), "recently changed wrote no record");
    byChange.VaultSettings.ListOrder = 0;
    Body again([&](Writer& out) {
        Encode(byChange, out);
    });
    CHECK(again == a);
}

TEST_CASE(Settings_AnUnknownListOrderSurvives)
{
    Document doc;
    doc.VaultSettings.ListOrder = 7;
    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK_EQ(back.VaultSettings.ListOrder, std::uint32_t{ 7 });
}

TEST_CASE(Mutate_SetVaultSettingsKeepsTheIconChoice)
{
    Document doc;
    Settings draft;
    draft.MonochromeIcons = true;
    SetVaultSettings(doc, draft);
    CHECK(doc.VaultSettings.MonochromeIcons);
}

TEST_CASE(Mutate_SetVaultSettingsKeepsTheSidebar)
{
    Document doc;
    doc.VaultSettings.SidebarCollapsed = true;
    Settings draft = doc.VaultSettings;
    draft.AutoLockSeconds = 600;
    SetVaultSettings(doc, draft);
    CHECK(doc.VaultSettings.SidebarCollapsed);
}

TEST_CASE(Settings_NothingIsClampedWhileDecoding)
{
    Body inner([&](Writer& out) {
        out.BeginRecord(static_cast<std::uint16_t>(Tag::VaultSettings));
        out.WriteU32(static_cast<std::uint16_t>(SettingsTag::ClipboardClearSeconds),
                       0xFFFFFFFFu);
        out.WriteU32(static_cast<std::uint16_t>(SettingsTag::FocusGraceSeconds), 1000000u);
        out.WriteU32(static_cast<std::uint16_t>(SettingsTag::BackupKeep), 0u);
        out.EndRecord();
    });

    Document back;
    CHECK(Decode(inner.data(), inner.size(), back));
    CHECK_MSG(back.VaultSettings.ClipboardClearSeconds == 0xFFFFFFFFu,
              "decoding clamped the value");
    CHECK_EQ(back.VaultSettings.FocusGraceSeconds, std::uint32_t{ 1000000 });
    CHECK_EQ(back.VaultSettings.BackupKeep, std::uint32_t{ 0 });
}

TEST_CASE(SettingsPolicy_MapsEveryFieldToItsOwnPlace)
{
    const LockSettings p = ToLockSettings(Loud());
    CHECK_EQ(p.IdleSeconds, std::uint32_t{ 120 });
    CHECK_MSG(p.LockOnMinimise, "LockOnMinimise did not reach the policy");
    CHECK_MSG(p.LockOnFocusLost, "LockOnFocusLost did not reach the policy");
    CHECK_MSG(p.FocusGraceSeconds == 33.0,
              "FocusGraceSeconds did not reach the policy");
}

TEST_CASE(SettingsPolicy_ClampsAFileWithoutRewritingIt)
{
    Settings hostile;
    hostile.AutoLockSeconds = 999999;
    hostile.ClipboardClearSeconds = 0xFFFFFFFFu;
    hostile.FocusGraceSeconds = 1000000;

    const LockSettings p = ToLockSettings(hostile);
    CHECK_EQ(p.IdleSeconds, kMaxAutoLockSeconds);
    CHECK_MSG(p.FocusGraceSeconds == static_cast<double>(kMaxFocusGraceSeconds),
              "a hostile grace period reached the policy unclamped");
    CHECK_MSG(ClipboardSeconds(hostile) == kMaxClipboardClearSeconds,
              "0xFFFFFFFF seconds means the clipboard is never cleared");

    CHECK_EQ(hostile.AutoLockSeconds, std::uint32_t{ 999999 });
    CHECK_EQ(hostile.ClipboardClearSeconds, 0xFFFFFFFFu);
}

TEST_CASE(SettingsPolicy_ZeroMeansOffAndStaysOff)
{
    Settings off;
    off.AutoLockSeconds = 0;
    CHECK_MSG(ToLockSettings(off).IdleSeconds == 0,
              "'auto-lock off' was silently switched back on");

    Settings tooSmall;
    tooSmall.AutoLockSeconds = 5;
    CHECK_EQ(ToLockSettings(tooSmall).IdleSeconds, kMinAutoLockSeconds);
}

TEST_CASE(Mutate_SetVaultSettingsKeepsTheStoredUnknowns)
{
    Document doc;
    doc.VaultSettings.Unknown.push_back(UnknownRecord{ 40, { 7 } });

    Settings draft;
    draft.ClipboardClearSeconds = 30;
    SetVaultSettings(doc, draft);

    CHECK_EQ(doc.VaultSettings.ClipboardClearSeconds, std::uint32_t{ 30 });
    CHECK_MSG(doc.VaultSettings.Unknown.size() == 1,
              "unknown settings records were lost when saving");
}

TEST_CASE(Mutate_SetVaultSettingsClampsWhatTheUserTyped)
{
    Document doc;
    Settings draft;
    draft.AutoLockSeconds = 5;
    draft.ClipboardClearSeconds = 99999;
    draft.BackupKeep = 0;
    SetVaultSettings(doc, draft);

    CHECK_EQ(doc.VaultSettings.AutoLockSeconds, kMinAutoLockSeconds);
    CHECK_EQ(doc.VaultSettings.ClipboardClearSeconds, kMaxClipboardClearSeconds);
    CHECK_EQ(doc.VaultSettings.BackupKeep, kMinBackupKeep);

    Settings off;
    off.AutoLockSeconds = 0;
    SetVaultSettings(doc, off);
    CHECK_MSG(doc.VaultSettings.AutoLockSeconds == 0,
              "the funnel clamped 'auto-lock off' upwards");
}

TEST_CASE(Mutate_SettingsChangesDoNotTouchAnyEntry)
{
    Document doc;
    const auto id = AddEntry(doc, [] { Entry e; e.Title = "Bank"; return e; }(), 1000);
    CHECK(id.IsOk());
    if (!id.IsOk())
        return;
    const std::uint64_t before = FindEntry(doc, id.Value())->ModifiedUtc;

    Settings draft;
    draft.ClipboardClearSeconds = 60;
    SetVaultSettings(doc, draft);
    CHECK_MSG(FindEntry(doc, id.Value())->ModifiedUtc == before,
              "a settings change touched an entry");

    RecordExport(doc, 1700000000ull, "D:\\irgendwo.csv");
    CHECK_MSG(FindEntry(doc, id.Value())->ModifiedUtc == before,
              "a recorded export touched an entry");
    CHECK_EQ(doc.VaultSettings.LastExportUtc, std::uint64_t{ 1700000000 });
    CHECK(doc.VaultSettings.LastExportPath == "D:\\irgendwo.csv");
}

TEST_CASE(Settings_TheLanguageBelongsToTheVault)
{
    Settings s;
    s.Language = "de";
    Document doc = WithSettings(s);

    Body w([&](Writer& out) {
        Encode(doc, out);
    });
    Document back;
    CHECK(Decode(w.data(), w.size(), back));
    CHECK(back.VaultSettings.Language == "de");

    Document plain;
    Body empty([&](Writer& out) {
        Encode(plain, out);
    });
    const std::string bytes(reinterpret_cast<const char*>(empty.data()),
                            empty.size());
    CHECK(bytes.find("de") == std::string::npos);
}

TEST_CASE(Settings_AnUnknownLanguageIsEnglish)
{
    Settings s;
    CHECK(std::strcmp(LanguageCode(s), "en") == 0);

    s.Language = "de";
    CHECK(std::strcmp(LanguageCode(s), "de") == 0);

    for (const char* nonsense : { "fr", "", "d", "DE", "de-AT", "xxxxxxxx" })
    {
        s.Language = nonsense;
        const char* got = LanguageCode(s);
        bool known = false;
        for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
            if (std::strcmp(Lang::kLanguages[i].Code, got) == 0)
                known = true;
        CHECK_MSG(known, nonsense);
    }
    s.Language = "DE";
    CHECK(std::strcmp(LanguageCode(s), "en") == 0);
}

TEST_CASE(Mutate_SetVaultSettingsKeepsTheLanguage)
{
    Document doc;
    Settings draft;
    draft.Language = "de";
    SetVaultSettings(doc, draft);
    CHECK(doc.VaultSettings.Language == "de");
}
