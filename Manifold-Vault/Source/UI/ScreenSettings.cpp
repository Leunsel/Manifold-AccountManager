#include "Core/Language.h"
#include "UI/ScreenSettings.h"

#include "App/Application.h"
#include "App/BunkerTheme.h"
#include "App/VaultSession.h"
#include "Core/Backup.h"
#include "Core/BackupRun.h"
#include "Core/FileSystem.h"
#include "Core/GermanText.h"
#include "Core/CsvExport.h"
#include "Core/CsvImport.h"
#include "Core/Palette.h"
#include "Core/PasswordStrength.h"
#include "Core/PathSafety.h"
#include "UI/SaveFileDialog.h"
#include "UI/SecretInput.h"
#include "UI/Tooltip.h"
#include "Core/Redact.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"
#include "UI/ScrollArea.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/SettingsPolicy.h"
#include "Core/VaultFormat.h"
#include "UI/BunkerFonts.h"
#include "UI/Card.h"
#include "Generated/FaIcons.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Bunker::UI::Settings
{
    using Theme::Token;

    namespace
    {
        bool g_open = false;

        struct Form
        {
            bool Loaded = false;
            bool AutoLock = true;
            int Minutes = 5;
            bool OnMinimise = false;
            bool OnFocus = false;
            int GraceSeconds = 20;
            int ClipSeconds = 20;

            int Language = 0;

            int Theme = 0;
            bool ColouredIcons = true;

            bool Backups = true;
            int Keep = 7;
            int IntervalMinutes = 60;

            std::unique_ptr<SecretField> OldPw;
            std::unique_ptr<SecretField> NewPw;
            std::unique_ptr<SecretField> RepeatPw;
            std::string ChangeMessage;
            bool ChangeWorked = false;

            bool ExportSecondFactor = false;
            bool ExportPickPending = false;
            bool ExportConfirmPending = false;
            std::string ExportPath;
            PathVerdict ExportVerdict;
            std::string ExportMessage;
            bool ExportWorked = false;

            bool ImportPickPending = false;
            bool ImportStandalone = false;
            std::string ImportSummary;
            bool ImportSummaryWorked = false;
            bool ImportConfirmPending = false;
            std::string ImportPath;
            Vault::ImportReport ImportFound;
            std::string ImportMessage;
            bool ImportWorked = false;

            std::size_t CopyCount = 0;
            std::uint64_t NewestUtc = 0;
        };

        Form g_form;

        void LoadForm(const Vault::Settings& s)
        {
            g_form.AutoLock = s.AutoLockSeconds != 0;
            g_form.Minutes = static_cast<int>((s.AutoLockSeconds != 0 ? s.AutoLockSeconds
                                                                      : 300u) / 60u);
            g_form.OnMinimise = s.LockOnMinimise;
            g_form.OnFocus = s.LockOnFocusLost;
            g_form.GraceSeconds = static_cast<int>(s.FocusGraceSeconds);
            g_form.ClipSeconds = static_cast<int>(s.ClipboardClearSeconds);
            g_form.Language = 0;
            for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
                if (s.Language == Lang::kLanguages[i].Code)
                    g_form.Language = static_cast<int>(i);
            g_form.Theme = static_cast<int>(PaletteIndex(s.Theme));
            g_form.ColouredIcons = !s.MonochromeIcons;
            g_form.Backups = s.BackupEnabled;
            g_form.Keep = static_cast<int>(s.BackupKeep);
            g_form.IntervalMinutes = static_cast<int>(s.BackupMinIntervalSeconds / 60u);
            g_form.Loaded = true;
        }

        void CountCopies(const std::string& vaultPath)
        {
            g_form.CopyCount = 0;
            g_form.NewestUtc = 0;

            std::vector<std::string> names;
            if (!RealFileSystem().ListDirectory(Vault::BackupDirectory(vaultPath),
                                                names).IsOk())
            {
                return;
            }
            for (const std::string& name : names)
            {
                Vault::BackupFile parsed;
                if (!Vault::ParseBackupFileName(vaultPath, name, parsed))
                    continue;
                ++g_form.CopyCount;
                std::uint64_t when = 0;
                if (Vault::UtcFromStamp(parsed.Stamp, when) && when > g_form.NewestUtc)
                    g_form.NewestUtc = when;
            }
        }

        void Hint(const char* text)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        void Row(const char* caption)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::TextUnformatted(caption);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 2));
        }

        void Fact(const char* caption, const char* value)
        {
            const Fonts::Set& f = Fonts::Current();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float column = Min(Px(180.0f), ImGui::GetContentRegionAvail().x * 0.45f);
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextUnformatted(caption);
            const bool besides = ImGui::GetItemRectSize().x + Px(10.0f) <= column;
            ImGui::PopStyleColor();
            ImGui::PopFont();

            if (besides)
            {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(at.x + column, at.y));
            }
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::TextWrapped("%s", value);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 3));
        }

        const char* ThemeName(std::size_t index)
        {
            struct Named { const char* Id; Str Name; };
            static constexpr Named kNames[] = {
                { "arc", Str::Settings_ThemeArc },
                { "red", Str::Settings_ThemeRedDark },
                { "red-oled", Str::Settings_ThemeRedOled },
                { "red-light", Str::Settings_ThemeRedLight },
                { "green", Str::Settings_ThemeGreenDark },
                { "green-oled", Str::Settings_ThemeGreenOled },
                { "green-light", Str::Settings_ThemeGreenLight },
                { "dark", Str::Settings_ThemeBlueDark },
                { "blue-oled", Str::Settings_ThemeBlueOled },
                { "light", Str::Settings_ThemeBlueLight },
                { "mint", Str::Settings_ThemeMint },
                { "midnight", Str::Settings_ThemeMidnight },
                { "terminal", Str::Settings_ThemeTerminal },
            };
            const char* id = PaletteAt(index).Id;
            for (const Named& n : kNames)
                if (std::strcmp(n.Id, id) == 0)
                    return T(n.Name);
            return id;
        }

        ImU32 PaletteColour(std::uint32_t rgb)
        {
            return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
        }
    }

        void ForgetImport()
        {
            for (Vault::ImportRow& row : g_form.ImportFound.Rows)
                Vault::WipeEntrySecrets(row.Draft);
            g_form.ImportFound = Vault::ImportReport{};
            g_form.ImportPath.clear();
            g_form.ImportPickPending = false;
            g_form.ImportConfirmPending = false;
        }

        void WipeFields()
        {
            ForgetImport();
            g_form.ImportMessage.clear();
            g_form.ImportWorked = false;
            if (g_form.OldPw) g_form.OldPw->Clear();
            if (g_form.NewPw) g_form.NewPw->Clear();
            if (g_form.RepeatPw) g_form.RepeatPw->Clear();
            g_form.OldPw.reset();
            g_form.NewPw.reset();
            g_form.RepeatPw.reset();
            g_form.ChangeMessage.clear();
            g_form.ChangeWorked = false;
        }

        void DrawImportSection()
        {
            const Fonts::Set& f = Fonts::Current();
            Hint(T(Str::Settings_ImportNote));
            ImGui::Dummy(Px(0, 8));

            char importLabel[96];
            std::snprintf(importLabel, sizeof(importLabel),
                          ICON_FA_FILE_IMPORT "   %s", T(Str::Settings_ImportButton));
            if (ImGui::Button(importLabel,
                              ImVec2(Min(Px(300.0f), ImGui::GetContentRegionAvail().x), Px(32.0f))))
            {
                g_form.ImportMessage.clear();
                g_form.ImportPickPending = true;
            }

            ImGui::Dummy(Px(0, 6));
            Hint(T(Str::Settings_ImportPlainNote));

            if (!g_form.ImportMessage.empty())
            {
                ImGui::Dummy(Px(0, 6));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(g_form.ImportWorked ? Token::GreenCore
                                                                       : Token::RedBright));
                ImGui::TextWrapped("%s", g_form.ImportMessage.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }

        void DrawExportSection(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();

            Hint(T(Str::Settings_ExportWarning));
            ImGui::Dummy(Px(0, 8));

            if (ImGui::Checkbox(T(Str::Settings_ExportSecondFactorBox),
                                &g_form.ExportSecondFactor))
            {
            }
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::Settings_ExportSecondFactorNote));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 8));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::RedCore, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.30f));
            char exportLabel[96];
            std::snprintf(exportLabel, sizeof(exportLabel),
                          ICON_FA_FILE_EXPORT "   %s", T(Str::Settings_ExportButton));
            if (ImGui::Button(exportLabel,
                              ImVec2(Min(Px(300.0f), ImGui::GetContentRegionAvail().x), Px(32.0f))))
            {
                g_form.ExportMessage.clear();
                g_form.ExportPickPending = true;
            }
            ImGui::PopStyleColor(3);

            const Vault::Settings& stored = session.Vault().Doc().VaultSettings;
            if (stored.LastExportUtc != 0)
            {
                ImGui::Dummy(Px(0, 8));
                Fact(T(Str::Settings_LastExport),
                     FormatLocalTimestamp(stored.LastExportUtc).c_str());
                if (!stored.LastExportPath.empty())
                    Fact(T(Str::Settings_LastExportTo), stored.LastExportPath.c_str());
            }

            if (!g_form.ExportMessage.empty())
            {
                ImGui::Dummy(Px(0, 6));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(g_form.ExportWorked ? Token::GreenCore
                                                                       : Token::RedBright));
                ImGui::TextWrapped("%s", g_form.ExportMessage.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }

        void DrawPasswordSection(Application& app, VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();

            if (!g_form.OldPw)
            {
                g_form.OldPw = std::make_unique<SecretField>(256);
                g_form.NewPw = std::make_unique<SecretField>(256);
                g_form.RepeatPw = std::make_unique<SecretField>(256);
            }

            if (session.ChangeFinished())
            {
                const Status result = session.TakeChangeResult();
                g_form.ChangeWorked = result.IsOk();
                if (result.IsOk())
                {
                    g_form.OldPw->Clear();
                    g_form.NewPw->Clear();
                    g_form.RepeatPw->Clear();
                    g_form.ChangeMessage = T(Str::Settings_PasswordChanged);
                }
                else
                {
                    g_form.ChangeMessage = ToUserMessage(result.Code());
                }
            }

            const bool busy = session.IsChangingPassword();
            const float width = Min(Px(420.0f), ImGui::GetContentRegionAvail().x);

            ImGui::BeginDisabled(busy);
            Row(T(Str::Settings_CurrentPassword));
            g_form.OldPw->Draw("##oldpw", T(Str::Settings_CurrentPasswordPlaceholder),
                               width, false,
                               ImGui::GetFrameHeight());
            ImGui::Dummy(Px(0, 6));
            Row(T(Str::Settings_NewPassword));
            g_form.NewPw->Draw("##newpw", T(Str::Settings_NewPasswordPlaceholder),
                               width, false,
                               ImGui::GetFrameHeight());
            ImGui::Dummy(Px(0, 6));
            g_form.RepeatPw->Draw("##newpw2", T(Str::Settings_RepeatPasswordPlaceholder),
                                  width, false,
                                  ImGui::GetFrameHeight());
            ImGui::EndDisabled();

            const std::size_t newLen = g_form.NewPw->Length();
            const bool match = newLen == g_form.RepeatPw->Length() &&
                               (newLen == 0 ||
                                std::memcmp(g_form.NewPw->Data(), g_form.RepeatPw->Data(),
                                            newLen) == 0);

            ImGui::Dummy(Px(0, 6));
            if (newLen > 0)
            {
                const PasswordStrength strength =
                    EstimateStrength(g_form.NewPw->Data(), newLen,
                                     session.Vault().Params());
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::Text(T(Str::Settings_StrengthBits), StrengthVerdict(strength),
                            strength.EntropyBits);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            const bool ready = !busy && g_form.OldPw->Length() > 0 && newLen > 0 && match;
            ImGui::Dummy(Px(0, 6));
            ImGui::BeginDisabled(!ready);
            char changeLabel[96];
            std::snprintf(changeLabel, sizeof(changeLabel),
                          ICON_FA_KEY "   %s", T(Str::Settings_ChangePasswordButton));
            if (ImGui::Button(changeLabel,
                              ImVec2(Min(Px(260.0f), ImGui::GetContentRegionAvail().x), Px(32.0f))))
            {
                g_form.ChangeMessage.clear();
                g_form.ChangeWorked = false;
                session.BeginChangePassword(g_form.OldPw->Data(), g_form.OldPw->Length(),
                                            g_form.NewPw->Data(), newLen);
            }
            ImGui::EndDisabled();

            ImGui::Dummy(Px(0, 6));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            if (busy)
            {
                app.RequestAnimationFrame();
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::AccentBright));
                ImGui::TextUnformatted(T(Str::Settings_Deriving));
                ImGui::PopStyleColor();
            }
            else if (newLen > 0 && !match)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                ImGui::TextUnformatted(T(Str::Settings_PasswordMismatch));
                ImGui::PopStyleColor();
            }
            else if (!g_form.ChangeMessage.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(g_form.ChangeWorked ? Token::GreenCore
                                                                       : Token::RedBright));
                ImGui::TextWrapped("%s", g_form.ChangeMessage.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 4));
            Hint(T(Str::Settings_ChangePasswordNote));
        }

    bool IsOpen() { return g_open; }

    void Open()
    {
        g_open = true;
        g_form.Loaded = false;
    }
    void Close()
    {
        g_open = false;
        g_form.ImportStandalone = false;
        WipeFields();
    }

    const char* ThemeLabel(std::size_t index)
    {
        return ThemeName(index);
    }

    void StartImport()
    {
        g_form.ImportMessage.clear();
        g_form.ImportStandalone = true;
        g_form.ImportPickPending = true;
    }

    std::string TakeImportSummary(bool& worked)
    {
        worked = g_form.ImportSummaryWorked;
        std::string out = std::move(g_form.ImportSummary);
        g_form.ImportSummary.clear();
        return out;
    }

    bool DrawPopups(Application& app, VaultSession& session)
    {
        if (!g_open && !g_form.ImportStandalone)
            return false;
        const auto finishStandalone = [](const std::string& message, bool worked) {
            if (!g_form.ImportStandalone)
                return;
            g_form.ImportStandalone = false;
            g_form.ImportSummary = message;
            g_form.ImportSummaryWorked = worked;
        };

        if (g_form.ExportPickPending)
        {
            g_form.ExportPickPending = false;
            std::string chosen;
            if (AskForExportPath(app.Window(),
                                 SuggestedExportName(Vault::SystemUtcNow()), chosen))
            {
                g_form.ExportPath = chosen;
                g_form.ExportVerdict = AssessExportPath(chosen, FactsFor(chosen),
                                                        KnownCloudRoots(),
                                                        session.Path());
                g_form.ExportConfirmPending = true;
            }
        }

        if (g_form.ExportConfirmPending)
        {
            g_form.ExportConfirmPending = false;
            ImGui::OpenPopup(T(Str::Settings_ExportConfirmTitle));
        }

        if (g_form.ImportPickPending)
        {
            g_form.ImportPickPending = false;
            std::string chosen;
            if (!AskForImportPath(app.Window(), chosen))
                finishStandalone({}, true);
            else
            {
                ForgetImport();
                g_form.ImportPath = chosen;

                SecureBuffer text;
                std::size_t length = 0;
                Status read = ReadIntoSecret(chosen, text, length);
                if (read.IsOk())
                    read = Vault::ParseCsv(reinterpret_cast<const char*>(text.Data()),
                                           length, g_form.ImportFound);

                if (!read.IsOk())
                {
                    char why[260];
                    std::snprintf(why, sizeof(why), T(Str::Settings_ImportFailed),
                                  read.GetError().Detail ? read.GetError().Detail
                                                         : T(Str::Settings_NoDetail));
                    g_form.ImportMessage = why;
                    g_form.ImportWorked = false;
                    finishStandalone(why, false);
                    ForgetImport();
                }
                else
                {
                    Vault::MarkDuplicates(session.Vault().Doc(), g_form.ImportFound);
                    g_form.ImportConfirmPending = true;
                }
            }
        }

        if (g_form.ImportConfirmPending)
        {
            g_form.ImportConfirmPending = false;
            ImGui::OpenPopup("###import");
        }

        bool documentChanged = false;
        ImGui::SetNextWindowSize(Px(560, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(T(Str::Settings_ExportConfirmTitle), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            const Fonts::Set& f = Fonts::Current();

            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            ImGui::TextWrapped(T(Str::Settings_ExportConfirmBody),
                               session.Vault().Doc().Entries.size());
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 8));

            Fact(T(Str::Settings_File), g_form.ExportPath.c_str());
            Fact(T(Str::Settings_SecondFactor),
                 g_form.ExportSecondFactor ? T(Str::Settings_SecondFactorIncluded) : T(Str::Settings_SecondFactorExcluded));

            if (const char* sentence = RiskSentence(g_form.ExportVerdict.Risk))
            {
                ImGui::Dummy(Px(0, 8));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                if (!g_form.ExportVerdict.What.empty())
                    ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s (%s)",
                                       sentence, g_form.ExportVerdict.What.c_str());
                else
                    ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", sentence);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            ImGui::Dummy(Px(0, 14));
            char cancelLabel[96];
            std::snprintf(cancelLabel, sizeof(cancelLabel),
                          ICON_FA_XMARK "   %s", T(Str::Settings_Cancel));
            if (ImGui::Button(cancelLabel, Px(160, 34)))
                ImGui::CloseCurrentPopup();

            ImGui::SameLine(0, Px(10));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::RedCore, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.34f));
            char saveLabel[96];
            std::snprintf(saveLabel, sizeof(saveLabel),
                          ICON_FA_FILE_EXPORT "   %s", T(Str::Settings_ExportConfirmButton));
            if (ImGui::Button(saveLabel, Px(280, 34)))
            {
                Vault::CsvOptions options;
                options.IncludeSecondFactor = g_form.ExportSecondFactor;

                SecureBuffer text;
                std::size_t length = 0;
                Status st = Vault::WriteCsv(session.Vault().Doc(), options, text, length);
                if (st.IsOk())
                    st = RealFileSystem().WriteAtomic(g_form.ExportPath, text.Data(),
                                                      length);
                text.Reset();

                g_form.ExportWorked = st.IsOk();
                if (st.IsOk())
                {
                    Vault::RecordExport(session.Vault().Doc(), Vault::SystemUtcNow(),
                                        g_form.ExportPath);
                    documentChanged = true;
                    g_form.ExportMessage = T(Str::Settings_ExportDone);
                }
                else
                {
                    g_form.ExportMessage = ToUserMessage(st.Code());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);

            ImGui::EndPopup();
        }

        {
            char title[128];
            if (g_form.ImportFound.Ready != 0)
                std::snprintf(title, sizeof(title), "%s###import",
                              T(Str::Import_Title));
            else
                std::snprintf(title, sizeof(title), "%s###import",
                              T(Str::Import_NothingTitle));

            char heading[160];
            if (g_form.ImportFound.Ready != 0)
                std::snprintf(heading, sizeof(heading), title,
                              g_form.ImportFound.Ready);
            else
                std::snprintf(heading, sizeof(heading), "%s", title);

            ImGui::SetNextWindowSize(Px(560, 0), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal(heading, nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings))
            {
                const Fonts::Set& f2 = Fonts::Current();
                const Vault::ImportReport& found = g_form.ImportFound;

                Fact(T(Str::Import_File), g_form.ImportPath.c_str());
                ImGui::Dummy(Px(0, 8));

                char line[320];
                auto say = [&](const char* text, Token colour)
                {
                    ImGui::PushFont(f2.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
                    ImGui::TextWrapped("%s", text);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                };

                auto count = [&](std::size_t n, Str one, Str many, Token colour)
                {
                    if (n == 0)
                        return;
                    if (n == 1)
                        say(T(one), colour);
                    else
                    {
                        std::snprintf(line, sizeof(line), T(many), n);
                        say(line, colour);
                    }
                };

                count(found.Ready, Str::Import_ReadyOne, Str::Import_Ready,
                      Token::TextPrimary);
                count(found.Duplicates, Str::Import_DuplicatesOne,
                      Str::Import_Duplicates, Token::TextSecondary);
                count(found.Problems, Str::Import_ProblemsOne, Str::Import_Problems,
                      Token::WarnBright);
                count(found.Skipped, Str::Import_BlankOne, Str::Import_Blank,
                      Token::TextMuted);

                std::size_t derived = 0;
                for (const Vault::ImportRow& row : found.Rows)
                    if (row.Verdict == Vault::RowVerdict::Ready && row.TitleDerived)
                        ++derived;
                count(derived, Str::Import_DerivedOne, Str::Import_Derived,
                      Token::TextSecondary);

                std::size_t noSecondFactor = 0;
                for (const Vault::ImportRow& row : found.Rows)
                    if (row.Verdict == Vault::RowVerdict::Ready && row.SecondFactorDropped)
                        ++noSecondFactor;
                count(noSecondFactor, Str::Import_NoSecondFactorOne,
                      Str::Import_NoSecondFactor, Token::WarnBright);

                if (!found.UnmappedColumns.empty())
                {
                    std::string names;
                    for (const std::string& c : found.UnmappedColumns)
                    {
                        if (!names.empty())
                            names += ", ";
                        names += c;
                    }
                    std::snprintf(line, sizeof(line), T(Str::Import_UnmappedColumns),
                                  names.c_str());
                    say(line, Token::TextMuted);
                }

                ImGui::Dummy(Px(0, 8));
                say(T(Str::Import_NoTimestamps), Token::TextDisabled);

                if (found.Ready != 0)
                {
                    std::vector<std::size_t> ready;
                    ready.reserve(found.Ready);
                    for (std::size_t i = 0; i < found.Rows.size(); ++i)
                        if (found.Rows[i].Verdict == Vault::RowVerdict::Ready)
                            ready.push_back(i);

                    ImGui::Dummy(Px(0, 10));
                    ImGui::PushFont(f2.Strong, Fonts::Sizes::Label);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::TextUnformatted(T(Str::Import_PreviewList));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::Dummy(Px(0, 4));

                    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
                    const float listHeight =
                        Min(Px(200.0f), rowHeight * static_cast<float>(ready.size()) + Px(8.0f));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Color(Token::Inset));
                    ImGui::BeginChild("##importpreview", ImVec2(Px(520.0f), listHeight),
                                      ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImGui::PushFont(f2.Text, Fonts::Sizes::Small);
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(ready.size()), rowHeight);
                    while (clipper.Step())
                    {
                        for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n)
                        {
                            const Vault::ImportRow& row = found.Rows[ready[static_cast<std::size_t>(n)]];
                            const Vault::Entry& draft = row.Draft;

                            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
                            ImGui::TextUnformatted(draft.Title.c_str());
                            ImGui::PopStyleColor();

                            if (!draft.Folder.empty())
                            {
                                ImGui::SameLine(0, Px(8));
                                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                                ImGui::Text(ICON_FA_FOLDER " %s", draft.Folder.c_str());
                                ImGui::PopStyleColor();
                            }

                            const Vault::Field* login = nullptr;
                            for (const Vault::Field& fld : draft.Fields)
                                if (fld.Role == Vault::FieldRole::Username && !fld.Value.Empty())
                                {
                                    login = &fld;
                                    break;
                                }
                            if (!login)
                                for (const Vault::Field& fld : draft.Fields)
                                    if (fld.Role == Vault::FieldRole::Email && !fld.Value.Empty())
                                    {
                                        login = &fld;
                                        break;
                                    }
                            if (login)
                            {
                                std::string hint = RedactAccountHint(login->Value.View(),
                                                                     kRedactKeepDefault);
                                ImGui::SameLine(0, Px(12));
                                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                                ImGui::TextUnformatted(hint.c_str());
                                ImGui::PopStyleColor();
                                WipeString(hint);
                            }

                            if (row.SecondFactorDropped)
                            {
                                ImGui::SameLine(0, Px(8));
                                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                                ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION);
                                ImGui::PopStyleColor();
                                if (ImGui::IsItemHovered())
                                    UI::Tooltip(row.SecondFactorProblem.c_str());
                            }
                        }
                    }
                    clipper.End();
                    ImGui::PopFont();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                if (found.Problems != 0)
                {
                    ImGui::Dummy(Px(0, 10));
                    ImGui::PushFont(f2.Strong, Fonts::Sizes::Label);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::TextUnformatted(T(Str::Import_ProblemList));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::Dummy(Px(0, 4));

                    constexpr std::size_t kShow = 6;
                    std::size_t shown = 0;
                    for (const Vault::ImportRow& row : found.Rows)
                    {
                        if (row.Verdict == Vault::RowVerdict::Ready ||
                            row.Verdict == Vault::RowVerdict::Blank ||
                            row.Verdict == Vault::RowVerdict::Duplicate)
                            continue;
                        if (shown >= kShow)
                            break;
                        ++shown;
                        std::snprintf(line, sizeof(line), T(Str::Import_ProblemRow),
                                      row.Line, row.Detail.c_str());
                        say(line, Token::TextMuted);
                    }
                    if (found.Problems > shown)
                    {
                        std::snprintf(line, sizeof(line), T(Str::Import_MoreProblems),
                                      found.Problems - shown);
                        say(line, Token::TextMuted);
                    }
                }

                if (noSecondFactor != 0)
                {
                    ImGui::Dummy(Px(0, 10));
                    ImGui::PushFont(f2.Strong, Fonts::Sizes::Label);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::TextUnformatted(T(Str::Import_NoSecondFactorList));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    ImGui::Dummy(Px(0, 4));
                    say(T(Str::Import_NoSecondFactorHint), Token::TextSecondary);

                    constexpr std::size_t kShow = 6;
                    std::size_t shown = 0;
                    for (const Vault::ImportRow& row : found.Rows)
                    {
                        if (row.Verdict != Vault::RowVerdict::Ready || !row.SecondFactorDropped)
                            continue;
                        if (shown >= kShow)
                            break;
                        ++shown;
                        std::snprintf(line, sizeof(line), T(Str::Import_ProblemRow),
                                      row.Line, row.SecondFactorProblem.c_str());
                        say(line, Token::TextMuted);
                    }
                    if (noSecondFactor > shown)
                    {
                        std::snprintf(line, sizeof(line), T(Str::Import_MoreProblems),
                                      noSecondFactor - shown);
                        say(line, Token::TextMuted);
                    }
                }

                ImGui::Dummy(Px(0, 14));

                if (found.Ready != 0)
                {
                    char confirmLabel[96];
                    std::snprintf(confirmLabel, sizeof(confirmLabel),
                                  ICON_FA_FILE_IMPORT "   %s", T(Str::Import_Confirm));
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::AccentCore));
                    if (ImGui::Button(confirmLabel, Px(200, 32)))
                    {
                        const Vault::ImportCommit commit = Vault::CommitImport(
                            session.Vault().Doc(), g_form.ImportFound, Vault::SystemUtcNow());
                        const std::size_t added = commit.Added;
                        const std::size_t refused = commit.Refused;

                        char done[200];
                        if (refused == 0)
                            std::snprintf(done, sizeof(done), T(Str::Import_Done), added);
                        else
                            std::snprintf(done, sizeof(done), T(Str::Import_PartlyDone),
                                          added, refused);
                        g_form.ImportMessage = done;
                        g_form.ImportWorked = refused == 0;
                        finishStandalone(done, refused == 0);
                        documentChanged = added != 0;

                        ForgetImport();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, Px(8));
                }

                if (ImGui::Button(T(Str::Import_Cancel), Px(140, 32)))
                {
                    finishStandalone({}, true);
                    ForgetImport();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        return documentChanged;
    }

    bool Draw(Application& app, VaultSession& session)
    {
        const Fonts::Set& f = Fonts::Current();
        const Vault::Document& doc = session.Vault().Doc();

        if (!g_form.Loaded)
        {
            LoadForm(doc.VaultSettings);
            CountCopies(session.Path());
        }

        bool commit = false;

        const float contentWidth = ScreenContentWidth(ImGui::GetContentRegionAvail().x);
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        char closeLabel[96];
        std::snprintf(closeLabel, sizeof(closeLabel),
                      ICON_FA_XMARK "   %s", T(Str::Settings_Close));
        const float headHeight = Px(30.0f);
        const float closeWidth =
            Max(Px(120.0f), ImGui::CalcTextSize(closeLabel).x +
                            ImGui::GetStyle().FramePadding.x * 2.0f);

        const ImVec2 headAt = origin;
        const float headWidth = contentWidth;
        ImGui::GetWindowDrawList()->AddText(
            f.Strong, Px(Fonts::Sizes::Title),
            ImVec2(headAt.x,
                   headAt.y + (headHeight - Px(Fonts::Sizes::Title)) * 0.5f),
            Theme::U32(Token::TextPrimary), T(Str::Settings_Title));

        ImGui::SetCursorScreenPos(
            ImVec2(headAt.x + headWidth - closeWidth, headAt.y));
        if (ImGui::Button(closeLabel, ImVec2(closeWidth, headHeight)))
            Close();
        ImGui::SetCursorScreenPos(ImVec2(origin.x, headAt.y + headHeight));

        ImGui::Dummy(Px(0, 14));

        BeginScreenBody("##settings", contentWidth);

        Hint(T(Str::Settings_ScopeNote));
        ImGui::Dummy(Px(0, 8));

        CardColumns columns;
        columns.Begin(Px(760.0f), 9);

        BeginCard("##card-lock", columns.Next());
        CardHeader(ICON_FA_LOCK, T(Str::Settings_CardLock), nullptr);

        if (ImGui::Checkbox(T(Str::Settings_LockAfterIdle), &g_form.AutoLock))
            commit = true;

        ImGui::BeginDisabled(!g_form.AutoLock);
        ImGui::Dummy(Px(0, 4));
        Row(T(Str::Settings_WaitTime));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##autolock", &g_form.Minutes,
                         static_cast<int>(Vault::kMinAutoLockSeconds / 60),
                         static_cast<int>(Vault::kMaxAutoLockSeconds / 60),
                         g_form.Minutes == 1 ? T(Str::Settings_MinuteOne)
                                             : T(Str::Settings_MinuteMany),
                         ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;
        ImGui::EndDisabled();

        ImGui::Dummy(Px(0, 8));
        if (ImGui::Checkbox(T(Str::Settings_LockOnMinimise), &g_form.OnMinimise))
            commit = true;
        Hint(T(Str::Settings_LockOnMinimiseNote));

        ImGui::Dummy(Px(0, 8));
        if (ImGui::Checkbox(T(Str::Settings_LockOnFocusLost),
                            &g_form.OnFocus))
            commit = true;

        ImGui::BeginDisabled(!g_form.OnFocus);
        ImGui::Dummy(Px(0, 4));
        Row(T(Str::Settings_GraceTime));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##grace", &g_form.GraceSeconds,
                         static_cast<int>(Vault::kMinFocusGraceSeconds),
                         static_cast<int>(Vault::kMaxFocusGraceSeconds),
                         T(Str::Settings_SecondsFormat), ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;
        Hint(T(Str::Settings_GraceNote));
        ImGui::EndDisabled();
        EndCard();
        columns.Placed();

        BeginCard("##card-language", columns.Next());
        CardHeader(ICON_FA_GLOBE, T(Str::Settings_CardLanguage), nullptr);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##language",
                              Lang::kLanguages[g_form.Language].Name))
        {
            for (std::size_t i = 0; i < Lang::kLanguageCount; ++i)
            {
                const bool selected = g_form.Language == static_cast<int>(i);
                if (ImGui::Selectable(Lang::kLanguages[i].Name, selected))
                {
                    g_form.Language = static_cast<int>(i);
                    commit = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        Hint(T(Str::Settings_LanguageNote));
        if (g_form.Language != 0)
        {
            ImGui::Dummy(Px(0, 2));
            Hint(T(Str::Settings_LanguageIncomplete));
        }
        EndCard();
        columns.Placed();

        BeginCard("##card-theme", columns.Next());
        CardHeader(ICON_FA_PALETTE, T(Str::Settings_CardTheme), nullptr);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##theme", ThemeName(static_cast<std::size_t>(g_form.Theme))))
        {
            for (std::size_t i = 0; i < PaletteCount(); ++i)
            {
                const Palette& p = PaletteAt(i);
                const bool selected = g_form.Theme == static_cast<int>(i);
                ImGui::PushID(static_cast<int>(i));
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float rowHeight = ImGui::GetTextLineHeight() + Px(8.0f);
                if (ImGui::Selectable("##theme-row", selected, 0, ImVec2(0, rowHeight)))
                {
                    g_form.Theme = static_cast<int>(i);
                    commit = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float swatch = Px(18.0f);
                const ImVec2 s0(at.x + Px(2.0f), at.y + (rowHeight - swatch) * 0.5f);
                const ImVec2 s1(s0.x + swatch, s0.y + swatch);
                dl->AddRectFilled(s0, s1, PaletteColour(p.Panel), p.Square ? 0.0f : Px(4.0f));
                dl->AddRect(s0, s1, PaletteColour(p.LineStrong), p.Square ? 0.0f : Px(4.0f));
                dl->AddCircleFilled(ImVec2((s0.x + s1.x) * 0.5f, (s0.y + s1.y) * 0.5f),
                                    swatch * 0.24f, PaletteColour(p.AccentCore));
                dl->AddText(ImVec2(s1.x + Px(10.0f),
                                   at.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), ThemeName(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        Hint(T(Str::Settings_ThemeNote));

        ImGui::Dummy(Px(0, 8));
        if (ImGui::Checkbox(T(Str::Settings_ColouredIcons), &g_form.ColouredIcons))
            commit = true;
        Hint(T(Str::Settings_ColouredIconsNote));
        EndCard();
        columns.Placed();

        BeginCard("##card-clipboard", columns.Next());
        CardHeader(ICON_FA_CLIPBOARD, T(Str::Settings_CardClipboard), nullptr);
        Row(T(Str::Settings_ClipboardClearAfter));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##clip", &g_form.ClipSeconds,
                         static_cast<int>(Vault::kMinClipboardClearSeconds),
                         static_cast<int>(Vault::kMaxClipboardClearSeconds),
                         T(Str::Settings_SecondsFormat), ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;
        Hint(T(Str::Settings_ClipboardNote));
        EndCard();
        columns.Placed();

        BeginCard("##card-backups", columns.Next());
        CardHeader(ICON_FA_FLOPPY_DISK, T(Str::Settings_CardBackups), nullptr);
        if (ImGui::Checkbox(T(Str::Settings_BackupAfterSave), &g_form.Backups))
            commit = true;

        ImGui::BeginDisabled(!g_form.Backups);
        ImGui::Dummy(Px(0, 4));
        Row(T(Str::Settings_BackupKeep));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##keep", &g_form.Keep,
                         static_cast<int>(Vault::kMinBackupKeep),
                         static_cast<int>(Vault::kMaxBackupKeep),
                         g_form.Keep == 1 ? T(Str::Settings_CopyOne) : T(Str::Settings_CopyMany),
                         ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;

        ImGui::Dummy(Px(0, 4));
        Row(T(Str::Settings_BackupInterval));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderInt("##interval", &g_form.IntervalMinutes,
                         static_cast<int>(Vault::kMinBackupIntervalSeconds / 60),
                         static_cast<int>(Vault::kMaxBackupIntervalSeconds / 60),
                         g_form.IntervalMinutes == 1 ? T(Str::Settings_MinuteOne)
                                                     : T(Str::Settings_MinuteMany),
                         ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit())
            commit = true;
        ImGui::EndDisabled();

        ImGui::Dummy(Px(0, 8));
        {
            char line[64];
            std::snprintf(line, sizeof(line), g_form.CopyCount == 1 ? T(Str::Settings_CopyCountOne)
                                                                    : T(Str::Settings_CopyCountMany),
                          g_form.CopyCount);
            Fact(T(Str::Settings_Present), line);
            Fact(T(Str::Settings_Folder), Vault::BackupDirectory(session.Path()).c_str());
            if (g_form.NewestUtc != 0)
                Fact(T(Str::Settings_Newest), FormatLocalTimestamp(g_form.NewestUtc).c_str());
        }

        const Vault::BackupReport& last = app.LastBackup();
        if (!last.Problem.IsOk())
        {
            ImGui::Dummy(Px(0, 4));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
            char backupWarning[256];
            std::snprintf(backupWarning, sizeof(backupWarning),
                          T(Str::Settings_BackupFailed),
                          last.Problem.GetError().Detail
                              ? last.Problem.GetError().Detail
                              : T(Str::Settings_NoDetail));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", backupWarning);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        ImGui::Dummy(Px(0, 4));
        Hint(T(Str::Settings_BackupNote));
        EndCard();
        columns.Placed();

        BeginCard("##card-password", columns.Next());
        CardHeader(ICON_FA_KEY, T(Str::Settings_CardPassword), nullptr);
        DrawPasswordSection(app, session);
        EndCard();
        columns.Placed();

        BeginCard("##card-import", columns.Next());
        CardHeader(ICON_FA_FILE_IMPORT, T(Str::Settings_CardImport), nullptr);
        DrawImportSection();
        EndCard();
        columns.Placed();

        BeginCard("##card-export", columns.Next());
        CardHeader(ICON_FA_FILE_EXPORT, T(Str::Settings_CardExport), nullptr);
        DrawExportSection(session);
        EndCard();
        columns.Placed();

        BeginCard("##card-about", columns.Next());
        CardHeader(ICON_FA_CIRCLE_INFO, T(Str::Settings_CardAbout), nullptr);

        Fact(T(Str::Settings_File), session.Path().c_str());

        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%zu", doc.Entries.size());
        Fact(T(Str::Settings_EntryCount), buffer);

        if (session.LastDerivationSeconds() > 0.0)
        {
            std::snprintf(buffer, sizeof(buffer), T(Str::Settings_LastDerivation),
                          session.LastDerivationSeconds());
            Fact(T(Str::Settings_Duration), buffer);
        }
        EndCard();
        columns.Placed();

        columns.End();
        ImGui::Dummy(Px(0, 10));

        ScrollFade(Token::Panel);
        EndScreenBody();

        if (!commit)
            return false;

        Vault::Settings draft = doc.VaultSettings;
        draft.AutoLockSeconds = g_form.AutoLock
                                    ? static_cast<std::uint32_t>(g_form.Minutes) * 60u
                                    : 0u;
        draft.LockOnMinimise = g_form.OnMinimise;
        draft.LockOnFocusLost = g_form.OnFocus;
        draft.FocusGraceSeconds = static_cast<std::uint32_t>(g_form.GraceSeconds);
        draft.ClipboardClearSeconds = static_cast<std::uint32_t>(g_form.ClipSeconds);
        draft.Language = Lang::kLanguages[g_form.Language].Code;
        draft.Theme = g_form.Theme == 0 ? std::string()
                                        : std::string(PaletteAt(static_cast<std::size_t>(g_form.Theme)).Id);
        draft.MonochromeIcons = !g_form.ColouredIcons;
        draft.BackupEnabled = g_form.Backups;
        draft.BackupKeep = static_cast<std::uint32_t>(g_form.Keep);
        draft.BackupMinIntervalSeconds =
            static_cast<std::uint32_t>(g_form.IntervalMinutes) * 60u;

        Vault::SetVaultSettings(session.Vault().Doc(), draft);
        SetLanguage(Vault::LanguageCode(session.Vault().Doc().VaultSettings));
        LoadForm(session.Vault().Doc().VaultSettings);
        app.ApplyVaultSettings();
        return true;
    }
}
