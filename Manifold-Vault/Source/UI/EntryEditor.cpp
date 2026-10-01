#include "Core/Language.h"
#include "UI/EntryEditor.h"

#include "App/Application.h"
#include "App/BunkerTheme.h"
#include "App/Clipboard.h"
#include "App/VaultSession.h"
#include "Core/FileSystem.h"
#include "Core/Generator.h"
#include "Core/FieldTemplate.h"
#include "Core/ListKind.h"
#include "Core/AccentColour.h"
#include "Core/Folder.h"
#include "Core/PasswordHistory.h"
#include "Core/Mutate.h"
#include "Core/Secret.h"
#include "Core/SettingsPolicy.h"
#include "Core/OtpUri.h"
#include "Core/Random.h"
#include "Core/VaultFormat.h"
#include "UI/BunkerFonts.h"
#include "UI/IconButton.h"
#include "UI/Modal.h"
#include "UI/Tooltip.h"
#include "UI/PlatformIcon.h"
#include "UI/SecretInput.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"
#include "UI/ScrollArea.h"
#include "UI/Shortcuts.h"
#include "UI/Trash.h"
#include "Core/Search.h"
#include "Generated/FaBrands.h"
#include "Generated/FaIcons.h"
#include "Generated/FaSymbols.h"

#include "imgui.h"

#include <sodium.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace Bunker::UI::Editor
{
    using Theme::Token;
    using Vault::Entry;
    using Vault::Field;

    namespace
    {
        constexpr std::size_t kValueCapacity = Vault::kMaxFieldValueBytes + 1;

        struct Row
        {
            char Key[128] = {};
            char Value[kValueCapacity] = {};
            bool Secret = false;
            Vault::FieldRole Role = Vault::FieldRole::Custom;
            std::unique_ptr<SecretField> Hidden;
            std::vector<Vault::UnknownRecord> Unknown;

            Row() = default;
            Row(Row&&) noexcept = default;
            Row& operator=(Row&&) noexcept = default;
            ~Row()
            {
                sodium_memzero(Key, sizeof(Key));
                sodium_memzero(Value, sizeof(Value));
            }
        };
        static_assert(std::is_nothrow_move_constructible_v<Row>,
                      "otherwise the vector copies every row when it grows");

        struct ItemRow
        {
            std::uint32_t Kind = Vault::kListKindOther;
            char Label[Vault::kMaxListLabelBytes + 1] = {};
            SecureString Secret;
            std::uint64_t AddedUtc = 0;
            std::vector<Vault::UnknownRecord> Unknown;

            ItemRow() = default;
            ItemRow(ItemRow&&) noexcept = default;
            ItemRow& operator=(ItemRow&&) noexcept = default;
            ~ItemRow() { sodium_memzero(Label, sizeof(Label)); }
        };
        static_assert(std::is_nothrow_move_constructible_v<ItemRow>,
                      "otherwise the vector copies every item when it grows");

        constexpr std::size_t kNoItem = static_cast<std::size_t>(-1);
        constexpr int kGenTargetListComposer = -2;

        struct Draft
        {
            bool Open = false;
            std::uint64_t Id = 0;
            char Title[256] = {};
            char PlatformId[96] = {};
            char Symbol[64] = {};
            std::uint32_t Accent = 0;
            std::string Folder;
            std::string LoadedFolder;
            std::vector<Row> Rows;
            std::string Problem;
            std::string SaveProblem;
            bool LoadIncomplete = false;
            bool PendingDelete = false;
            bool FocusTitle = false;

            bool HasTotp = false;
            std::uint32_t SecondFactorState = 0;
            TotpConfig Totp;
            std::unique_ptr<SecretField> Paste;
            std::string PasteProblem;
            bool PasteAccepted = false;

            char NewCodes[1024] = {};

            std::vector<ItemRow> Items;
            std::uint32_t ComposeKind = Vault::kListKindOther;
            bool ComposeKindChosen = false;
            char ComposeLabel[Vault::kMaxListLabelBytes + 1] = {};
            std::unique_ptr<SecretField> ComposeSecret;
            bool ComposeFocusLabel = false;
            std::size_t ReplaceIndex = kNoItem;
            std::unique_ptr<SecretField> ReplaceSecret;
            std::size_t RevealItem = kNoItem;
            double RevealStarted = 0.0;
        };

        Draft g_draft;

        constexpr const char* kLeavePopup = "###leave-unsaved";
        std::function<void()> g_afterLeave;
        bool g_leavePending = false;
        bool g_leaveAsking = false;

        void ForgetLeave()
        {
            g_afterLeave = nullptr;
            g_leavePending = false;
            g_leaveAsking = false;
        }

        constexpr const char* kFolderPopup = "###folder-create";

        bool g_folderCreatePending = false;
        char g_newFolderName[128] = {};

        struct GeneratorState
        {
            GeneratorOptions Options;
            bool Passphrase = false;
            int Words = 5;
            std::string Preview;
            double EntropyBits = 0.0;
            std::string Problem;
            int Target = -1;
            bool PendingOpen = false;
        };

        GeneratorState g_gen;

        void ClearRow(Row& row)
        {
            sodium_memzero(row.Key, sizeof(row.Key));
            sodium_memzero(row.Value, sizeof(row.Value));
            if (row.Hidden)
                row.Hidden->Clear();
            row.Hidden.reset();
            row.Unknown.clear();
        }

        std::unique_ptr<SecretField> NewSecretField(std::size_t capacity)
        {
            auto field = std::make_unique<SecretField>(capacity);
            field->SetEscapeClears(false);
            return field;
        }

        bool MakeSecret(Row& row, bool on)
        {
            if (on == row.Secret)
                return true;
            if (on)
            {
                std::unique_ptr<SecretField> hidden = NewSecretField(kValueCapacity);
                if (!hidden->Set(row.Value, std::strlen(row.Value)))
                    return false;
                row.Hidden = std::move(hidden);
                sodium_memzero(row.Value, sizeof(row.Value));
            }
            else
            {
                if (row.Hidden)
                {
                    const char* text = row.Hidden->Data();
                    const std::size_t n = Min(std::strlen(text), sizeof(row.Value) - 1);
                    std::memcpy(row.Value, text, n);
                    row.Value[n] = 0;
                    row.Hidden->Clear();
                }
                row.Hidden.reset();
            }
            row.Secret = on;
            return true;
        }

        bool CopyInto(char* dst, std::size_t cap, std::string_view src)
        {
            const std::size_t n = Min(src.size(), cap - 1);
            std::memcpy(dst, src.data(), n);
            dst[n] = 0;
            return n == src.size();
        }

        Row RowFrom(const Field& f)
        {
            Row row;
            CopyInto(row.Key, sizeof(row.Key), f.Key);
            row.Role = f.Role;
            row.Unknown = f.Unknown;
            if (f.IsSecret())
            {
                row.Secret = true;
                row.Hidden = NewSecretField(kValueCapacity);
                if (!row.Hidden->Set(f.Value.CStr(), f.Value.Size()))
                    g_draft.LoadIncomplete = true;
            }
            else if (!CopyInto(row.Value, sizeof(row.Value), f.Value.View()))
            {
                g_draft.LoadIncomplete = true;
            }
            return row;
        }

        bool Assemble(Entry& e)
        {
            e.Title = g_draft.Title;
            e.PlatformId = g_draft.PlatformId;
            for (const Row& row : g_draft.Rows)
            {
                Field f;
                f.Key = row.Key;
                f.Role = row.Role;
                f.Unknown = row.Unknown;
                if (row.Secret)
                {
                    f.Flags |= Vault::kFieldFlagSecret;
                    if (row.Hidden)
                    {
                        f.Value.Assign(row.Hidden->Data(), row.Hidden->Length());
                        if (f.Value.Size() != row.Hidden->Length())
                            return false;
                    }
                }
                else
                {
                    const std::size_t n = std::strlen(row.Value);
                    f.Value.Assign(row.Value, n);
                    if (f.Value.Size() != n)
                        return false;
                }
                e.Fields.push_back(std::move(f));
            }
            return true;
        }

        void ResetDraft()
        {
            for (Row& row : g_draft.Rows)
                ClearRow(row);
            g_draft.Rows.clear();
            g_draft.Rows.reserve(Vault::kMaxFieldsPerEntry);
            std::memset(g_draft.Title, 0, sizeof(g_draft.Title));
            std::memset(g_draft.PlatformId, 0, sizeof(g_draft.PlatformId));
            std::memset(g_draft.Symbol, 0, sizeof(g_draft.Symbol));
            g_draft.Folder.clear();
            g_draft.LoadedFolder.clear();
            g_draft.Accent = 0;
            g_draft.Problem.clear();
            g_draft.SaveProblem.clear();
            g_draft.LoadIncomplete = false;
            g_draft.HasTotp = false;
            g_draft.SecondFactorState = 0;
            g_draft.Totp = TotpConfig{};
            if (g_draft.Paste)
                g_draft.Paste->Clear();
            g_draft.Paste.reset();
            g_draft.PasteProblem.clear();
            g_draft.PasteAccepted = false;
            std::memset(g_draft.NewCodes, 0, sizeof(g_draft.NewCodes));
            g_draft.Items.clear();
            g_draft.ComposeKind = Vault::kListKindOther;
            g_draft.ComposeKindChosen = false;
            sodium_memzero(g_draft.ComposeLabel, sizeof(g_draft.ComposeLabel));
            if (g_draft.ComposeSecret)
                g_draft.ComposeSecret->Clear();
            g_draft.ComposeSecret.reset();
            g_draft.ComposeFocusLabel = false;
            g_draft.ReplaceIndex = kNoItem;
            if (g_draft.ReplaceSecret)
                g_draft.ReplaceSecret->Clear();
            g_draft.ReplaceSecret.reset();
            g_draft.RevealItem = kNoItem;
            g_draft.PendingDelete = false;
            g_draft.Id = 0;
            g_draft.Open = false;
            WipeString(g_gen.Preview);
            g_gen.Target = -1;
            g_gen.PendingOpen = false;
            g_folderCreatePending = false;
            g_newFolderName[0] = 0;
        }

        void Label(const char* text)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        constexpr float kSlotWidth = 30.0f;
        constexpr float kSlotGap = 4.0f;
        constexpr int kSlotCount = 6;
        constexpr float kToolStripWidth =
            kSlotCount * kSlotWidth + (kSlotCount - 1) * kSlotGap;

        bool SlotButton(const char* icon, const char* id, const char* tooltip,
                        bool danger = false)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.0f));
            const bool hit = UI::IconButton(
                id, icon, ImVec2(Px(kSlotWidth), ImGui::GetFrameHeight()),
                danger ? Token::RedBright : Token::TextMuted, tooltip);
            ImGui::PopStyleColor();
            return hit;
        }

        void EmptySlot()
        {
            ImGui::Dummy(ImVec2(Px(kSlotWidth), ImGui::GetFrameHeight()));
        }

        constexpr const char* kGeneratorPopup = "###generator";
        constexpr const char* kDeletePopup = "###entry-remove";
        constexpr const char* kIconPopup = "###iconpicker";

        bool g_iconPickerPending = false;
        char g_iconSearch[64] = {};
        bool g_iconBrands = false;

        void Regenerate()
        {
            g_gen.Problem.clear();
            WipeString(g_gen.Preview);

            if (g_gen.Passphrase)
            {
                const auto r = GeneratePassphrase(static_cast<std::size_t>(g_gen.Words),
                                                         '-', SystemRandom());
                if (!r.IsOk())
                {
                    g_gen.Problem = T(Str::Editor_PassphraseFailed);
                    return;
                }
                g_gen.Preview = r.Value().Text;
                g_gen.EntropyBits = r.Value().EntropyBits;
                return;
            }

            const auto r = GeneratePassword(g_gen.Options, SystemRandom());
            if (!r.IsOk())
            {
                const char* detail = r.GetError().Detail;
                g_gen.Problem = detail ? detail : T(Str::Editor_OptionsImpossible);
                return;
            }
            g_gen.Preview = r.Value().Text;
            g_gen.EntropyBits = r.Value().EntropyBits;
        }

        struct GeneratorButtons
        {
            char Again[64] = {};
            char Copy[64] = {};
            char Apply[64] = {};
            float AgainWidth = 0.0f;
            float CopyWidth = 0.0f;
            float ApplyWidth = 0.0f;
            float CloseWidth = 0.0f;
            float Gap = 0.0f;
            float Between = 0.0f;
            bool CanApply = false;

            float LeftWidth() const { return AgainWidth + Gap + CopyWidth; }
            float RightWidth() const
            {
                return (CanApply ? ApplyWidth + Gap : 0.0f) + CloseWidth;
            }
            float RowWidth() const { return LeftWidth() + Between + RightWidth(); }
        };

        GeneratorButtons MeasureGeneratorButtons()
        {
            GeneratorButtons b;
            std::snprintf(b.Again, sizeof(b.Again), ICON_FA_ROTATE "   %s",
                          T(Str::Editor_GenRegenerate));
            std::snprintf(b.Copy, sizeof(b.Copy), ICON_FA_COPY "   %s", T(Str::Editor_Copy));
            std::snprintf(b.Apply, sizeof(b.Apply), ICON_FA_CHECK "   %s", T(Str::Editor_Apply));
            b.CanApply = g_gen.Target >= 0;

            const auto width = [](const char* label) {
                return Max(Px(96.0f), ImGui::CalcTextSize(label, nullptr, true).x +
                                          ImGui::GetStyle().FramePadding.x * 2.0f + Px(8.0f));
            };
            b.AgainWidth = width(b.Again);
            b.CopyWidth = width(b.Copy);
            b.ApplyWidth = b.CanApply ? width(b.Apply) : 0.0f;
            b.CloseWidth = width(T(Str::Editor_Close));
            b.Gap = Px(8.0f);
            b.Between = Px(16.0f);
            return b;
        }

        bool DrawGeneratorBody(Application& app)
        {
            const Fonts::Set& f = Fonts::Current();
            bool accepted = false;

            if (ImGui::RadioButton(T(Str::Editor_GenCharacters), !g_gen.Passphrase))
            {
                g_gen.Passphrase = false;
                Regenerate();
            }
            ImGui::SameLine(0, Px(14));
            if (ImGui::RadioButton(T(Str::Editor_GenWords), g_gen.Passphrase))
            {
                g_gen.Passphrase = true;
                Regenerate();
            }

            ImGui::Dummy(Px(0, 8));
            bool changed = false;

            if (g_gen.Passphrase)
            {
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::SliderInt("##words", &g_gen.Words, 3, 12,
                                            T(Str::Editor_GenWordCount));
                ImGui::Dummy(Px(0, 4));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::TextWrapped(T(Str::Editor_GenWordListNote), PassphraseWordCount());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            else
            {
                int length = static_cast<int>(g_gen.Options.Length);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt("##length", &length, 8, 64, T(Str::Editor_GenLengthCount)))
                {
                    g_gen.Options.Length = static_cast<std::size_t>(length);
                    changed = true;
                }

                ImGui::Dummy(Px(0, 4));
                changed |= ImGui::Checkbox("a-z", &g_gen.Options.UseLower);
                ImGui::SameLine(0, Px(12));
                changed |= ImGui::Checkbox("A-Z", &g_gen.Options.UseUpper);
                ImGui::SameLine(0, Px(12));
                changed |= ImGui::Checkbox("0-9", &g_gen.Options.UseDigits);
                ImGui::SameLine(0, Px(12));
                changed |= ImGui::Checkbox("!#$", &g_gen.Options.UseSymbols);

                changed |= ImGui::Checkbox(T(Str::Editor_GenAvoidAmbiguous),
                                           &g_gen.Options.AvoidAmbiguous);
                changed |= ImGui::Checkbox(T(Str::Editor_GenRequireEveryClass),
                                           &g_gen.Options.RequireEveryClass);
            }

            if (changed || g_gen.Preview.empty())
                Regenerate();

            ImGui::Dummy(Px(0, 10));

            const ImVec2 boxPos = ImGui::GetCursorScreenPos();
            const float boxWidth = ImGui::GetContentRegionAvail().x;
            const float boxHeight = Px(46.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(boxPos, ImVec2(boxPos.x + boxWidth, boxPos.y + boxHeight),
                              Theme::U32(Token::Inset), Theme::Round(Px(6.0f)));
            dl->AddRect(boxPos, ImVec2(boxPos.x + boxWidth, boxPos.y + boxHeight),
                        Theme::U32(Token::Line), Theme::Round(Px(6.0f)));

            if (!g_gen.Problem.empty())
            {
                dl->AddText(f.Text, Px(Fonts::Sizes::Body),
                            ImVec2(boxPos.x + Px(12), boxPos.y + Px(14)),
                            Theme::U32(Token::RedBright), g_gen.Problem.c_str());
            }
            else
            {
                dl->PushClipRect(ImVec2(boxPos.x + Px(10), boxPos.y),
                                 ImVec2(boxPos.x + boxWidth - Px(10), boxPos.y + boxHeight), true);
                dl->AddText(f.Mono, Px(Fonts::Sizes::Body),
                            ImVec2(boxPos.x + Px(12), boxPos.y + Px(14)),
                            Theme::U32(Token::TextPrimary), g_gen.Preview.c_str());
                dl->PopClipRect();
            }
            ImGui::Dummy(ImVec2(boxWidth, boxHeight));

            ImGui::Dummy(Px(0, 6));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            if (g_gen.Problem.empty())
            {
                char line[160];
                ::snprintf(line, sizeof(line), T(Str::Editor_GenEntropyBits), g_gen.EntropyBits);
                ImGui::TextUnformatted(line);
            }
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 10));
            const bool usable = g_gen.Problem.empty() && !g_gen.Preview.empty();

            const GeneratorButtons b = MeasureGeneratorButtons();
            const float gap = b.Gap;
            const float buttonHeight = Px(32.0f);
            const float leftGroup = b.LeftWidth();
            const float rightGroup = b.RightWidth();
            const float rowStartX = ImGui::GetCursorPosX();
            const float avail = ImGui::GetContentRegionAvail().x;
            const bool oneRow = leftGroup + b.Between + rightGroup <= avail;
            const bool canApply = b.CanApply;

            ImGui::BeginDisabled(!usable);
            if (ImGui::Button(b.Again, ImVec2(b.AgainWidth, buttonHeight)))
                Regenerate();
            ImGui::SameLine(0, gap);
            if (ImGui::Button(b.Copy, ImVec2(b.CopyWidth, buttonHeight)))
            {
                const std::uint32_t seconds = Vault::ClipboardSeconds(
                    app.Session().Vault().Doc().VaultSettings);
                app.Clipboard().CopySecret(g_gen.Preview.c_str(), g_gen.Preview.size(),
                                           static_cast<double>(seconds), Application::Now());
                app.Clipboard().SetLastLabel(T(Str::Editor_GeneratedPassword));
            }
            ImGui::EndDisabled();

            if (oneRow)
            {
                ImGui::SameLine(rowStartX + avail - rightGroup, 0.0f);
            }
            else
            {
                ImGui::Dummy(Px(0, 2));
                ImGui::SetCursorPosX(rowStartX + Max(0.0f, avail - rightGroup));
            }
            if (canApply)
            {
                ImGui::BeginDisabled(!usable);
                if (ImGui::Button(b.Apply, ImVec2(b.ApplyWidth, buttonHeight)))
                    accepted = true;
                ImGui::EndDisabled();
                ImGui::SameLine(0, gap);
            }
            if (ImGui::Button(T(Str::Editor_Close), ImVec2(b.CloseWidth, buttonHeight)))
            {
                WipeString(g_gen.Preview);
                ImGui::CloseCurrentPopup();
            }
            return accepted;
        }

        enum class RowAction { None, Delete, MoveUp, MoveDown, Generate };

        RowAction DrawRow(Application& app, Row& row, std::size_t index, std::size_t count)
        {
            const Fonts::Set& f = Fonts::Current();
            RowAction action = RowAction::None;

            ImGui::PushID(static_cast<int>(index));

            const float avail = ImGui::GetContentRegionAvail().x;
            const float rowHeight = ImGui::GetFrameHeight();
            const float keyWidth = Max(Px(110.0f), (avail - Px(kToolStripWidth)) * 0.34f);
            const float valueWidth =
                Max(Px(120.0f), avail - Px(kToolStripWidth) - keyWidth - Px(16.0f));

            if (row.Role == Vault::FieldRole::Custom)
            {
                ImGui::SetNextItemWidth(keyWidth);
                ImGui::InputTextWithHint("##key", T(Str::Editor_FieldNameHint),
                                         row.Key, sizeof(row.Key));
            }
            else
            {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(keyWidth, rowHeight));
                ImGui::GetWindowDrawList()->AddText(
                    f.Text, Px(Fonts::Sizes::Body),
                    ImVec2(at.x + Px(2), at.y + (rowHeight - Px(Fonts::Sizes::Body)) * 0.5f - Px(1.0f)),
                    Theme::U32(Token::TextSecondary), T(FieldRoleText(row.Role)));
            }
            ImGui::SameLine(0, Px(8));

            if (row.Secret && row.Hidden)
            {
                row.Hidden->Draw("##secret", T(Str::Editor_FieldSecretHint), valueWidth, false, rowHeight);
            }
            else
            {
                ImGui::SetNextItemWidth(valueWidth);
                ImGui::InputTextWithHint("##value", T(Str::Editor_FieldValueHint), row.Value, sizeof(row.Value));
            }

            ImGui::SameLine(0, Px(8));

            {
                const bool secret = row.Secret;
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(secret ? Token::AccentCore : Token::TextMuted));
                if (UI::IconButton("###secret",
                                   secret ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN,
                                   ImVec2(Px(kSlotWidth), rowHeight),
                                   secret ? Token::AccentCore : Token::TextMuted,
                                   secret ? T(Str::Editor_SecretTooltip)
                                          : T(Str::Editor_PlainTooltip)))
                {
                    if (!MakeSecret(row, !secret))
                        g_draft.SaveProblem = T(Str::Editor_SecretFieldFailed);
                }

                ImGui::PopStyleColor();
            }

            ImGui::SameLine(0, Px(kSlotGap));
            if (row.Secret)
            {
                UI::IconButton("###reveal", ICON_FA_EYE,
                               ImVec2(Px(kSlotWidth), rowHeight));
                if (row.Hidden)
                    row.Hidden->SetRevealed(ImGui::IsItemActive());

                ImGui::SameLine(0, Px(kSlotGap));
                if (SlotButton(ICON_FA_DICE, "##dice", T(Str::Editor_GeneratePassword)))
                    action = RowAction::Generate;
            }
            else
            {
                EmptySlot();
                ImGui::SameLine(0, Px(kSlotGap));
                EmptySlot();
            }

            ImGui::SameLine(0, Px(kSlotGap));
            ImGui::BeginDisabled(index == 0);
            if (SlotButton(ICON_FA_CHEVRON_UP, "##up", T(Str::Editor_MoveUp)))
                action = RowAction::MoveUp;
            ImGui::EndDisabled();

            ImGui::SameLine(0, Px(kSlotGap));
            ImGui::BeginDisabled(index + 1 >= count);
            if (SlotButton(ICON_FA_CHEVRON_DOWN, "##down", T(Str::Editor_MoveDown)))
                action = RowAction::MoveDown;
            ImGui::EndDisabled();

            ImGui::SameLine(0, Px(kSlotGap));
            if (SlotButton(ICON_FA_TRASH, "##trash", T(Str::Editor_RemoveField), true))
                action = RowAction::Delete;

            (void)app;
            (void)f;
            ImGui::PopID();
            return action;
        }
    }

    namespace
    {
        void DrawTotpSection()
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, 12));
            ImGui::Separator();
            ImGui::Dummy(Px(0, 8));
            Label(T(Str::Editor_SectionTwoFactor));
            ImGui::Dummy(Px(0, 4));

            if (g_draft.HasTotp)
            {
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                char summary[160];
                std::snprintf(summary, sizeof(summary), T(Str::Editor_TotpSummary),
                              AlgorithmName(g_draft.Totp.Algorithm),
                              g_draft.Totp.Digits, g_draft.Totp.Period,
                              g_draft.Totp.Issuer.empty() ? "" : " · ",
                              g_draft.Totp.Issuer.c_str());
                char line[192];
                std::snprintf(line, sizeof(line), ICON_FA_SHIELD_HALVED "  %s", summary);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
                ImGui::PopFont();

                ImGui::Dummy(Px(0, 6));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                char removeTotpLabel[96];
                std::snprintf(removeTotpLabel, sizeof(removeTotpLabel), ICON_FA_TRASH "   %s",
                              T(Str::Editor_RemoveTwoFactor));
                if (ImGui::Button(removeTotpLabel, Px(230, 30)))
                {
                    g_draft.HasTotp = false;
                    g_draft.Totp = TotpConfig{};
                    g_draft.PasteAccepted = false;
                }
                ImGui::PopStyleColor();
                ImGui::Dummy(Px(0, 4));
                return;
            }

            if (!g_draft.Paste)
                g_draft.Paste = NewSecretField(1024);

            const float width = Min(Px(420.0f), ImGui::GetContentRegionAvail().x);
            g_draft.Paste->Draw("##totppaste", T(Str::Editor_TotpPasteHint),
                                width, false, ImGui::GetFrameHeight());

            ImGui::SameLine(0, Px(8));
            char applyLabel[64];
            std::snprintf(applyLabel, sizeof(applyLabel), ICON_FA_CHECK "   %s",
                          T(Str::Editor_Apply));
            if (ImGui::Button(applyLabel, ImVec2(Px(150), ImGui::GetFrameHeight())))
            {
                g_draft.PasteProblem.clear();
                g_draft.PasteAccepted = false;
                auto parsed = ParsePastedSecret(
                    std::string_view(g_draft.Paste->Data(), g_draft.Paste->Length()));
                if (parsed.IsOk())
                {
                    g_draft.Totp = std::move(parsed.Value());
                    g_draft.HasTotp = true;
                    g_draft.PasteAccepted = true;
                    g_draft.Paste->Clear();
                }
                else
                {
                    g_draft.PasteProblem = parsed.GetError().Detail
                                               ? parsed.GetError().Detail
                                               : T(Str::Editor_TotpPasteUnusable);
                }
            }

            ImGui::Dummy(Px(0, 4));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            if (!g_draft.PasteProblem.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                                   g_draft.PasteProblem.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                ImGui::TextWrapped("%s", T(Str::Editor_TotpPasteNote));
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 4));

            ImGui::Dummy(Px(0, 12));
            Label(T(Str::Editor_SecondFactorSection));
            ImGui::Dummy(Px(0, 2));

            struct Choice { std::uint32_t Value; Str Text; };
            const Choice choices[] = {
                { Vault::SecondFactorUnknown,    Str::Editor_SecondFactorUnknown },
                { Vault::SecondFactorNotOffered, Str::Editor_SecondFactorNone },
                { Vault::SecondFactorElsewhere,  Str::Editor_SecondFactorOther },
            };

            for (const Choice& c : choices)
            {
                const bool on = g_draft.SecondFactorState == c.Value;
                if (ImGui::RadioButton(T(c.Text), on))
                    g_draft.SecondFactorState = c.Value;
            }

            ImGui::Dummy(Px(0, 4));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::Editor_SecondFactorNote));
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 4));
        }

        void DrawRecoveryCodeSection()
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, 8));
            Label(T(Str::Editor_SectionRecoveryCodes));
            ImGui::Dummy(Px(0, 4));

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##codes", g_draft.NewCodes, sizeof(g_draft.NewCodes),
                                      Px(0, 70));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::Editor_RecoveryCodesNote));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }

    namespace
    {
        std::uint32_t DefaultComposeKind()
        {
            const Vault::ListKindOffer offer = Vault::ListKindsFor(g_draft.PlatformId);
            return offer.Count != 0 ? offer.Kinds[0] : Vault::kListKindOther;
        }

        bool TakeTrimmed(const SecretField& field, SecureString& into, bool& lost)
        {
            lost = false;
            const char* data = field.Data();
            std::size_t begin = 0;
            std::size_t end = field.Length();
            while (begin < end && (data[begin] == ' ' || data[begin] == '\t'))
                ++begin;
            while (end > begin && (data[end - 1] == ' ' || data[end - 1] == '\t'))
                --end;
            if (begin == end)
                return false;
            SecureString value;
            value.Assign(data + begin, end - begin);
            if (value.Size() != end - begin)
            {
                lost = true;
                return false;
            }
            into = std::move(value);
            return true;
        }

        bool CommitComposer()
        {
            if (!g_draft.ComposeSecret)
                return false;
            if (g_draft.Items.size() >= Vault::kMaxListItemsPerEntry)
            {
                g_draft.SaveProblem = T(Str::List_Full);
                return false;
            }
            ItemRow row;
            bool lost = false;
            if (!TakeTrimmed(*g_draft.ComposeSecret, row.Secret, lost))
            {
                if (lost)
                    g_draft.SaveProblem = T(Str::Editor_ValueNotStored);
                return false;
            }
            row.Kind = g_draft.ComposeKind;
            CopyInto(row.Label, sizeof(row.Label), g_draft.ComposeLabel);
            row.AddedUtc = Vault::SystemUtcNow();
            g_draft.Items.push_back(std::move(row));

            sodium_memzero(g_draft.ComposeLabel, sizeof(g_draft.ComposeLabel));
            g_draft.ComposeSecret->Clear();
            g_draft.ComposeFocusLabel = true;
            return true;
        }

        bool ApplyReplace()
        {
            if (g_draft.ReplaceIndex >= g_draft.Items.size() || !g_draft.ReplaceSecret)
                return false;
            bool lost = false;
            SecureString next;
            if (!TakeTrimmed(*g_draft.ReplaceSecret, next, lost))
            {
                if (lost)
                    g_draft.SaveProblem = T(Str::Editor_ValueNotStored);
                return false;
            }
            g_draft.Items[g_draft.ReplaceIndex].Secret = std::move(next);
            g_draft.ReplaceSecret->Clear();
            g_draft.ReplaceIndex = kNoItem;
            return true;
        }

        void CloseReplace()
        {
            if (g_draft.ReplaceSecret)
                g_draft.ReplaceSecret->Clear();
            g_draft.ReplaceIndex = kNoItem;
        }

        void KindHeading(std::uint32_t kind, std::size_t count)
        {
            char heading[128];
            if (Vault::IsKnownListKind(kind))
                std::snprintf(heading, sizeof(heading),
                              T(Vault::ListKindTextsFor(kind).Section), count);
            else
                std::snprintf(heading, sizeof(heading), T(Str::List_SectionUnknown),
                              static_cast<unsigned>(kind), count);
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::TextUnformatted(heading);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        enum class ItemAction { None, Delete, MoveUp, MoveDown };

        ItemAction DrawItemRow(std::size_t index, bool hasPrev, bool hasNext)
        {
            const Fonts::Set& f = Fonts::Current();
            ItemRow& row = g_draft.Items[index];
            const Vault::ListKindTexts& texts = Vault::ListKindTextsFor(row.Kind);
            ItemAction action = ItemAction::None;
            ImGui::PushID(static_cast<int>(index));

            const float slots = 6.0f * Px(kSlotWidth) + 6.0f * Px(kSlotGap);
            const float avail = ImGui::GetContentRegionAvail().x;
            const float labelWidth = Max(Px(80.0f), (avail - slots) * 0.5f);
            const float valueWidth = Max(Px(80.0f), avail - slots - labelWidth - Px(8.0f));

            ImGui::SetNextItemWidth(labelWidth);
            ImGui::InputTextWithHint("##label", T(texts.LabelHint), row.Label, sizeof(row.Label));
            ImGui::SameLine(0, Px(8));

            const bool replacing = g_draft.ReplaceIndex == index && g_draft.ReplaceSecret;
            if (replacing)
            {
                if (g_draft.ReplaceSecret->Draw("##replace", T(texts.SecretHint), valueWidth,
                                                false, ImGui::GetFrameHeight()))
                    ApplyReplace();
                ImGui::SameLine(0, Px(kSlotGap));
                EmptySlot();
            }
            else
            {
                const bool holding = g_draft.RevealItem == index;
                const bool revealed = holding &&
                    (ImGui::GetTime() - g_draft.RevealStarted) < 10.0;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(valueWidth, ImGui::GetFrameHeight()));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float textY = at.y + (ImGui::GetFrameHeight() - Px(Fonts::Sizes::Body)) * 0.5f;
                dl->PushClipRect(at, ImVec2(at.x + valueWidth, at.y + ImGui::GetFrameHeight()), true);
                if (revealed)
                {
                    const std::string_view v = row.Secret.View();
                    dl->AddText(f.Mono, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(4), textY),
                                Theme::U32(Token::WarnBright), v.data(), v.data() + v.size());
                }
                else
                {
                    dl->AddText(f.Mono, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(4), textY),
                                Theme::U32(Token::TextPrimary), "••••••••••••");
                }
                dl->PopClipRect();

                ImGui::SameLine(0, Px(kSlotGap));
                UI::IconButton("###itemreveal", ICON_FA_EYE,
                               ImVec2(Px(kSlotWidth), ImGui::GetFrameHeight()),
                               revealed ? Token::WarnBright : Token::TextMuted);
                if (ImGui::IsItemActive())
                {
                    if (!holding)
                    {
                        g_draft.RevealItem = index;
                        g_draft.RevealStarted = ImGui::GetTime();
                    }
                }
                else if (holding)
                {
                    g_draft.RevealItem = kNoItem;
                }
            }

            ImGui::SameLine(0, Px(kSlotGap));
            if (replacing)
            {
                if (SlotButton(ICON_FA_CHECK, "##apply", T(Str::Editor_Apply)))
                    ApplyReplace();
                ImGui::SameLine(0, Px(kSlotGap));
                if (SlotButton(ICON_FA_XMARK, "##cancelreplace", T(Str::Editor_Cancel)))
                    CloseReplace();
            }
            else
            {
                if (SlotButton(ICON_FA_PEN, "##replace", T(Str::List_ReplaceValue)))
                {
                    if (!g_draft.ReplaceSecret)
                        g_draft.ReplaceSecret = NewSecretField(Vault::kMaxListSecretBytes + 1);
                    g_draft.ReplaceSecret->Clear();
                    g_draft.ReplaceSecret->SetFocus(true);
                    g_draft.ReplaceIndex = index;
                    g_draft.RevealItem = kNoItem;
                }
                ImGui::SameLine(0, Px(kSlotGap));
                EmptySlot();
            }

            ImGui::SameLine(0, Px(kSlotGap));
            ImGui::BeginDisabled(!hasPrev);
            if (SlotButton(ICON_FA_CHEVRON_UP, "##up", T(Str::Editor_MoveUp)))
                action = ItemAction::MoveUp;
            ImGui::EndDisabled();
            ImGui::SameLine(0, Px(kSlotGap));
            ImGui::BeginDisabled(!hasNext);
            if (SlotButton(ICON_FA_CHEVRON_DOWN, "##down", T(Str::Editor_MoveDown)))
                action = ItemAction::MoveDown;
            ImGui::EndDisabled();
            ImGui::SameLine(0, Px(kSlotGap));
            if (SlotButton(ICON_FA_TRASH, "##trash", T(Str::List_RemoveItem), true))
                action = ItemAction::Delete;

            ImGui::PopID();
            return action;
        }

        void DrawListsSection()
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, 12));
            ImGui::Separator();
            ImGui::Dummy(Px(0, 8));
            Label(T(Str::List_EditorSection));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::List_EditorNote));
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 6));

            if (!g_draft.ComposeKindChosen)
                g_draft.ComposeKind = DefaultComposeKind();

            ImGui::PushID("##listitems");
            std::size_t deleteAt = kNoItem;
            std::size_t swapA = kNoItem;
            std::size_t swapB = kNoItem;

            auto drawKind = [&](std::uint32_t kind) {
                std::size_t count = 0;
                for (const ItemRow& row : g_draft.Items)
                    if (row.Kind == kind)
                        ++count;
                if (count == 0)
                    return;
                KindHeading(kind, count);
                std::size_t prev = kNoItem;
                for (std::size_t i = 0; i < g_draft.Items.size(); ++i)
                {
                    if (g_draft.Items[i].Kind != kind)
                        continue;
                    std::size_t next = kNoItem;
                    for (std::size_t k = i + 1; k < g_draft.Items.size(); ++k)
                        if (g_draft.Items[k].Kind == kind)
                        {
                            next = k;
                            break;
                        }
                    const ItemAction a = DrawItemRow(i, prev != kNoItem, next != kNoItem);
                    if (a == ItemAction::Delete)
                        deleteAt = i;
                    else if (a == ItemAction::MoveUp && prev != kNoItem)
                    {
                        swapA = prev;
                        swapB = i;
                    }
                    else if (a == ItemAction::MoveDown && next != kNoItem)
                    {
                        swapA = i;
                        swapB = next;
                    }
                    prev = i;
                }
                ImGui::Dummy(Px(0, 6));
            };
            for (const std::uint32_t kind : Vault::kListKindOrder)
                drawKind(kind);
            {
                std::uint32_t last = 0;
                bool any = false;
                for (;;)
                {
                    bool found = false;
                    std::uint32_t next = 0;
                    for (const ItemRow& row : g_draft.Items)
                    {
                        if (Vault::IsKnownListKind(row.Kind) || (any && row.Kind <= last))
                            continue;
                        if (!found || row.Kind < next)
                        {
                            next = row.Kind;
                            found = true;
                        }
                    }
                    if (!found)
                        break;
                    drawKind(next);
                    last = next;
                    any = true;
                }
            }
            ImGui::PopID();

            if (deleteAt < g_draft.Items.size())
            {
                g_draft.Items.erase(g_draft.Items.begin() + static_cast<std::ptrdiff_t>(deleteAt));
                g_draft.RevealItem = kNoItem;
                CloseReplace();
            }
            else if (swapA < g_draft.Items.size() && swapB < g_draft.Items.size())
            {
                std::swap(g_draft.Items[swapA], g_draft.Items[swapB]);
                g_draft.RevealItem = kNoItem;
                CloseReplace();
            }

            if (!g_draft.ComposeSecret)
                g_draft.ComposeSecret = NewSecretField(Vault::kMaxListSecretBytes + 1);
            const Vault::ListKindTexts& texts = Vault::ListKindTextsFor(g_draft.ComposeKind);
            const bool full = g_draft.Items.size() >= Vault::kMaxListItemsPerEntry;

            ImGui::BeginDisabled(full);
            ImGui::SetNextItemWidth(Px(170.0f));
            if (ImGui::BeginCombo("##composekind", T(texts.Name)))
            {
                const Vault::ListKindOffer offer = Vault::ListKindsFor(g_draft.PlatformId);
                auto choose = [](std::uint32_t kind) {
                    if (ImGui::Selectable(T(Vault::ListKindTextsFor(kind).Name),
                                          g_draft.ComposeKind == kind))
                    {
                        g_draft.ComposeKind = kind;
                        g_draft.ComposeKindChosen = true;
                    }
                };
                if (offer.Count != 0)
                {
                    ImGui::TextDisabled("%s", T(Str::List_Suggested));
                    for (std::size_t i = 0; i < offer.Count; ++i)
                    {
                        ImGui::PushID(static_cast<int>(100 + i));
                        choose(offer.Kinds[i]);
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                }
                for (const std::uint32_t kind : Vault::kListKindOrder)
                {
                    bool offered = false;
                    for (std::size_t i = 0; i < offer.Count; ++i)
                        if (offer.Kinds[i] == kind)
                            offered = true;
                    if (offered)
                        continue;
                    ImGui::PushID(static_cast<int>(kind));
                    choose(kind);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine(0, Px(8));
            const float buttonWidth = Px(150.0f);
            const float dice = texts.OffersGenerator ? Px(kSlotWidth) + Px(kSlotGap) : 0.0f;
            const float rest = Max(Px(160.0f),
                                   ImGui::GetContentRegionAvail().x - buttonWidth - dice - Px(16.0f));
            if (g_draft.ComposeFocusLabel)
            {
                ImGui::SetKeyboardFocusHere();
                g_draft.ComposeFocusLabel = false;
            }
            ImGui::SetNextItemWidth(rest * 0.5f);
            ImGui::InputTextWithHint("##composelabel", T(texts.LabelHint),
                                     g_draft.ComposeLabel, sizeof(g_draft.ComposeLabel));
            ImGui::SameLine(0, Px(8));
            const bool entered = g_draft.ComposeSecret->Draw(
                "##composesecret", T(texts.SecretHint), rest * 0.5f - Px(8.0f), false,
                ImGui::GetFrameHeight());
            if (texts.OffersGenerator)
            {
                ImGui::SameLine(0, Px(kSlotGap));
                if (SlotButton(ICON_FA_DICE, "##composedice", T(Str::Editor_GeneratePassword)))
                {
                    g_gen.Target = kGenTargetListComposer;
                    WipeString(g_gen.Preview);
                    g_gen.PendingOpen = true;
                }
            }
            ImGui::SameLine(0, Px(8));
            char addLabel[96];
            std::snprintf(addLabel, sizeof(addLabel), ICON_FA_CIRCLE_PLUS "   %s",
                          T(Str::List_Add));
            const bool canAdd = g_draft.ComposeSecret->Length() != 0;
            ImGui::BeginDisabled(!canAdd);
            if (ImGui::Button(addLabel, ImVec2(buttonWidth, ImGui::GetFrameHeight())) ||
                (entered && canAdd))
                CommitComposer();
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            if (full)
            {
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                ImGui::TextWrapped("%s", T(Str::List_Full));
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }

        bool ItemsDiffer(const std::vector<Vault::ListItem>& stored)
        {
            if (stored.size() != g_draft.Items.size())
                return true;
            for (std::size_t i = 0; i < stored.size(); ++i)
            {
                const ItemRow& row = g_draft.Items[i];
                const Vault::ListItem& item = stored[i];
                if (row.Kind != item.Kind || std::string_view(row.Label) != item.Label ||
                    row.AddedUtc != item.AddedUtc || row.Unknown.size() != item.Unknown.size())
                    return true;
                for (std::size_t k = 0; k < row.Unknown.size(); ++k)
                    if (row.Unknown[k].Tag != item.Unknown[k].Tag ||
                        row.Unknown[k].Payload != item.Unknown[k].Payload)
                        return true;
                if (!(row.Secret == item.Secret))
                    return true;
            }
            return false;
        }
    }

    bool IsOpen() { return g_draft.Open; }
    std::uint64_t EditingId() { return g_draft.Id; }

    void Close()
    {
        ForgetLeave();
        ResetDraft();
    }

    namespace { bool g_discardedByLock = false; }

    void AbandonForLock()
    {
        if (g_draft.Open)
            g_discardedByLock = true;
        ForgetLeave();
        ResetDraft();
    }

    namespace
    {
        std::uint64_t g_showTarget = 0;
    }

    std::uint64_t TakeShowTarget()
    {
        const std::uint64_t id = g_showTarget;
        g_showTarget = 0;
        return id;
    }

    bool TakeDiscardedNotice()
    {
        const bool was = g_discardedByLock;
        g_discardedByLock = false;
        return was;
    }

    namespace
    {
        void InsertTemplate()
        {
            const Vault::FieldTemplate plan = Vault::TemplateFor(g_draft.PlatformId);
            for (std::size_t i = 0; i < plan.Count; ++i)
            {
                const Vault::TemplateField& want = plan.Fields[i];
                bool present = false;
                for (const Row& row : g_draft.Rows)
                    if (row.Role == want.Role)
                        present = true;
                if (present || g_draft.Rows.size() >= Vault::kMaxFieldsPerEntry)
                    continue;

                Row row;
                row.Role = want.Role;
                CopyInto(row.Key, sizeof(row.Key), Vault::CanonicalFieldName(want.Role));
                if (want.Secret)
                    (void)MakeSecret(row, true);
                g_draft.Rows.push_back(std::move(row));
            }
        }
    }

    void BeginNew(std::string folder)
    {
        ResetDraft();
        g_draft.Open = true;
        g_draft.Id = 0;
        g_draft.FocusTitle = true;

        InsertTemplate();
        g_draft.Folder = std::move(folder);
    }

    void BeginEdit(const Entry& entry)
    {
        ResetDraft();
        g_draft.Open = true;
        g_draft.Id = entry.Id;
        g_draft.FocusTitle = true;
        CopyInto(g_draft.Title, sizeof(g_draft.Title), entry.Title);
        CopyInto(g_draft.PlatformId, sizeof(g_draft.PlatformId), entry.PlatformId);
        CopyInto(g_draft.Symbol, sizeof(g_draft.Symbol), entry.Symbol);
        g_draft.Folder = entry.Folder;
        g_draft.LoadedFolder = entry.Folder;
        g_draft.Accent = entry.Accent;
        for (const Field& f : entry.Fields)
            g_draft.Rows.push_back(RowFrom(f));
        g_draft.SecondFactorState = entry.SecondFactorState;
        g_draft.Items.reserve(entry.ListItems.size());
        for (const Vault::ListItem& item : entry.ListItems)
        {
            ItemRow row;
            row.Kind = item.Kind;
            if (!CopyInto(row.Label, sizeof(row.Label), item.Label))
                g_draft.LoadIncomplete = true;
            row.Secret = item.Secret;
            if (row.Secret.Size() != item.Secret.Size())
                g_draft.LoadIncomplete = true;
            row.AddedUtc = item.AddedUtc;
            row.Unknown = item.Unknown;
            g_draft.Items.push_back(std::move(row));
        }
        g_draft.HasTotp = entry.HasTotp;
        if (entry.HasTotp)
        {
            g_draft.Totp = entry.Totp.Config;
            if (g_draft.Totp.SecretBase32.Size() != entry.Totp.Config.SecretBase32.Size())
                g_draft.LoadIncomplete = true;
        }
    }

    void OpenStandaloneGenerator()
    {
        g_gen.Target = -1;
        WipeString(g_gen.Preview);
        g_gen.PendingOpen = true;
    }

    namespace
    {
        bool ContainsFolded(const char* haystack, const char* needle)
        {
            if (!*needle)
                return true;
            for (const char* at = haystack; *at != '\0'; ++at)
            {
                const char* a = at;
                const char* b = needle;
                while (*a != '\0' && *b != '\0')
                {
                    const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
                    const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
                    if (ca != cb)
                        break;
                    ++a;
                    ++b;
                }
                if (*b == '\0')
                    return true;
            }
            return false;
        }

        void DrawIconPicker()
        {
            const Fonts::Set& f = Fonts::Current();

            ImGui::SetNextWindowSize(Px(620, 540), ImGuiCond_Appearing);
            if (!UI::BeginTitledModal(T(Str::Editor_IconPickerTitle), kIconPopup,
                                        ImGuiWindowFlags_NoSavedSettings))
            {
                return;
            }

            {
                const float half = (ImGui::GetContentRegionAvail().x - Px(6.0f)) * 0.5f;
                const auto tab = [half](const char* label, bool on) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                                          on ? Theme::Color(Token::AccentCore, 0.22f)
                                             : Theme::Color(Token::Elevated, 0.55f));
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(on ? Token::TextPrimary
                                                                         : Token::TextSecondary));
                    const bool hit = ImGui::Button(label, ImVec2(half, Px(30.0f)));
                    ImGui::PopStyleColor(2);
                    return hit;
                };
                if (tab(T(Str::Editor_IconTabSymbols), !g_iconBrands))
                    g_iconBrands = false;
                ImGui::SameLine(0, Px(6));
                if (tab(T(Str::Editor_IconTabBrands), g_iconBrands))
                    g_iconBrands = true;
            }
            ImGui::Dummy(Px(0, 4));

            ImGui::SetNextItemWidth(-1);
            char hint[96];
            std::snprintf(hint, sizeof(hint), ICON_FA_MAGNIFYING_GLASS "  %s",
                          T(Str::Editor_IconSearchHint));
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##iconsearch", hint, g_iconSearch,
                                     sizeof(g_iconSearch));

            std::vector<std::size_t> shown;
            if (g_iconBrands)
            {
                shown.reserve(kFaBrandCount);
                for (std::size_t i = 0; i < kFaBrandCount; ++i)
                    if (ContainsFolded(kFaBrands[i].name, g_iconSearch))
                        shown.push_back(i);
            }
            else
            {
                const std::string folded = Vault::FoldForSearch(g_iconSearch);
                shown.reserve(kFaSymbolCount);
                for (std::size_t i = 0; i < kFaSymbolCount; ++i)
                    if (Vault::FoldedContains(kFaSymbols[i].name, folded) ||
                        Vault::FoldedContains(kFaSymbols[i].terms, folded))
                        shown.push_back(i);
            }

            ImGui::Dummy(Px(0, 4));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            if (g_iconBrands)
                ImGui::Text(T(Str::Editor_IconMatches), shown.size(), kFaBrandCount);
            else
                ImGui::Text(T(Str::Editor_SymbolMatches), shown.size(), kFaSymbolCount);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 4));

            const float tile = Px(g_iconBrands ? 84.0f : 64.0f);
            const float icon = Px(g_iconBrands ? 30.0f : 34.0f);
            const float rowHeight = tile - Px(4.0f) + ImGui::GetStyle().ItemSpacing.y;
            bool chosen = false;

            const float footer = ImGui::GetStyle().ItemSpacing.y * 2.0f +
                                 Px(4.0f) + Px(32.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##icongrid", ImVec2(0.0f, -footer),
                              ImGuiChildFlags_None);
            if (shown.empty())
            {
                ImGui::Dummy(Px(0, 20));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                ImGui::TextWrapped("%s", T(Str::Editor_IconNone));
                ImGui::PopStyleColor();
            }
            else
            {
                const int columns =
                    (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / tile));
                const int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;

                ImGuiListClipper clipper;
                clipper.Begin(rows, rowHeight);
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        for (int col = 0; col < columns; ++col)
                        {
                            const std::size_t at =
                                static_cast<std::size_t>(row) * columns + col;
                            if (at >= shown.size())
                                break;

                            if (col != 0)
                                ImGui::SameLine(0, 0);
                            ImGui::PushID(static_cast<int>(shown[at]));

                            const ImVec2 origin = ImGui::GetCursorScreenPos();
                            const ImVec2 box(tile, tile - Px(4));
                            const ImVec2 boxMax(origin.x + box.x, origin.y + box.y);
                            const bool clicked = ImGui::InvisibleButton("##tile", box);
                            const bool hovered = ImGui::IsItemHovered();
                            ImDrawList* dl = ImGui::GetWindowDrawList();

                            const bool current =
                                g_iconBrands
                                    ? (g_draft.Symbol[0] == 0 &&
                                       std::strcmp(g_draft.PlatformId, kFaBrands[shown[at]].name) == 0)
                                    : std::strcmp(g_draft.Symbol, kFaSymbols[shown[at]].name) == 0;
                            if (hovered || current)
                                dl->AddRectFilled(origin, boxMax, Theme::U32(Token::Elevated, 0.7f),
                                                  Theme::Round(Px(6.0f)));
                            if (current)
                                dl->AddRect(origin, boxMax, Theme::U32(Token::AccentCore),
                                            Theme::Round(Px(6.0f)), 0, Px(1.5f));

                            if (g_iconBrands)
                            {
                                const FaBrand& brand = kFaBrands[shown[at]];
                                if (clicked)
                                {
                                    CopyInto(g_draft.PlatformId, sizeof(g_draft.PlatformId),
                                             brand.name);
                                    g_draft.Symbol[0] = 0;
                                    chosen = true;
                                }

                                IconSpec cell;
                                cell.BrandName = brand.name;
                                DrawPlatformIconAt(dl,
                                                   ImVec2(origin.x + (tile - icon) * 0.5f,
                                                          origin.y + Px(10.0f)),
                                                   icon, cell);

                                const float wrap = tile - Px(8.0f);
                                const ImVec2 size = f.Text->CalcTextSizeA(
                                    Px(Fonts::Sizes::Label), FLT_MAX, wrap, brand.name);
                                dl->PushClipRect(ImVec2(origin.x + Px(4), origin.y + icon + Px(14)),
                                                 ImVec2(origin.x + tile - Px(4), origin.y + tile - Px(6)),
                                                 true);
                                dl->AddText(f.Text, Px(Fonts::Sizes::Label),
                                            ImVec2(origin.x + (tile - (std::min)(size.x, wrap)) * 0.5f,
                                                   origin.y + icon + Px(16.0f)),
                                            Theme::U32(hovered ? Token::TextPrimary
                                                               : Token::TextMuted),
                                            brand.name);
                                dl->PopClipRect();
                            }
                            else
                            {
                                const FaSymbol& symbol = kFaSymbols[shown[at]];
                                if (clicked)
                                {
                                    CopyInto(g_draft.Symbol, sizeof(g_draft.Symbol), symbol.name);
                                    chosen = true;
                                }

                                IconSpec cell;
                                cell.Symbol = symbol.name;
                                DrawPlatformIconAt(dl,
                                                   ImVec2(origin.x + (tile - icon) * 0.5f,
                                                          origin.y + (box.y - icon) * 0.5f),
                                                   icon, cell);
                            }

                            ImGui::PopID();
                        }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Dummy(Px(0, 4));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
            if (ImGui::Button(T(Str::Editor_IconClear), Px(150, 32)))
            {
                if (g_iconBrands)
                    g_draft.PlatformId[0] = 0;
                else
                    g_draft.Symbol[0] = 0;
                chosen = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0, Px(8));
            char closeLabel[96];
            std::snprintf(closeLabel, sizeof(closeLabel), ICON_FA_XMARK "   %s",
                          T(Str::Editor_Close));
            if (ImGui::Button(closeLabel, Px(150, 32)) || chosen)
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    namespace
    {
        enum class SaveOutcome
        {
            Saved,
            Refused,
            Partly,
        };

        SaveOutcome SaveDraft(VaultSession& session)
        {
            Vault::Document& doc = session.Vault().Doc();
            const std::uint64_t now = Vault::SystemUtcNow();

            if (g_draft.ComposeSecret && g_draft.ComposeSecret->Length() != 0 &&
                !CommitComposer())
            {
                if (g_draft.SaveProblem.empty())
                    g_draft.SaveProblem = T(Str::List_ComposerNoValue);
                return SaveOutcome::Refused;
            }
            if (g_draft.ComposeLabel[0] != '\0')
            {
                g_draft.SaveProblem = T(Str::List_ComposerNoValue);
                return SaveOutcome::Refused;
            }
            if (g_draft.ReplaceSecret && g_draft.ReplaceSecret->Length() != 0 &&
                !ApplyReplace())
            {
                if (g_draft.SaveProblem.empty())
                    g_draft.SaveProblem = T(Str::Editor_ValueNotStored);
                return SaveOutcome::Refused;
            }
            CloseReplace();

            Entry candidate;
            bool assembled = Assemble(candidate);

            std::vector<Vault::ListItem> items;
            items.reserve(g_draft.Items.size());
            for (const ItemRow& row : g_draft.Items)
            {
                Vault::ListItem item;
                item.Kind = row.Kind;
                item.Label = row.Label;
                item.Secret = row.Secret;
                if (item.Secret.Size() != row.Secret.Size())
                    assembled = false;
                item.AddedUtc = row.AddedUtc;
                item.Unknown = row.Unknown;
                items.push_back(std::move(item));
            }
            if (g_draft.Id == 0)
            {
                candidate.ListItems = std::move(items);
            }
            else if (assembled)
            {
                const Status lists = Vault::ValidateListItems(items);
                if (!lists.IsOk())
                {
                    g_draft.SaveProblem = lists.GetError().Detail
                                              ? lists.GetError().Detail
                                              : T(Str::Editor_SaveFailed);
                    return SaveOutcome::Refused;
                }
            }

            Vault::Normalise(candidate);

            candidate.HasTotp = g_draft.HasTotp;
            if (g_draft.HasTotp)
            {
                candidate.Totp.Config = g_draft.Totp;
                if (candidate.Totp.Config.SecretBase32.Size() !=
                    g_draft.Totp.SecretBase32.Size())
                    assembled = false;
            }

            std::vector<std::string> newCodes;
            {
                std::string line;
                const char* p = g_draft.NewCodes;
                for (;; ++p)
                {
                    if (*p == 0 || *p == '\n' || *p == '\r')
                    {
                        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
                            line.pop_back();
                        std::size_t begin = 0;
                        while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t'))
                            ++begin;
                        if (begin < line.size())
                            newCodes.push_back(line.substr(begin));
                        line.clear();
                        if (*p == 0)
                            break;
                        continue;
                    }
                    line.push_back(*p);
                }
            }

            Status result = assembled
                ? Status{}
                : Status{ Error{ ErrorCode::OutOfMemory, "value could not be stored" } };
            const bool wasNew = g_draft.Id == 0;
            bool applied = false;
            if (result.IsOk() && wasNew)
            {
                const auto added = Vault::AddEntry(doc, std::move(candidate), now);
                result = added.IsOk() ? Status{} : Status{ added.GetError() };
                if (added.IsOk())
                {
                    g_draft.Id = added.Value();
                    applied = true;
                }
            }
            else if (result.IsOk())
            {
                result = Vault::UpdateEntry(doc, g_draft.Id, std::move(candidate), now);
                applied = result.IsOk();
            }

            if (result.IsOk() && g_draft.Id != 0)
            {
                result = g_draft.HasTotp
                             ? Vault::SetTotp(doc, g_draft.Id, g_draft.Totp, now)
                             : Vault::ClearTotp(doc, g_draft.Id, now);
            }
            if (result.IsOk() && !newCodes.empty() && g_draft.Id != 0)
                result = Vault::AddRecoveryCodes(doc, g_draft.Id, newCodes, now);
            if (result.IsOk() && g_draft.Id != 0 &&
                (wasNew || g_draft.Folder != g_draft.LoadedFolder))
            {
                result = Vault::SetEntryFolder(doc, g_draft.Id, g_draft.Folder, now);
                if (result.IsOk())
                    g_draft.LoadedFolder = g_draft.Folder;
            }
            if (result.IsOk() && g_draft.Id != 0)
                result = Vault::SetAccent(doc, g_draft.Id, g_draft.Accent, now);
            if (result.IsOk() && g_draft.Id != 0)
                result = Vault::SetSymbol(doc, g_draft.Id, g_draft.Symbol, now);
            if (result.IsOk() && g_draft.Id != 0)
                result = Vault::SetSecondFactorState(doc, g_draft.Id,
                                                     g_draft.SecondFactorState, now);
            if (result.IsOk() && !wasNew)
                result = Vault::SetListItems(doc, g_draft.Id, std::move(items), now);

            if (result.IsOk())
            {
                ResetDraft();
                return SaveOutcome::Saved;
            }
            const char* detail = result.GetError().Detail ? result.GetError().Detail
                                                          : T(Str::Editor_SaveFailed);
            if (!applied)
            {
                g_draft.SaveProblem = result.Code() == ErrorCode::OutOfMemory
                                          ? T(Str::Editor_ValueNotStored)
                                          : detail;
                return SaveOutcome::Refused;
            }
            char partly[512];
            std::snprintf(partly, sizeof(partly), T(Str::Editor_SavedPartly), detail);
            g_draft.SaveProblem = partly;
            return SaveOutcome::Partly;
        }

        bool SecretRowDiffers(const Row& row, const Field& f)
        {
            const std::string_view typed = row.Hidden
                ? std::string_view(row.Hidden->Data(), row.Hidden->Length())
                : std::string_view();
            return !(f.Value == typed);
        }
    }

    bool HasUnsavedChanges(const Vault::Document& doc)
    {
        if (!g_draft.Open)
            return false;

        if (g_draft.Paste && g_draft.Paste->Length() != 0)
            return true;
        if (g_draft.NewCodes[0] != '\0')
            return true;
        if (g_draft.ComposeLabel[0] != '\0' ||
            (g_draft.ComposeSecret && g_draft.ComposeSecret->Length() != 0) ||
            (g_draft.ReplaceSecret && g_draft.ReplaceSecret->Length() != 0))
            return true;

        if (g_draft.Id == 0)
        {
            if (g_draft.Title[0] || g_draft.PlatformId[0] || g_draft.Symbol[0] || g_draft.HasTotp ||
                g_draft.Accent != 0 || g_draft.SecondFactorState != 0 ||
                !g_draft.Items.empty())
                return true;
            for (const Row& row : g_draft.Rows)
            {
                const bool hasValue = row.Secret
                    ? (row.Hidden && row.Hidden->Length() != 0)
                    : row.Value[0] != '\0';
                const bool namedCustom = row.Role == Vault::FieldRole::Custom && row.Key[0];
                if (hasValue || namedCustom)
                    return true;
            }
            return false;
        }

        const Entry* stored = Vault::FindEntry(doc, g_draft.Id);
        if (!stored)
            return false;

        if (std::string_view(g_draft.Title) != stored->Title ||
            std::string_view(g_draft.PlatformId) != stored->PlatformId ||
            std::string_view(g_draft.Symbol) != stored->Symbol ||
            g_draft.Folder != g_draft.LoadedFolder || g_draft.Accent != stored->Accent ||
            g_draft.SecondFactorState != stored->SecondFactorState ||
            g_draft.HasTotp != stored->HasTotp)
            return true;

        if (g_draft.HasTotp)
        {
            const TotpConfig& a = g_draft.Totp;
            const TotpConfig& b = stored->Totp.Config;
            if (!(a.SecretBase32 == b.SecretBase32) || a.Digits != b.Digits ||
                a.Period != b.Period || a.Algorithm != b.Algorithm ||
                a.Issuer != b.Issuer || a.AccountName != b.AccountName)
                return true;
        }

        if (ItemsDiffer(stored->ListItems))
            return true;

        if (g_draft.Rows.size() != stored->Fields.size())
            return true;
        for (std::size_t i = 0; i < g_draft.Rows.size(); ++i)
        {
            const Row& row = g_draft.Rows[i];
            const Field& f = stored->Fields[i];
            if (std::string_view(row.Key) != f.Key || row.Role != f.Role ||
                row.Secret != f.IsSecret())
                return true;
            if (row.Secret ? SecretRowDiffers(row, f)
                           : !(f.Value == std::string_view(row.Value)))
                return true;
        }
        return false;
    }

    void RequestClose(const Vault::Document& doc, std::function<void()> then)
    {
        if (g_draft.Open && HasUnsavedChanges(doc))
        {
            g_afterLeave = std::move(then);
            g_leavePending = true;
            g_leaveAsking = true;
            return;
        }
        if (g_draft.Open)
        {
            ForgetLeave();
            ResetDraft();
        }
        if (then)
            then();
    }

    bool DrawPopups(Application& app, VaultSession& session)
    {
        const Fonts::Set& f = Fonts::Current();
        bool documentChanged = false;

        if (g_gen.PendingOpen)
        {
            ImGui::OpenPopup(kGeneratorPopup);
            g_gen.PendingOpen = false;
        }
        if (g_draft.PendingDelete)
        {
            ImGui::OpenPopup(kDeletePopup);
            g_draft.PendingDelete = false;
        }
        if (g_leavePending)
        {
            if (ImGui::IsPopupOpen(kGeneratorPopup))
            {
                WipeString(g_gen.Preview);
                g_gen.Target = -1;
            }
            ImGui::OpenPopup(kLeavePopup);
            g_leavePending = false;
        }
        if (g_iconPickerPending)
        {
            ImGui::OpenPopup(kIconPopup);
            g_iconPickerPending = false;
        }
        if (g_folderCreatePending)
        {
            ImGui::OpenPopup(kFolderPopup);
            g_folderCreatePending = false;
        }

        ImGui::SetNextWindowSize(Px(420, 0), ImGuiCond_Appearing);
        if (UI::BeginTitledModal(T(Str::Editor_NewFolderTitle), kFolderPopup,
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##newfolder", T(Str::Main_FolderNameHint),
                                     g_newFolderName, sizeof(g_newFolderName));

            const std::string typed = g_newFolderName;
            const std::string existing =
                Vault::ExistingSpelling(session.Vault().Doc(), typed);

            ImGui::Dummy(Px(0, 6));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextWrapped("%s", T(Str::Editor_FolderNote));
            if (!existing.empty())
            {
                ImGui::Dummy(Px(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                ImGui::TextWrapped("%s", T(Str::Editor_FolderExists));
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 14));
            const bool usable = Vault::ValidateFolderName(typed).IsOk();
            ImGui::BeginDisabled(!usable);
            char takeLabel[192];
            if (existing.empty())
                std::snprintf(takeLabel, sizeof(takeLabel), "%s",
                              T(Str::Editor_FolderCreate));
            else
                std::snprintf(takeLabel, sizeof(takeLabel),
                              T(Str::Editor_FolderUseExisting), existing.c_str());
            if (ImGui::Button(takeLabel, Px(200, 32)))
            {
                g_draft.Folder = existing.empty() ? typed : existing;
                g_newFolderName[0] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine(0, Px(8));
            if (ImGui::Button(T(Str::Editor_Cancel), Px(120, 32)))
            {
                g_newFolderName[0] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        DrawIconPicker();

        ImGui::SetNextWindowSize(
            ImVec2(Max(Px(450.0f), MeasureGeneratorButtons().RowWidth() +
                                       ImGui::GetStyle().WindowPadding.x * 2.0f),
                   0.0f),
            ImGuiCond_Always);
        if (UI::BeginTitledModal(T(Str::Editor_GeneratePassword), kGeneratorPopup,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            const bool accepted = DrawGeneratorBody(app);
            if (accepted && g_gen.Target >= 0 &&
                g_gen.Target < static_cast<int>(g_draft.Rows.size()))
            {
                Row& row = g_draft.Rows[static_cast<std::size_t>(g_gen.Target)];
                const bool placed =
                    MakeSecret(row, true) && row.Hidden &&
                    row.Hidden->Set(g_gen.Preview.c_str(), g_gen.Preview.size());
                if (placed)
                {
                    WipeString(g_gen.Preview);
                    g_gen.Target = -1;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    g_gen.Problem = T(Str::Editor_SecretFieldFailed);
                }
            }
            else if (accepted && g_gen.Target == kGenTargetListComposer &&
                     g_draft.ComposeSecret)
            {
                if (g_draft.ComposeSecret->Set(g_gen.Preview.c_str(), g_gen.Preview.size()))
                {
                    WipeString(g_gen.Preview);
                    g_gen.Target = -1;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    g_gen.Problem = T(Str::Editor_SecretFieldFailed);
                }
            }
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowSize(Px(400, 0), ImGuiCond_Appearing);
        if (UI::BeginTitledModal(T(Str::Editor_DeleteTitle), kDeletePopup,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextWrapped(T(Str::Editor_DeleteWarning), g_draft.Title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 14));

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.18f));
            char confirmDeleteLabel[96];
            std::snprintf(confirmDeleteLabel, sizeof(confirmDeleteLabel), ICON_FA_TRASH "   %s",
                          T(Str::Editor_DeleteConfirm));
            if (ImGui::Button(confirmDeleteLabel, Px(220, 32)))
            {
                if (Trash::Delete(session.Vault().Doc(), { g_draft.Id }, Application::Now()) != 0)
                {
                    documentChanged = true;
                    ResetDraft();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, Px(8));
            const bool cancel = ImGui::Button(T(Str::Editor_Cancel), Px(120, 32));
            ImGui::SetItemDefaultFocus();
            if (cancel)
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        std::function<void()> runAfter;
        ImGui::SetNextWindowSize(Px(480, 0), ImGuiCond_Appearing);
        if (UI::BeginTitledModal(T(Str::Editor_LeaveTitle), kLeavePopup,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            if (!g_draft.Open || !g_leaveAsking)
            {
                ForgetLeave();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            else
            {
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                const Entry* stored = g_draft.Id != 0
                    ? Vault::FindEntry(session.Vault().Doc(), g_draft.Id)
                    : nullptr;
                if (stored)
                    ImGui::TextWrapped(T(Str::Editor_LeaveText), stored->Title.c_str());
                else
                    ImGui::TextWrapped("%s", T(Str::Editor_LeaveTextNew));
                ImGui::PopStyleColor();
                const bool canSave = g_draft.Problem.empty();
                if (!canSave)
                {
                    ImGui::Dummy(Px(0, 4));
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                    ImGui::TextWrapped(T(Str::Editor_LeaveCannotSave), g_draft.Problem.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::PopFont();
                ImGui::Dummy(Px(0, 14));

                ImGui::BeginDisabled(!canSave);
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::AccentDim));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::AccentCore, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
                char saveLabel[96];
                std::snprintf(saveLabel, sizeof(saveLabel), ICON_FA_FLOPPY_DISK "   %s",
                              T(Str::Editor_LeaveSave));
                if (ImGui::Button(saveLabel, Px(140, 32)))
                {
                    const SaveOutcome outcome = SaveDraft(session);
                    if (outcome != SaveOutcome::Refused)
                        documentChanged = true;
                    if (outcome == SaveOutcome::Saved)
                        runAfter = std::move(g_afterLeave);
                    ForgetLeave();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(3);
                ImGui::EndDisabled();

                ImGui::SameLine(0, Px(8));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.18f));
                char discardLabel[96];
                std::snprintf(discardLabel, sizeof(discardLabel), ICON_FA_XMARK "   %s",
                              T(Str::Editor_LeaveDiscard));
                if (ImGui::Button(discardLabel, Px(140, 32)))
                {
                    runAfter = std::move(g_afterLeave);
                    ForgetLeave();
                    ResetDraft();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine(0, Px(8));
                const bool stay = ImGui::Button(T(Str::Editor_LeaveStay), Px(150, 32));
                ImGui::SetItemDefaultFocus();
                if (stay || (!ImGui::IsWindowAppearing() &&
                             ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                {
                    ForgetLeave();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        if (runAfter)
            runAfter();

        return documentChanged;
    }

    bool Draw(Application& app, VaultSession& session)
    {
        if (!g_draft.Open)
            return false;

        if (g_leaveAsking ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            for (Row& row : g_draft.Rows)
                if (row.Hidden)
                    row.Hidden->SetFocus(false);
            if (g_draft.Paste)
                g_draft.Paste->SetFocus(false);
            if (g_draft.ComposeSecret)
                g_draft.ComposeSecret->SetFocus(false);
            if (g_draft.ReplaceSecret)
                g_draft.ReplaceSecret->SetFocus(false);
        }

        const Fonts::Set& f = Fonts::Current();
        bool documentChanged = false;

        Vault::DraftField shapeFields[Vault::kMaxFieldsPerEntry] = {};
        const std::size_t shapeCount =
            g_draft.Rows.size() < Vault::kMaxFieldsPerEntry
                ? g_draft.Rows.size()
                : Vault::kMaxFieldsPerEntry;
        for (std::size_t i = 0; i < shapeCount; ++i)
        {
            const Row& row = g_draft.Rows[i];
            shapeFields[i].Key = row.Key;
            shapeFields[i].ValueBytes = row.Secret
                                            ? (row.Hidden ? row.Hidden->Length() : 0)
                                            : std::strlen(row.Value);
            shapeFields[i].HasRole = row.Role != Vault::FieldRole::Custom;
        }

        Vault::DraftShape shape;
        shape.Title = g_draft.Title;
        shape.PlatformId = g_draft.PlatformId;
        shape.Folder = g_draft.Folder.c_str();
        shape.Fields = shapeFields;
        shape.FieldCount = g_draft.Rows.size();

        bool reusedPassword = false;
        if (g_draft.Id != 0)
        {
            const Vault::Document& doc = session.Vault().Doc();
            if (const Entry* stored = Vault::FindEntry(doc, g_draft.Id))
            {
                for (const Row& row : g_draft.Rows)
                {
                    if (row.Role != Vault::FieldRole::Password || !row.Hidden)
                        continue;
                    reusedPassword = Vault::WasUsedBefore(
                        doc, *stored,
                        reinterpret_cast<const unsigned char*>(row.Hidden->Data()),
                        row.Hidden->Length());
                    break;
                }
            }
        }

        Status valid = Vault::ValidateShape(shape);
        if (valid.IsOk() && g_draft.HasTotp)
            valid = Vault::ValidateTotp(g_draft.Totp);
        if (valid.IsOk() && !g_draft.Items.empty())
        {
            std::vector<Vault::DraftListItem> items(g_draft.Items.size());
            for (std::size_t i = 0; i < g_draft.Items.size(); ++i)
            {
                items[i].Label = g_draft.Items[i].Label;
                items[i].SecretBytes = g_draft.Items[i].Secret.Size();
                items[i].CarriesUnknown = !g_draft.Items[i].Unknown.empty();
            }
            valid = Vault::ValidateListShape(items.data(), items.size());
        }
        g_draft.Problem = valid.IsOk() ? std::string()
                                       : std::string(valid.GetError().Detail
                                                         ? valid.GetError().Detail
                                                         : T(Str::Editor_EntryIncomplete));
        if (g_draft.LoadIncomplete)
            g_draft.Problem = T(Str::Editor_ValueNotLoaded);

        char saveLabel[96];
        std::snprintf(saveLabel, sizeof(saveLabel), ICON_FA_FLOPPY_DISK "   %s",
                      T(Str::Editor_Save));
        char cancelLabel[96];
        std::snprintf(cancelLabel, sizeof(cancelLabel), ICON_FA_XMARK "   %s",
                      T(Str::Editor_Cancel));

        ImGui::Dummy(Px(0, 6));
        const ImVec2 headAt = ImGui::GetCursorScreenPos();
        const float headWidth = ImGui::GetContentRegionAvail().x;
        const float buttonHeight = Px(30.0f);
        const float gap = Px(8.0f);
        const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float saveWidth = Max(Px(110.0f), ImGui::CalcTextSize(saveLabel).x + pad);
        const float cancelWidth = Max(Px(110.0f), ImGui::CalcTextSize(cancelLabel).x + pad);
        const float buttonsLeft = headAt.x + headWidth - saveWidth - gap - cancelWidth;

        {
            const char* title = g_draft.Id == 0 ? T(Str::Editor_NewEntryTitle)
                                                : T(Str::Editor_EditEntryTitle);
            const float titleTop = headAt.y + Px(2) + (buttonHeight - Px(Fonts::Sizes::Title)) * 0.5f;
            const ImVec4 clip(headAt.x, headAt.y, buttonsLeft - gap, headAt.y + Px(2) + buttonHeight);
            ImGui::GetWindowDrawList()->AddText(f.Strong, Px(Fonts::Sizes::Title),
                                                ImVec2(headAt.x, titleTop),
                                                Theme::U32(Token::TextPrimary), title,
                                                nullptr, 0.0f, &clip);
        }

        ImGui::SetCursorScreenPos(ImVec2(buttonsLeft, headAt.y + Px(2)));
        const bool cancel = ImGui::Button(cancelLabel, ImVec2(cancelWidth, buttonHeight));

        ImGui::SameLine(0, gap);
        ImGui::BeginDisabled(!g_draft.Problem.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::AccentDim));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::AccentCore, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
        const bool save = ImGui::Button(saveLabel, ImVec2(saveWidth, buttonHeight));
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (!g_draft.Problem.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            UI::Tooltip(g_draft.Problem);
        else if (g_draft.Problem.empty() && ImGui::IsItemHovered())
            UI::Tooltip(Shortcuts::KeyName(Shortcuts::Action::SaveEntry));

        ImGui::SetCursorScreenPos(ImVec2(headAt.x, headAt.y + Px(2) + buttonHeight));

        const bool saveKey =
            Shortcuts::Pressed(Shortcuts::Action::SaveEntry) && g_draft.Problem.empty() &&
            !g_leaveAsking &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

        if (save || saveKey)
        {
            documentChanged = SaveDraft(session) != SaveOutcome::Refused;
            if (!g_draft.Open)
                return documentChanged;
        }
        if (cancel)
        {
            RequestClose(session.Vault().Doc(), {});
            if (!g_draft.Open)
                return documentChanged;
        }

        if (!g_draft.SaveProblem.empty())
        {
            ImGui::Dummy(Px(0, 6));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", g_draft.SaveProblem.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        ImGui::Dummy(Px(0, 10));
        ImGui::Separator();

        BeginScrollBody("##editorbody");
        ImGui::Dummy(Px(0, 10));

        Label(T(Str::Editor_SectionTitle));
        ImGui::Dummy(Px(0, 2));
        if (g_draft.FocusTitle)
        {
            ImGui::SetKeyboardFocusHere();
            g_draft.FocusTitle = false;
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##title", T(Str::Editor_TitleHint), g_draft.Title, sizeof(g_draft.Title));

        ImGui::Dummy(Px(0, 10));
        Label(T(Str::Editor_SectionPlatform));
        ImGui::Dummy(Px(0, 2));

        const float iconSize = Px(34.0f);
        const ImVec2 iconPos = ImGui::GetCursorScreenPos();
        IconSpec spec;
        spec.BrandName = g_draft.PlatformId;
        spec.Symbol = g_draft.Symbol;
        if (const std::uint32_t packed =
                ResolveAccent(g_draft.Accent, g_draft.PlatformId, g_draft.Symbol))
            spec.Accent = Theme::AccentOnPanel(packed);
        char monogram[5] = {};
        if (!HasGlyph(spec))
        {
            DeriveMonogram(g_draft.Title, monogram, sizeof(monogram));
            spec.Monogram = monogram;
        }
        DrawPlatformIconAt(ImGui::GetWindowDrawList(), iconPos, iconSize, spec);
        ImGui::SetCursorScreenPos(ImVec2(iconPos.x + iconSize + Px(10), iconPos.y + Px(2)));
        const float pickWidth = ImGui::GetFrameHeight() + Px(8.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - pickWidth - Px(6.0f));
        ImGui::InputTextWithHint("##platform", T(Str::Editor_PlatformHint),
                                 g_draft.PlatformId, sizeof(g_draft.PlatformId));

        ImGui::SameLine(0, Px(6));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
        if (UI::IconButton("##iconchoice", ICON_FA_MAGNIFYING_GLASS,
                          ImVec2(pickWidth, ImGui::GetFrameHeight())))
        {
            g_iconSearch[0] = 0;
            {
                IconSpec current;
                current.BrandName = g_draft.PlatformId;
                g_iconBrands = g_draft.Symbol[0] == 0 && HasGlyph(current);
            }
            g_iconPickerPending = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(T(Str::Editor_ChooseIcon));
        ImGui::SetCursorScreenPos(ImVec2(iconPos.x, iconPos.y + iconSize + Px(4)));

        ImGui::PushFont(f.Text, Fonts::Sizes::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
        ImGui::TextUnformatted(LookupSymbolIcon(g_draft.Symbol) != 0 ? T(Str::Editor_SymbolChosen)
                               : HasGlyph(spec)                      ? T(Str::Editor_BrandFound)
                                                                     : T(Str::Editor_BrandMissing));
        ImGui::PopStyleColor();
        ImGui::PopFont();

        {
            const char* login = "";
            for (const Row& row : g_draft.Rows)
            {
                if (row.Secret || row.Value[0] == 0)
                    continue;
                if (row.Role == Vault::FieldRole::Username)
                {
                    login = row.Value;
                    break;
                }
                if (row.Role == Vault::FieldRole::Email && login[0] == 0)
                    login = row.Value;
            }
            const Vault::Document& doc = session.Vault().Doc();
            const std::uint64_t twin = Vault::FindLikelyDuplicate(
                doc, g_draft.Id, g_draft.PlatformId, g_draft.Title, login);
            if (const Vault::Entry* other = twin ? Vault::FindEntry(doc, twin) : nullptr)
            {
                ImGui::Dummy(Px(0, 4));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                char line[256];
                std::snprintf(line, sizeof(line), T(Str::Editor_Duplicate), other->Title.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(ICON_FA_CIRCLE_INFO);
                ImGui::SameLine(0, Px(6));
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, Px(10));
                if (ImGui::SmallButton(T(Str::Editor_DuplicateShow)))
                    g_showTarget = twin;
                ImGui::PopFont();
            }
        }

        ImGui::Dummy(Px(0, 12));
        Label(T(Str::Editor_SectionFolder));
        ImGui::Dummy(Px(0, 2));

        const float folderPick = ImGui::GetFrameHeight() + Px(8.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - folderPick - Px(6.0f));
        if (ImGui::BeginCombo("##folder", g_draft.Folder.empty()
                                              ? T(Str::Editor_FolderNone)
                                              : g_draft.Folder.c_str()))
        {
            if (ImGui::Selectable(T(Str::Editor_FolderNone), g_draft.Folder.empty()))
                g_draft.Folder.clear();
            for (const std::string& name : Vault::FolderNames(session.Vault().Doc()))
            {
                const bool chosen = Vault::SameFolder(name, g_draft.Folder);
                if (ImGui::Selectable(name.c_str(), chosen))
                    g_draft.Folder = name;
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, Px(6));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
        if (UI::IconButton("##foldercreate", ICON_FA_FOLDER_PLUS,
                          ImVec2(folderPick, ImGui::GetFrameHeight())))
        {
            g_newFolderName[0] = 0;
            g_folderCreatePending = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            UI::Tooltip(T(Str::Editor_NewFolder));

        ImGui::Dummy(Px(0, 12));
        Label(T(Str::Editor_Colour));
        ImGui::Dummy(Px(0, 2));

        {
            const std::uint32_t packed =
                ResolveAccent(g_draft.Accent, g_draft.PlatformId, g_draft.Symbol);
            const Rgb shown = packed ? Rgb::FromPacked(packed)
                                     : Rgb{ 0x8A, 0x93, 0x9E };
            float colour[3] = { shown.R / 255.0f, shown.G / 255.0f, shown.B / 255.0f };

            if (ImGui::ColorEdit3("##colour", colour,
                                  ImGuiColorEditFlags_NoInputs |
                                  ImGuiColorEditFlags_NoLabel))
            {
                g_draft.Accent = Rgb{
                    static_cast<unsigned char>(colour[0] * 255.0f + 0.5f),
                    static_cast<unsigned char>(colour[1] * 255.0f + 0.5f),
                    static_cast<unsigned char>(colour[2] * 255.0f + 0.5f) }.Packed();
                if (g_draft.Accent == 0)
                    g_draft.Accent = 0x010101u;
            }

            ImGui::SameLine(0, Px(8));
            ImGui::BeginDisabled(g_draft.Accent == 0);
            if (ImGui::Button(T(Str::Editor_ColourAuto), Px(140, 0)))
                g_draft.Accent = 0;
            ImGui::EndDisabled();
        }
        ImGui::Dummy(Px(0, 4));
        ImGui::PushFont(f.Text, Fonts::Sizes::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
        ImGui::TextWrapped("%s", Theme::ColouredIcons() ? T(Str::Editor_ColourNote)
                                                        : T(Str::Editor_ColourOff));
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(Px(0, 12));
        ImGui::Separator();
        ImGui::Dummy(Px(0, 8));

        Label(T(Str::Editor_SectionFields));
        ImGui::Dummy(Px(0, 4));

        std::size_t deleteAt = static_cast<std::size_t>(-1);
        std::size_t swapA = static_cast<std::size_t>(-1);
        std::size_t swapB = static_cast<std::size_t>(-1);
        int generateFor = -1;

        for (std::size_t i = 0; i < g_draft.Rows.size(); ++i)
        {
            const RowAction action = DrawRow(app, g_draft.Rows[i], i, g_draft.Rows.size());
            switch (action)
            {
            case RowAction::Delete:   deleteAt = i; break;
            case RowAction::MoveUp:   swapA = i; swapB = i - 1; break;
            case RowAction::MoveDown: swapA = i; swapB = i + 1; break;
            case RowAction::Generate: generateFor = static_cast<int>(i); break;
            case RowAction::None:     break;
            }
            ImGui::Dummy(Px(0, 6));
        }

        if (deleteAt != static_cast<std::size_t>(-1))
        {
            ClearRow(g_draft.Rows[deleteAt]);
            g_draft.Rows.erase(g_draft.Rows.begin() + static_cast<std::ptrdiff_t>(deleteAt));
        }
        else if (swapA != static_cast<std::size_t>(-1) && swapB < g_draft.Rows.size())
        {
            std::swap(g_draft.Rows[swapA], g_draft.Rows[swapB]);
        }

        if (generateFor >= 0)
        {
            g_gen.Target = generateFor;
            WipeString(g_gen.Preview);
            g_gen.PendingOpen = true;
        }

        ImGui::BeginDisabled(g_draft.Rows.size() >= Vault::kMaxFieldsPerEntry);
        char addFieldLabel[96];
        std::snprintf(addFieldLabel, sizeof(addFieldLabel), ICON_FA_CIRCLE_PLUS "   %s",
                      T(Str::Editor_AddField));
        if (ImGui::Button(addFieldLabel, Px(190, 32)))
            g_draft.Rows.push_back(Row{});

        ImGui::SameLine(0, Px(8));
        char templateLabel[96];
        std::snprintf(templateLabel, sizeof(templateLabel), ICON_FA_LIST "   %s",
                      T(Str::Editor_InsertTemplate));
        if (ImGui::Button(templateLabel, Px(200, 32)))
            InsertTemplate();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            UI::Tooltip(T(Str::Editor_TemplateNote));

        DrawTotpSection();
        DrawRecoveryCodeSection();
        DrawListsSection();

        if (reusedPassword)
        {
            ImGui::Dummy(Px(0, 8));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                               T(Str::Editor_PasswordUsedBefore));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        ImGui::Dummy(Px(0, 10));
        if (!g_draft.Problem.empty())
        {
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", g_draft.Problem.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 6));
        }

        if (g_draft.Open && g_draft.Id != 0)
        {
            ImGui::Dummy(Px(0, 4));
            ImGui::Separator();
            ImGui::Dummy(Px(0, 8));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.18f));
            char deleteEntryLabel[96];
            std::snprintf(deleteEntryLabel, sizeof(deleteEntryLabel), ICON_FA_TRASH "   %s",
                          T(Str::Editor_Delete));
            if (ImGui::Button(deleteEntryLabel, Px(140, 34)))
                g_draft.PendingDelete = true;
            ImGui::PopStyleColor(2);
        }
        ImGui::Dummy(Px(0, 10));

        ScrollFade(Token::Panel);
        ImGui::EndChild();

        return documentChanged;
    }
}
