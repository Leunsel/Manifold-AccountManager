#include "Core/Language.h"

#include <sodium.h>
#include "Core/LinkSafety.h"
#include "UI/ScreenMain.h"

#include "App/Application.h"
#include "App/BunkerTheme.h"
#include "App/Clipboard.h"
#include "App/VaultSession.h"
#include "Core/Crypto.h"
#include "Core/FileSystem.h"
#include "Core/GermanText.h"
#include "Core/FieldRole.h"
#include "Core/Folder.h"
#include "Core/ListKind.h"
#include "Core/AccentColour.h"
#include "Core/Age.h"
#include "Core/PasswordHistory.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/Otp.h"
#include "Core/Redact.h"
#include "Core/Search.h"
#include "Core/Generator.h"
#include "Core/Random.h"
#include "Core/Palette.h"
#include "Core/VaultAudit.h"
#include "Core/BackupRun.h"
#include "Core/SaveGuard.h"
#include "Core/SettingsPolicy.h"
#include "UI/BunkerFonts.h"
#include "UI/Card.h"
#include "UI/IconButton.h"
#include "UI/Modal.h"
#include "UI/Motion.h"
#include "UI/MotionCurve.h"
#include "UI/EntryEditor.h"
#include "UI/OpenLink.h"
#include "UI/ScreenAudit.h"
#include "UI/ScreenSettings.h"
#include "UI/PlatformIcon.h"
#include "UI/SecretInput.h"
#include "UI/Shortcuts.h"
#include "UI/Tooltip.h"
#include "UI/Trash.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"
#include "UI/ScrollArea.h"
#include "Generated/FaIcons.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace Bunker::UI
{
    using Theme::Token;
    using Vault::Entry;
    using Vault::Field;

    namespace
    {
        std::string FitEnd(const std::string& text, ImFont* font, float size,
                           float maxWidth)
        {
            if (maxWidth <= 0.0f)
                return std::string();
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
                return text;

            const std::string ellipsis = "\xe2\x80\xa6";
            std::size_t cut = text.size();
            while (cut > 0)
            {
                cut = Utf8SnapBack(text, cut - 1);
                const std::string candidate = text.substr(0, cut) + ellipsis;
                if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= maxWidth)
                    return candidate;
                if (cut == 0)
                    break;
            }
            return ellipsis;
        }

        class WipedText
        {
        public:
            WipedText() = default;
            ~WipedText() { Wipe(); }
            WipedText(const WipedText&) = delete;
            WipedText& operator=(const WipedText&) = delete;

            void Take(std::string&& s) { Wipe(); m_s = std::move(s); }

            std::string& Buffer() { return m_s; }
            const std::string& Str() const { return m_s; }
            const char* c_str() const { return m_s.c_str(); }

            bool Truncated() const { return m_truncated; }
            void MarkTruncated() { m_truncated = true; }

            void Wipe()
            {
                if (m_s.capacity() == 0)
                    return;
                m_s.resize(m_s.capacity(), '\0');
                sodium_memzero(m_s.data(), m_s.size());
                m_s.clear();
                m_truncated = false;
            }

        private:
            std::string m_s;
            bool m_truncated = false;
        };

        std::string_view FitEndInto(WipedText& into, std::string_view text,
                                    ImFont* font, float size, float maxWidth)
        {
            into.Wipe();
            if (maxWidth <= 0.0f)
            {
                into.MarkTruncated();
                return std::string_view("");
            }
            if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(),
                                    text.data() + text.size()).x <= maxWidth)
                return text;

            constexpr std::string_view ellipsis = "\xe2\x80\xa6";
            std::string& buf = into.Buffer();
            buf.reserve(text.size() + ellipsis.size());
            std::size_t cut = text.size();
            while (cut > 0)
            {
                cut = Utf8SnapBack(text, cut - 1);
                buf.assign(text.data(), cut);
                buf.append(ellipsis);
                if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, buf.data(),
                                        buf.data() + buf.size()).x <= maxWidth)
                {
                    into.MarkTruncated();
                    return buf;
                }
                if (cut == 0)
                    break;
            }
            buf.assign(ellipsis);
            into.MarkTruncated();
            return buf;
        }

        void TextView(std::string_view text)
        {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }

        struct Seed
        {
            const char* Title;
            const char* PlatformId;
            const char* Account;
            const char* Url;
            const char* Folder;
        };

        constexpr Seed kSeeds[] = {
            { "Steam",           "steam",          "alex.doe",              "https://store.steampowered.com", "Gaming" },
            { "Google",          "google",         "alex.doe@example.com", "https://accounts.google.com", "Work" },
            { "Rockstar Games",  "rockstar",       "alex.doe",              "https://socialclub.rockstargames.com", "Gaming" },
            { "Ubisoft Connect", "ubisoft",        "alex.doe",              "https://account.ubisoft.com", "Gaming" },
            { "Epic Games",      "epic-games",     "alex.doe",              "https://www.epicgames.com", "Gaming" },
            { "Battle.net",      "battle-net",     "AlexDoe#1234",         "https://account.battle.net", "Gaming" },
            { "GOG Galaxy",      "gog",            "alex.doe",              "https://www.gog.com", "Gaming" },
            { "Discord",         "discord",        "alex.doe",              "https://discord.com", "" },
            { "GitHub",          "github",         "alex.doe",              "https://github.com", "Work" },
            { "Nintendo",        "nintendo",       "alex.doe",              "https://accounts.nintendo.com", "Gaming" },
            { "EA App",          "ea",             "alex.doe",              "https://www.ea.com", "Gaming" },
            { "Unreal Engine",   "unreal-engine",  "alex.doe",              "https://www.unrealengine.com", "Work" },
        };

        std::uint64_t g_selectedId = 0;
        std::uint64_t g_duplicateFailedFor = 0;

        bool g_quitRequested = false;
        bool g_leaveChoseEntry = false;

        bool g_scrollToSelection = false;

        bool g_itemActiveLastFrame = false;
        bool g_popupOpenLastFrame = false;
        char g_search[128] = "";
        bool g_deletePending = false;
        std::uint64_t g_deleteTarget = 0;
        bool g_deleteMulti = false;

        enum class RowAction
        {
            None,
            Edit,
            CopyPassword,
            CopyUsername,
            CopyTwoFactor,
            OpenLogin,
            ToggleFavourite,
            MoveToFolder,
            Rename,
            Duplicate,
            Delete,
        };
        RowAction g_rowAction = RowAction::None;
        std::uint64_t g_rowActionTarget = 0;
        std::string g_rowActionFolder;

        bool g_renamePending = false;
        std::uint64_t g_renameTarget = 0;
        char g_renameBuf[Vault::kMaxTitleBytes + 1] = {};
        std::string g_renameProblem;

        void ForgetRowMenu()
        {
            g_rowAction = RowAction::None;
            g_rowActionTarget = 0;
            g_rowActionFolder.clear();
            g_renamePending = false;
            g_renameTarget = 0;
            g_renameBuf[0] = 0;
            g_renameProblem.clear();
        }

        bool g_focusSearch = false;

        enum class FolderScope : std::uint8_t { All, Favourites, Unfiled, One, Finding };
        FolderScope g_folderScope = FolderScope::All;
        std::string g_folderName;

        Audit::FindingKind g_findingKind = Audit::FindingKind::None;
        std::vector<std::uint64_t> g_findingIds;

        std::vector<std::uint64_t> g_multi;
        std::uint64_t g_multiAnchor = 0;

        bool InMulti(std::uint64_t id)
        {
            return std::find(g_multi.begin(), g_multi.end(), id) != g_multi.end();
        }

        bool MultiActive()
        {
            return g_multi.size() >= 2;
        }

        void ClearMulti()
        {
            g_multi.clear();
            g_multiAnchor = 0;
        }

        void ResetUiState();

        void ClearSelection();

        bool CopyUsernameOf(Application& app, const Entry* e);
        bool CopyTwoFactorOf(Application& app, const Entry* e);

        Motion::Entrance g_detailEntrance;
        Motion::Entrance g_listEntrance;

        struct QuickDone
        {
            std::uint64_t Entry = 0;
            int Which = -1;
            double Until = 0.0;
        };
        QuickDone g_quickDone;
        constexpr double kQuickDoneSeconds = 1.2;

        void ForgetPalette();

        constexpr const char* kEntryPayload = "BUNKER_ENTRY";
        std::uint64_t g_revealEntry = 0;
        std::size_t g_revealIndex = static_cast<std::size_t>(-1);
        double g_revealStarted = 0.0;

        void StopRevealing()
        {
            g_revealEntry = 0;
            g_revealIndex = static_cast<std::size_t>(-1);
        }
        float g_savedFlash = 0.0f;
        double g_discardedNoticeUntil = 0.0;

        std::string g_notice;
        double g_noticeUntil = 0.0;
        bool g_noticeWarn = false;
        constexpr double kNoticeSeconds = 2.5;

        bool g_undoRequested = false;

        bool g_helpPending = false;

        void Notice(std::string text, bool warn)
        {
            g_notice = std::move(text);
            g_noticeUntil = Application::Now() + kNoticeSeconds;
            g_noticeWarn = warn;
        }

        void NoticeCopied(const char* what)
        {
            char line[192];
            std::snprintf(line, sizeof(line), T(Str::Main_Copied), what);
            Notice(line, false);
        }

        IconSpec SpecFor(const Entry& e)
        {
            IconSpec spec;
            spec.BrandName = e.PlatformId.c_str();
            spec.Symbol = e.Symbol.c_str();
            spec.Monogram = nullptr;
            const std::uint32_t packed =
                ResolveAccent(e.Accent, e.PlatformId.c_str(), e.Symbol.c_str());
            spec.Accent = packed ? Theme::AccentOnPanel(packed) : 0;
            return spec;
        }

        const Entry* SelectedEntry(const Vault::Document& doc)
        {
            return g_selectedId == 0 ? nullptr
                                     : Vault::FindEntry(doc, g_selectedId);
        }

        bool InScope(const Vault::Document& doc, std::uint64_t id)
        {
            if (g_folderScope == FolderScope::All)
                return true;
            if (g_folderScope == FolderScope::Finding)
                return std::find(g_findingIds.begin(), g_findingIds.end(), id) !=
                       g_findingIds.end();
            const Entry* e = Vault::FindEntry(doc, id);
            if (!e)
                return false;
            if (g_folderScope == FolderScope::Favourites)
                return e->Favourite;
            if (g_folderScope == FolderScope::Unfiled)
                return e->Folder.empty();
            return !e->Folder.empty() && Vault::SameFolder(e->Folder, g_folderName);
        }

        struct Visible
        {
            std::vector<Vault::SearchHit> Hits;
            std::size_t Outside = 0;
        };

        Visible VisibleHits(const Vault::Document& doc)
        {
            Visible out;
            const Vault::SortOrder order = Vault::SortOrderFrom(doc.VaultSettings.ListOrder);
            for (const Vault::SearchHit& hit : Vault::Search(doc, g_search, order))
            {
                if (InScope(doc, hit.EntryId))
                    out.Hits.push_back(hit);
                else
                    ++out.Outside;
            }
            return out;
        }

        std::uint64_t FirstVisibleId(const Vault::Document& doc)
        {
            const Visible visible = VisibleHits(doc);
            return visible.Hits.empty() ? 0 : visible.Hits.front().EntryId;
        }

        std::string FormatStamp(std::uint64_t utcSeconds)
        {
            return FormatLocalTimestamp(utcSeconds);
        }

        std::string AgeText(std::uint64_t thenUtc)
        {
            if (thenUtc == 0)
                return FormatStamp(0);
            const Vault::Age age = Vault::AgeBetween(thenUtc, Vault::SystemUtcNow());
            char out[96];
            auto pick = [&](Str one, Str many) {
                if (age.Count == 1)
                    std::snprintf(out, sizeof(out), "%s", T(one));
                else
                    std::snprintf(out, sizeof(out), T(many),
                                  static_cast<std::size_t>(age.Count));
            };
            switch (age.Unit)
            {
            case Vault::AgeUnit::JustNow:
                std::snprintf(out, sizeof(out), "%s", T(Str::Age_JustNow));
                break;
            case Vault::AgeUnit::Minutes: pick(Str::Age_MinuteOne, Str::Age_MinuteMany); break;
            case Vault::AgeUnit::Hours:   pick(Str::Age_HourOne, Str::Age_HourMany); break;
            case Vault::AgeUnit::Days:    pick(Str::Age_DayOne, Str::Age_DayMany); break;
            case Vault::AgeUnit::Weeks:   pick(Str::Age_WeekOne, Str::Age_WeekMany); break;
            case Vault::AgeUnit::Months:  pick(Str::Age_MonthOne, Str::Age_MonthMany); break;
            case Vault::AgeUnit::Years:   pick(Str::Age_YearOne, Str::Age_YearMany); break;
            }
            return out;
        }

        const Field* FindByRole(const Entry& e, Vault::FieldRole role)
        {
            for (const Field& f : e.Fields)
                if (f.Role == role)
                    return &f;
            return nullptr;
        }

        const Field* FindLogin(const Entry& e)
        {
            const Field* user = FindByRole(e, Vault::FieldRole::Username);
            return user ? user : FindByRole(e, Vault::FieldRole::Email);
        }

        void AddSeedEntries(VaultSession& session)
        {
            Vault::Document& doc = session.Vault().Doc();
            const std::uint64_t now = Vault::SystemUtcNow();
            for (const Seed& s : kSeeds)
            {
                Entry e;
                e.Title = s.Title;
                e.PlatformId = s.PlatformId;
                e.Folder = s.Folder;

                Field user;
                user.Role = Vault::FieldRole::Username;
                user.Key = Vault::CanonicalFieldName(user.Role);
                user.Value = s.Account;

                Field pw;
                pw.Role = Vault::FieldRole::Password;
                pw.Key = Vault::CanonicalFieldName(pw.Role);
                {
                    GeneratorOptions options;
                    options.Length = 20;
                    options.AvoidAmbiguous = true;
                    auto made = GeneratePassword(options, SystemRandom());
                    if (!made.IsOk())
                        continue;
                    pw.Value = made.Value().Text;
                    WipeString(made.Value().Text);
                }
                pw.Flags = Vault::kFieldFlagSecret;

                Field url;
                url.Role = Vault::FieldRole::Website;
                url.Key = Vault::CanonicalFieldName(url.Role);
                url.Value = s.Url;

                e.Fields.push_back(std::move(user));
                e.Fields.push_back(std::move(pw));
                e.Fields.push_back(std::move(url));

                if (e.Fields[0].Value.Size() != std::strlen(s.Account) ||
                    e.Fields[2].Value.Size() != std::strlen(s.Url) ||
                    e.Fields[1].Value.Empty())
                    continue;

                (void)Vault::AddEntry(doc, std::move(e), now);
            }
            g_selectedId = FirstVisibleId(doc);
            if (session.Vault().Save(RealFileSystem(), session.Path()).IsOk())
                g_savedFlash = static_cast<float>(ImGui::GetTime()) + 2.0f;
        }

        void SaveNow(Application& app, VaultSession& session)
        {
            const Status result = session.Vault().Save(RealFileSystem(), session.Path());
            app.Saves().Record(result, Application::Now());
            if (!result.IsOk())
                return;
            g_savedFlash = static_cast<float>(ImGui::GetTime()) + 2.0f;

            app.SetLastBackup(Vault::RunBackup(RealFileSystem(), session.Path(),
                                               session.Vault().Doc().VaultSettings,
                                               session.Vault().Generation(),
                                               Vault::SystemUtcNow()));
        }

        void LockFromUi(Application& app, LockReason reason)
        {
            app.LockVault(reason);
            Editor::Close();
            Settings::Close();
            ResetUiState();
        }

        void SectionLabel(const char* text)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 2));
        }

        constexpr float kRowHeight = 28.0f;
        constexpr float kRowIconAt = 4.0f;
        constexpr float kRowTextAt = 26.0f;
        constexpr float kRowTallyInset = 6.0f;
        constexpr float kRowTallyGap = 6.0f;

        float RowTextY(float rowY, float fontSize)
        {
            return rowY + (Px(kRowHeight) - fontSize) * 0.5f - Px(1.0f);
        }

        float RowTally(const ImVec2& rowAt, float rowWidth, std::size_t count,
                       Token colour)
        {
            const Fonts::Set& f = Fonts::Current();
            char tally[16];
            std::snprintf(tally, sizeof(tally), "%zu", count);
            const ImVec2 size =
                f.Strong->CalcTextSizeA(Px(Fonts::Sizes::Label), FLT_MAX, 0.0f, tally);
            const float x = rowAt.x + rowWidth - size.x - Px(kRowTallyInset);
            ImGui::GetWindowDrawList()->AddText(
                f.Strong, Px(Fonts::Sizes::Label),
                ImVec2(x, rowAt.y + (Px(kRowHeight) - size.y) * 0.5f),
                Theme::U32(colour), tally);
            return x;
        }

        struct MenuRowLook
        {
            bool Selected = false;
            bool Marked = false;
            bool OwnIconColour = false;
            Token IconColour = Token::TextSecondary;
            std::size_t Tally = 0;
            Token TallyColour = Token::TextMuted;
            bool BadgeWhenCompact = false;
            const char* KeyName = nullptr;
        };

        bool g_sidebarCompact = false;
        constexpr float kCompactSidebarWidth = 56.0f;
        constexpr float kTopBarHeight = 56.0f;
        constexpr float kCompactRowHeight = 34.0f;
        constexpr float kFolderTileSize = 24.0f;

        bool SidebarSelectable(const char* id, bool selected, float height)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
            const bool clicked = ImGui::Selectable(id, selected, 0, ImVec2(0, height));
            ImGui::PopStyleColor(3);

            const bool hovered = ImGui::IsItemHovered();
            const ImGuiID item = ImGui::GetItemID();
            const float hot = Motion::Toward(Motion::KeyFor(item, "hover"), hovered, 0.12f);
            const float chosen = Motion::Toward(Motion::KeyFor(item, "chosen"), selected, 0.16f);
            if (hot > 0.0f || chosen > 0.0f)
            {
                const ImVec4& base = ImGui::GetStyleColorVec4(ImGuiCol_Header);
                const ImVec4& over = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
                ImVec4 colour(Lerp(base.x, over.x, hot), Lerp(base.y, over.y, hot),
                              Lerp(base.z, over.z, hot), Lerp(base.w * chosen, over.w, hot));
                if (ImGui::IsItemActive() && hovered)
                    colour = ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive);
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                min.y += Px(1.0f);
                max.y -= Px(1.0f);
                ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(colour),
                                                          Theme::Round(Px(6.0f)));
            }
            return clicked;
        }

        void RowTooltip(const char* text, const char* keyName)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                return;
            if (!BeginWrappedTooltip())
                return;
            ImGui::TextUnformatted(text);
            if (keyName)
            {
                ImGui::SameLine(0, Px(12));
                ImGui::TextDisabled("%s", keyName);
            }
            EndWrappedTooltip();
        }

        void CentredGlyph(ImDrawList* dl, ImFont* font, float size, ImVec2 centre,
                          ImU32 colour, const char* glyph)
        {
            const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
            dl->AddText(font, size,
                        ImVec2(centre.x - measured.x * 0.5f, centre.y - measured.y * 0.5f),
                        colour, glyph);
        }

        bool MenuRowWidget(const char* id, const char* icon, const char* text,
                           const MenuRowLook& look)
        {
            const Fonts::Set& f = Fonts::Current();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const Token textColour = (look.Selected || look.Marked) ? Token::TextPrimary
                                                                     : Token::TextSecondary;

            if (g_sidebarCompact)
            {
                const float rowHeight = Px(kCompactRowHeight);
                const bool clicked = SidebarSelectable(id, look.Selected, rowHeight);
                RowTooltip(text, look.KeyName);
                const ImVec2 centre(at.x + width * 0.5f, at.y + rowHeight * 0.5f);
                CentredGlyph(dl, f.Text, Px(Fonts::Sizes::Body) * 1.1f, centre,
                             Theme::U32(look.OwnIconColour ? look.IconColour : textColour),
                             icon);
                if (look.Tally != 0 && look.BadgeWhenCompact)
                {
                    char tally[16];
                    std::snprintf(tally, sizeof(tally), "%zu", look.Tally);
                    const float size = Px(Fonts::Sizes::Label) * 0.9f;
                    const ImVec2 t = f.Strong->CalcTextSizeA(size, FLT_MAX, 0.0f, tally);
                    const float h = t.y + Px(2);
                    const float w = std::max(h, t.x + Px(6));
                    const ImVec2 max(at.x + width - Px(1), at.y + Px(2) + h);
                    const ImVec2 min(max.x - w, at.y + Px(2));
                    dl->AddRectFilled(min, max, Theme::U32(Token::Panel), Theme::Round(h * 0.5f));
                    dl->AddRectFilled(min, max, Theme::U32(look.TallyColour, 0.22f),
                                      Theme::Round(h * 0.5f));
                    dl->AddText(f.Strong, size,
                                ImVec2(min.x + (w - t.x) * 0.5f, min.y + (h - t.y) * 0.5f),
                                Theme::U32(look.TallyColour), tally);
                }
                return clicked;
            }

            const bool clicked = SidebarSelectable(id, look.Selected, Px(kRowHeight));
            const float textY = RowTextY(at.y, Px(Fonts::Sizes::Body));
            dl->AddText(f.Text, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(kRowIconAt), textY),
                        Theme::U32(look.OwnIconColour ? look.IconColour : textColour), icon);

            float textEnd = at.x + width - Px(kRowTallyInset);
            if (look.Tally != 0)
                textEnd = RowTally(at, width, look.Tally, look.TallyColour) - Px(kRowTallyGap);

            dl->PushClipRect(ImVec2(at.x + Px(kRowTextAt), at.y),
                             ImVec2(textEnd, at.y + Px(kRowHeight)), true);
            dl->AddText(f.Text, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(kRowTextAt), textY),
                        Theme::U32(textColour), text);
            dl->PopClipRect();
            return clicked;
        }

        constexpr ImVec2 kPillPad(9.0f, 4.0f);

        ImVec2 PillSize(const char* text)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
            const ImVec2 t = ImGui::CalcTextSize(text);
            ImGui::PopFont();
            return ImVec2(t.x + Px(kPillPad.x * 2), t.y + Px(kPillPad.y * 2));
        }

        void PillAt(ImVec2 topLeft, const char* text, Token fg, Token bg, float alpha = 1.0f)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
            const ImVec2 size = PillSize(text);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y),
                              Theme::U32(bg, alpha), Theme::Round(size.y * 0.5f));
            dl->AddText(ImVec2(topLeft.x + Px(kPillPad.x), topLeft.y + Px(kPillPad.y)),
                        Theme::U32(fg, alpha), text);
            ImGui::PopFont();
        }

        constexpr double kPillFadeIn = 0.15;
        constexpr double kPillFadeOut = 0.30;

        float PillPresence(double since, double until, double now)
        {
            if (!Motion::Enabled())
                return 1.0f;
            const float in = Curve::EaseOut(static_cast<float>((now - since) / kPillFadeIn));
            const float out = Curve::EaseOut(static_cast<float>((until - now) / kPillFadeOut));
            return std::min(in, out);
        }

        struct Presence
        {
            double Since = -100.0;
            bool Shown = false;

            float Update(bool shown, double now)
            {
                if (shown && !Shown)
                    Since = now;
                Shown = shown;
                return PillPresence(Since, now + kPillFadeOut, now);
            }
        };
        Presence g_clipPill;
        Presence g_undoPill;

        void TakeFirstHit(const Vault::Document& doc)
        {
            const Visible visible = VisibleHits(doc);
            if (visible.Hits.empty())
                return;
            for (const Vault::SearchHit& hit : visible.Hits)
            {
                if (hit.EntryId == g_selectedId && g_selectedId != 0)
                {
                    g_scrollToSelection = true;
                    return;
                }
            }
            const std::uint64_t first = visible.Hits.front().EntryId;
            auto select = [first] {
                g_selectedId = first;
                g_leaveChoseEntry = true;
                g_scrollToSelection = true;
            };
            if (Editor::IsOpen() && Editor::EditingId() != first)
                Editor::RequestClose(doc, select);
            else
                select();
        }

        void StartNewEntry(const Vault::Document& doc)
        {
            const std::string folder =
                g_folderScope == FolderScope::One ? g_folderName : std::string();
            Editor::RequestClose(doc, [folder] {
                Settings::Close();
                Audit::Close();
                Editor::BeginNew(folder);
            });
        }

        void DrawTopBar(Application& app, VaultSession& session, float height)
        {
            const Fonts::Set& f = Fonts::Current();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;

            dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), Theme::U32(Token::Panel));
            dl->AddLine(ImVec2(p.x, p.y + height - 1), ImVec2(p.x + width, p.y + height - 1),
                        Theme::U32(Token::Line));
            {
                const float half = Px(1.0f) * 0.5f;
                dl->AddLine(ImVec2(p.x + half, p.y), ImVec2(p.x + half, p.y + height),
                            Theme::U32(Token::Line), Px(1.0f));
            }

            const float centreY = p.y + height * 0.5f;
            const float margin = Px(18.0f);
            const float gap = Px(14.0f);
            const float frame = Px(34.0f);
            float right = p.x + width - margin;

            ImGui::SetCursorScreenPos(ImVec2(right - frame, centreY - frame * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
            if (UI::IconButton("###topgen", ICON_FA_DICE, ImVec2(frame, frame),
                               Token::TextSecondary, T(Str::Main_GeneratePassword)))
                Editor::OpenStandaloneGenerator();
            ImGui::PopStyleColor();
            right -= frame + Px(8.0f);

            {
                char label[96];
                std::snprintf(label, sizeof(label), ICON_FA_CIRCLE_PLUS "   %s",
                              T(Str::Main_NewEntry));
                const float w = ImGui::CalcTextSize(label).x + Px(28.0f);
                ImGui::SetCursorScreenPos(ImVec2(right - w, centreY - frame * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::AccentDim));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::AccentCore, 0.55f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Color(Token::AccentCore, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
                const bool add = ImGui::Button(label, ImVec2(w, frame));
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    UI::Tooltip(Shortcuts::KeyName(Shortcuts::Action::NewEntry));
                if (add)
                    StartNewEntry(session.Vault().Doc());
                right -= w + gap;
            }

            const bool saved = ImGui::GetTime() < g_savedFlash;
            const char* statusText = saved ? T(Str::Main_StatusSaved) : T(Str::Main_StatusUnlocked);

            char lockNote[64];
            const double lockIn = app.Lock().SecondsUntilIdleLock(Application::Now());
            const bool lockSoon = lockIn > 0.0 && lockIn <= 30.0;
            if (lockSoon)
            {
                char count[48];
                std::snprintf(count, sizeof(count), T(Str::Main_LockingIn), std::ceil(lockIn));
                std::snprintf(lockNote, sizeof(lockNote), ICON_FA_HOURGLASS_HALF "  %s", count);
                statusText = lockNote;
            }
            const ImVec2 pillSize = PillSize(statusText);
            const float pillX = right - pillSize.x;
            PillAt(ImVec2(pillX, centreY - pillSize.y * 0.5f), statusText,
                   lockSoon ? Token::WarnBright : Token::GreenCore,
                   lockSoon ? Token::WarnDim : Token::GreenDim);
            if (lockSoon)
            {
                ImGui::SetCursorScreenPos(ImVec2(pillX, centreY - pillSize.y * 0.5f));
                ImGui::InvisibleButton("##locktip", pillSize);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(T(Str::Main_LockingInTip));
            }

            float leftEdge = pillX;
            const double clearIn = app.Clipboard().SecondsUntilClear(Application::Now());
            const float clipShown = g_clipPill.Update(clearIn >= 0.0, ImGui::GetTime());
            if (clearIn >= 0.0)
            {
                char note[64];
                std::snprintf(note, sizeof(note), ICON_FA_CLIPBOARD "  %s: %.0f s",
                              app.Clipboard().LastLabel().c_str(), clearIn);
                const ImVec2 noteSize = PillSize(note);
                leftEdge = pillX - gap - noteSize.x;
                const ImVec2 noteAt(leftEdge, centreY - noteSize.y * 0.5f);
                PillAt(noteAt, note, Token::WarnBright, Token::WarnDim, clipShown);

                ImGui::SetCursorScreenPos(noteAt);
                if (ImGui::InvisibleButton("##clipclear", noteSize))
                    app.Clipboard().ClearIfOurs();
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    UI::Tooltip(T(Str::Main_ClearClipboardNow));
                }
            }

            const bool undoPending = Trash::Pending(Application::Now());
            const float undoShown = g_undoPill.Update(undoPending, ImGui::GetTime());
            if (undoPending)
            {
                const Fonts::Set& pf = Fonts::Current();
                char what[160];
                if (Trash::Count() == 1)
                {
                    const std::string title = FitEnd(Trash::FirstTitle(), pf.Strong,
                                                     Px(Fonts::Sizes::Label), Px(150.0f));
                    std::snprintf(what, sizeof(what), T(Str::Main_Deleted), title.c_str());
                }
                else
                {
                    std::snprintf(what, sizeof(what), T(Str::Main_DeletedMany), Trash::Count());
                }
                char line[256];
                std::snprintf(line, sizeof(line), ICON_FA_TRASH "  %s   " ICON_FA_ARROW_ROTATE_LEFT " %s",
                              what, T(Str::Main_Undo));
                const ImVec2 size = PillSize(line);
                leftEdge = leftEdge - gap - size.x;
                const ImVec2 at(leftEdge, centreY - size.y * 0.5f);
                ImGui::SetCursorScreenPos(at);
                const bool undo = ImGui::InvisibleButton("##undo", size);
                const bool hot = ImGui::IsItemHovered();
                PillAt(at, line, Token::AccentBright, hot ? Token::AccentDim : Token::Elevated,
                       undoShown);
                if (hot)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    UI::Tooltip(Shortcuts::KeyName(Shortcuts::Action::Undo));
                }
                if (undo)
                    g_undoRequested = true;
            }
            else if (Application::Now() < g_noticeUntil && !g_notice.empty())
            {
                char line[224];
                std::snprintf(line, sizeof(line), "%s  %s",
                              g_noticeWarn ? ICON_FA_CIRCLE_INFO : ICON_FA_CHECK,
                              g_notice.c_str());
                const ImVec2 size = PillSize(line);
                leftEdge = leftEdge - gap - size.x;
                const double now = Application::Now();
                PillAt(ImVec2(leftEdge, centreY - size.y * 0.5f), line,
                       g_noticeWarn ? Token::WarnBright : Token::TextSecondary,
                       g_noticeWarn ? Token::WarnDim : Token::Elevated,
                       PillPresence(g_noticeUntil - kNoticeSeconds, g_noticeUntil, now));
            }

            const float searchX = p.x + margin;
            const float wantedWidth = Clamp(leftEdge - gap - searchX, Px(160.0f), Px(520.0f));
            const float searchWidth = std::floor(
                Motion::Follow(ImGui::GetID("##search-width"), wantedWidth, 0.14f) + 0.5f);
            const float padY = Max(Px(3.0f), (frame - ImGui::GetFontSize()) * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(searchX, centreY - frame * 0.5f));
            if (g_focusSearch)
            {
                ImGui::SetKeyboardFocusHere();
                g_focusSearch = false;
            }
            ImGui::SetNextItemWidth(searchWidth);
            char searchHint[192];
            if (g_folderScope == FolderScope::One)
            {
                std::snprintf(searchHint, sizeof(searchHint),
                              T(Str::Main_SearchHintInFolder), g_folderName.c_str());
            }
            else
            {
                std::snprintf(searchHint, sizeof(searchHint), "%s", T(Str::Main_SearchHint));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Px(34.0f), padY));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Round(Px(8.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Color(Token::Line));
            if (ImGui::InputTextWithHint("##search", searchHint,
                                         g_search, IM_ARRAYSIZE(g_search)))
            {
                Settings::Close();
                Audit::Close();
            }
            if (ImGui::IsItemDeactivated() &&
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
                TakeFirstHit(session.Vault().Doc());
            const bool searchActive = ImGui::IsItemActive();
            const ImVec2 fieldMin = ImGui::GetItemRectMin();
            const ImVec2 fieldMax = ImGui::GetItemRectMax();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);

            {
                const float size = Px(Fonts::Sizes::Body);
                const ImVec2 t = f.Text->CalcTextSizeA(size, FLT_MAX, 0.0f, ICON_FA_MAGNIFYING_GLASS);
                dl->AddText(f.Text, size,
                            ImVec2(fieldMin.x + Px(12.0f), centreY - t.y * 0.5f),
                            Theme::U32(Token::TextMuted), ICON_FA_MAGNIFYING_GLASS);
            }
            if (!searchActive && g_search[0] == 0)
            {
                const char* key = Shortcuts::KeyName(Shortcuts::Action::Search);
                const float keySize = Px(Fonts::Sizes::Label);
                const ImVec2 kt = f.Mono->CalcTextSizeA(keySize, FLT_MAX, 0.0f, key);
                const float hintRight = fieldMin.x + Px(34.0f) + ImGui::CalcTextSize(searchHint).x;
                const ImVec2 chipMax(fieldMax.x - Px(8.0f), centreY + kt.y * 0.5f + Px(3.0f));
                const ImVec2 chipMin(chipMax.x - kt.x - Px(12.0f), centreY - kt.y * 0.5f - Px(3.0f));
                if (chipMin.x > hintRight + Px(12.0f))
                {
                    dl->AddRect(chipMin, chipMax, Theme::U32(Token::Line), Theme::Round(Px(4.0f)));
                    dl->AddText(f.Mono, keySize, ImVec2(chipMin.x + Px(6.0f), chipMin.y + Px(3.0f)),
                                Theme::U32(Token::TextMuted), key);
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
        }

        bool CopyPasswordOf(Application& app, VaultSession& session, const Entry* e)
        {
            if (!e)
            {
                Notice(T(Str::Main_NothingSelectedToCopy), true);
                return false;
            }
            const Field* pw = FindByRole(*e, Vault::FieldRole::Password);
            if (!pw || pw->Value.Empty())
            {
                Notice(T(Str::Main_NothingToCopy), true);
                return false;
            }
            const double seconds = Vault::ClipboardSeconds(
                session.Vault().Doc().VaultSettings);
            app.Clipboard().CopySecret(pw->Value.CStr(), pw->Value.Size(),
                                       static_cast<double>(seconds),
                                       Application::Now());
            app.Clipboard().SetLastLabel(T(Str::Main_CopiedPassword));
            return true;
        }

        void CopySelectedPassword(Application& app, VaultSession& session)
        {
            CopyPasswordOf(app, session, SelectedEntry(session.Vault().Doc()));
        }

        const Field* OpenableSite(const Entry& e)
        {
            const Field* site = FindByRole(e, Vault::FieldRole::Website);
            return site && IsOpenableWebLink(site->Value.View()) ? site : nullptr;
        }

        bool OpenLoginOf(Application& app, VaultSession& session, const Entry* e)
        {
            if (!e)
            {
                Notice(T(Str::Main_NothingSelectedToCopy), true);
                return false;
            }
            const Field* site = OpenableSite(*e);
            if (!site)
            {
                Notice(T(Str::Main_NoWebsite), true);
                return false;
            }
            OpenWebLink(site->Value.View());
            const Field* pw = FindByRole(*e, Vault::FieldRole::Password);
            if (!pw || pw->Value.Empty())
            {
                Notice(T(Str::Main_NothingToCopy), true);
                return true;
            }
            CopyPasswordOf(app, session, e);
            Notice(T(Str::Main_OpenedLogin), false);
            return true;
        }

        void StepSelection(const Vault::Document& doc, int delta)
        {
            const Visible visible = VisibleHits(doc);
            if (visible.Hits.empty())
                return;

            std::size_t at = 0;
            bool found = false;
            for (std::size_t i = 0; i < visible.Hits.size(); ++i)
            {
                if (visible.Hits[i].EntryId != g_selectedId)
                    continue;
                at = i;
                found = true;
                break;
            }
            if (!found)
            {
                g_selectedId = visible.Hits.front().EntryId;
                g_scrollToSelection = true;
                return;
            }

            if (delta < 0 && at == 0)
                return;
            if (delta > 0 && at + 1 >= visible.Hits.size())
                return;
            g_selectedId = visible.Hits[at + static_cast<std::size_t>(delta)].EntryId;
            g_scrollToSelection = true;
        }

        void ToggleInMulti(std::uint64_t id)
        {
            if (g_multi.empty())
            {
                if (g_selectedId == id)
                {
                    ClearSelection();
                    return;
                }
                if (g_selectedId != 0)
                    g_multi.push_back(g_selectedId);
            }
            const auto at = std::find(g_multi.begin(), g_multi.end(), id);
            if (at != g_multi.end())
                g_multi.erase(at);
            else
                g_multi.push_back(id);
            g_multiAnchor = id;

            if (g_multi.empty())
            {
                ClearSelection();
                return;
            }
            g_selectedId = InMulti(id) ? id : g_multi.back();
            if (g_multi.size() == 1)
                ClearMulti();
        }

        void SelectRangeTo(const std::vector<Vault::SearchHit>& hits, std::uint64_t to)
        {
            std::uint64_t from = g_multiAnchor != 0 ? g_multiAnchor : g_selectedId;
            if (from == 0)
                from = to;
            std::size_t a = hits.size();
            std::size_t b = hits.size();
            for (std::size_t i = 0; i < hits.size(); ++i)
            {
                if (hits[i].EntryId == from)
                    a = i;
                if (hits[i].EntryId == to)
                    b = i;
            }
            if (b == hits.size())
                return;
            if (a == hits.size())
                a = b;
            g_multi.clear();
            for (std::size_t i = std::min(a, b); i <= std::max(a, b); ++i)
                g_multi.push_back(hits[i].EntryId);
            g_multiAnchor = from;
            g_selectedId = to;
            if (g_multi.size() < 2)
                ClearMulti();
        }

        void SelectAllVisible(const Vault::Document& doc)
        {
            const Visible visible = VisibleHits(doc);
            if (visible.Hits.size() < 2)
                return;
            g_multi.clear();
            for (const Vault::SearchHit& hit : visible.Hits)
                g_multi.push_back(hit.EntryId);
            g_multiAnchor = g_multi.front();
            if (g_selectedId == 0 || !InMulti(g_selectedId))
                g_selectedId = g_multi.front();
        }

        bool ListOwnsKeys()
        {
            return !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                ImGuiPopupFlags_AnyPopupLevel) &&
                   !ImGui::IsAnyItemActive() && !Editor::IsOpen() &&
                   !Settings::IsOpen() && !Audit::IsOpen();
        }

        void DrawFolderTile(ImDrawList* dl, ImVec2 centre, float tile,
                            const char* monogram, ImU32 accent)
        {
            const ImVec2 min(centre.x - tile * 0.5f, centre.y - tile * 0.5f);
            const ImVec2 max(min.x + tile, min.y + tile);
            const ImVec4 a = ImGui::ColorConvertU32ToFloat4(accent);
            const float rounding = Theme::Round(tile * 0.26f);
            dl->AddRectFilled(min, max,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(a.x, a.y, a.z, 0.16f)),
                              rounding);
            dl->AddRect(min, max, accent, rounding, 0, Px(1.2f));
            CentredGlyph(dl, Fonts::Current().Strong, tile * 0.46f, centre, accent, monogram);
        }

        bool FolderRowWidget(const char* icon, const std::string& name,
                             std::size_t count, bool selected, bool marked,
                             const char* monogram = nullptr, ImU32 accent = 0)
        {
            const Fonts::Set& f = Fonts::Current();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;

            if (g_sidebarCompact)
            {
                const float rowHeight = Px(kCompactRowHeight);
                const bool clicked = SidebarSelectable("##folder", selected, rowHeight);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    char tally[48];
                    std::snprintf(tally, sizeof(tally), T(Str::Main_EntryCount), count);
                    if (BeginWrappedTooltip())
                    {
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::TextDisabled("%s", tally);
                        EndWrappedTooltip();
                    }
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 centre(at.x + width * 0.5f, at.y + rowHeight * 0.5f);
                if (monogram && *monogram)
                    DrawFolderTile(dl, centre, Px(kFolderTileSize), monogram, accent);
                else
                {
                    CentredGlyph(dl, f.Text, Px(Fonts::Sizes::Body) * 1.1f, centre,
                                 Theme::U32((selected || marked) ? Token::TextPrimary
                                                                 : Token::TextMuted),
                                 icon);
                }
                return clicked;
            }

            const float rowHeight = Px(kRowHeight);
            const bool clicked = SidebarSelectable("##folder", selected, rowHeight);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 colour = Theme::U32((selected || marked) ? Token::TextPrimary
                                                                 : Token::TextSecondary);
            const float textY = RowTextY(at.y, Px(Fonts::Sizes::Body));
            dl->AddText(f.Text, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(kRowIconAt), textY),
                        Theme::U32(Token::TextMuted), icon);

            char tally[24];
            std::snprintf(tally, sizeof(tally), "%zu", count);
            const ImVec2 tallySize =
                f.Text->CalcTextSizeA(Px(Fonts::Sizes::Small), FLT_MAX, 0.0f, tally);
            const float tallyX = at.x + width - tallySize.x - Px(kRowTallyInset);
            dl->AddText(f.Text, Px(Fonts::Sizes::Small),
                        ImVec2(tallyX, at.y + (rowHeight - Px(Fonts::Sizes::Small)) * 0.5f),
                        Theme::U32(Token::TextDisabled), tally);

            dl->PushClipRect(ImVec2(at.x + Px(kRowTextAt), at.y),
                             ImVec2(tallyX - Px(kRowTallyGap), at.y + rowHeight), true);
            dl->AddText(f.Text, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(kRowTextAt), textY),
                        colour, name.c_str());
            dl->PopClipRect();
            return clicked;
        }

        void SelectFolder(const Vault::Document& doc, FolderScope scope,
                          const std::string& name)
        {
            g_folderScope = scope;
            g_folderName = (scope == FolderScope::One) ? name : std::string();

            if (!InScope(doc, g_selectedId))
                ClearSelection();

            g_multi.erase(std::remove_if(g_multi.begin(), g_multi.end(),
                                         [&doc](std::uint64_t id) { return !InScope(doc, id); }),
                          g_multi.end());
            if (g_multi.size() < 2)
                ClearMulti();
            if (scope != FolderScope::Finding)
            {
                g_findingKind = Audit::FindingKind::None;
                g_findingIds.clear();
            }
        }

        void ShowFolder(const Vault::Document& doc, FolderScope scope,
                        const std::string& name)
        {
            Settings::Close();
            Audit::Close();
            SelectFolder(doc, scope, name);
        }

        bool g_folderRenamePending = false;
        bool g_folderDissolvePending = false;
        std::string g_folderTarget;
        char g_folderNameBuf[128] = {};
        std::string g_folderProblem;

        bool g_folderJumpPending = false;
        char g_folderJumpQuery[96] = {};
        int g_folderJumpIndex = 0;
        bool g_folderJumpScroll = false;
        bool g_folderJumpRefocus = false;

        void ForgetFolderDialogs()
        {
            g_folderRenamePending = false;
            g_folderDissolvePending = false;
            g_folderTarget.clear();
            g_folderNameBuf[0] = 0;
            g_folderProblem.clear();
        }

        void ForgetFolderJump()
        {
            g_folderJumpPending = false;
            g_folderJumpQuery[0] = 0;
            g_folderJumpIndex = 0;
            g_folderJumpScroll = false;
            g_folderJumpRefocus = false;
        }

        void SidebarDivider(float lead = 0.0f)
        {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float y = at.y + Px(5.0f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + Px(6.0f), y),
                                                ImVec2(at.x + w - Px(6.0f), y),
                                                Theme::U32(Token::Line), Px(1.0f));
            ImGui::Dummy(ImVec2(w, Px(10.0f) - lead));
        }

        ImU32 FolderAccent(const Vault::FolderTally& row)
        {
            if (!Theme::ColouredIcons())
                return Theme::U32(Token::TextSecondary);
            return Theme::AccentOnPanel(DerivedAccent(row.Folded.c_str()).Packed());
        }

        void ToggleSidebar(Application& app, VaultSession& session)
        {
            Vault::Document& doc = session.Vault().Doc();
            Vault::Settings draft = doc.VaultSettings;
            draft.SidebarCollapsed = !draft.SidebarCollapsed;
            Vault::SetVaultSettings(doc, draft);
            SaveNow(app, session);
        }

        std::uint64_t EntryDroppedHere()
        {
            std::uint64_t id = 0;
            if (!ImGui::BeginDragDropTarget())
                return 0;
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(
                    kEntryPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                                       ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                min.y += Px(1.0f);
                max.y -= Px(1.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(min, max, Theme::U32(Token::AccentCore, 0.14f),
                                  Theme::Round(Px(6.0f)));
                dl->AddRect(min, max, Theme::U32(Token::AccentCore), Theme::Round(Px(6.0f)), 0,
                            Px(1.0f));
                if (p->IsDelivery() && p->DataSize == static_cast<int>(sizeof(id)))
                    std::memcpy(&id, p->Data, sizeof(id));
            }
            ImGui::EndDragDropTarget();
            return id;
        }

        void DropEntry(Application& app, VaultSession& session, std::uint64_t id,
                       FolderScope scope, const std::string& folder)
        {
            Vault::Document& doc = session.Vault().Doc();
            const std::uint64_t now = Vault::SystemUtcNow();

            if (MultiActive() && InMulti(id))
            {
                const std::vector<std::uint64_t> ids = g_multi;
                std::size_t changed = 0;
                const std::string target = scope == FolderScope::One ? folder : std::string();
                for (std::uint64_t each : ids)
                {
                    const Entry* e = Vault::FindEntry(doc, each);
                    if (!e)
                        continue;
                    if (scope == FolderScope::Favourites)
                    {
                        if (!e->Favourite && Vault::SetFavourite(doc, each, true, now).IsOk())
                            ++changed;
                    }
                    else if (Vault::SetEntryFolder(doc, each, target, now).IsOk())
                        ++changed;
                }
                if (changed == 0)
                    return;
                SaveNow(app, session);
                if (scope == FolderScope::Favourites)
                {
                    Notice(T(Str::Main_MarkedFavourite), false);
                }
                else
                {
                    char line[96];
                    std::snprintf(line, sizeof(line), T(Str::Main_MultiMoved), changed);
                    Notice(line, false);
                }
                return;
            }

            const Entry* e = Vault::FindEntry(doc, id);
            if (!e)
                return;
            if (scope == FolderScope::Favourites)
            {
                if (!e->Favourite && Vault::SetFavourite(doc, id, true, now).IsOk())
                {
                    SaveNow(app, session);
                    Notice(T(Str::Main_MarkedFavourite), false);
                }
                return;
            }
            const std::string target = scope == FolderScope::One ? folder : std::string();
            const bool same = target.empty() ? e->Folder.empty()
                                             : (!e->Folder.empty() && Vault::SameFolder(e->Folder, target));
            if (same)
                return;
            if (!Vault::SetEntryFolder(doc, id, target, now).IsOk())
                return;
            SaveNow(app, session);
            if (target.empty())
                Notice(T(Str::Main_MovedToNoFolder), false);
            else
            {
                char line[192];
                std::snprintf(line, sizeof(line), T(Str::Main_MovedTo), target.c_str());
                Notice(line, false);
            }
        }

        void DrawSidebarHead(Application& app, VaultSession& session, float headHeight)
        {
            const Fonts::Set& f = Fonts::Current();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 win = ImGui::GetWindowPos();
            const float winWidth = ImGui::GetWindowWidth();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;

            dl->AddLine(ImVec2(win.x, win.y + headHeight - 1),
                        ImVec2(win.x + winWidth, win.y + headHeight - 1), Theme::U32(Token::Line));

            const float tile = Px(32.0f);
            const float tileX = g_sidebarCompact ? at.x + (w - tile) * 0.5f : at.x;
            const float tileY = win.y + (headHeight - tile) * 0.5f;
            const ImVec2 t0(tileX, tileY);
            const ImVec2 t1(tileX + tile, tileY + tile);
            dl->AddRectFilled(t0, t1, Theme::U32(Token::AccentCore, 0.14f), Theme::Round(Px(8.0f)));
            dl->AddRect(t0, t1, Theme::U32(Token::AccentCore, 0.45f), Theme::Round(Px(8.0f)), 0, 1.0f);
            CentredGlyph(dl, f.Strong, Px(Fonts::Sizes::Body) * 1.15f,
                         ImVec2(tileX + tile * 0.5f, tileY + tile * 0.5f),
                         Theme::U32(Token::AccentCore), ICON_FA_VAULT);

            if (!g_sidebarCompact)
            {
                const float toggle = Px(26.0f);
                const float textX = tileX + tile + Px(10.0f);
                const float textRight = at.x + w - toggle - Px(6.0f);
                const float nameSize = Px(Fonts::Sizes::Body) + Px(1.0f);
                const float tagSize = Px(Fonts::Sizes::Label) * 0.92f;
                dl->PushClipRect(ImVec2(textX, win.y), ImVec2(textRight, win.y + headHeight), true);
                dl->AddText(f.Strong, nameSize, ImVec2(textX, tileY - Px(1.0f)),
                            Theme::U32(Token::TextPrimary), "Vault");
                dl->AddText(f.Mono, tagSize, ImVec2(textX, tileY + nameSize + Px(3.0f)),
                            Theme::U32(Token::TextMuted), T(Str::Main_BrandTagline));
                dl->PopClipRect();

                char toggleTip[128];
                std::snprintf(toggleTip, sizeof(toggleTip), "%s   %s",
                              T(Str::Main_CollapseSidebar),
                              Shortcuts::KeyName(Shortcuts::Action::ToggleSidebar));
                ImGui::SetCursorScreenPos(
                    ImVec2(at.x + w - toggle, win.y + (headHeight - toggle) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (UI::IconButton("###sidebar-toggle", ICON_FA_CHEVRON_LEFT,
                                   ImVec2(toggle, toggle), Token::TextMuted, toggleTip))
                    ToggleSidebar(app, session);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
            ImGui::SetCursorScreenPos(ImVec2(at.x, win.y + headHeight + Px(10.0f)));
        }

        void DrawVaultCard(Application& app, const Vault::Document& doc,
                           const Vault::FolderSurvey& folders, std::size_t favourites)
        {
            const Fonts::Set& f = Fonts::Current();
            BeginBox("##vaultcard", ImGui::GetContentRegionAvail().x, Px(12.0f, 10.0f));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float labelSize = Px(Fonts::Sizes::Label);

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::PushFont(f.Mono, Fonts::Sizes::Label);
            ImGui::TextUnformatted(T(Str::Main_VaultCardLabel));
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(at.x + w - Px(4.0f), at.y + labelSize * 0.55f), Px(3.5f),
                Theme::U32(app.Saves().Failing() ? Token::RedCore : Token::GreenCore));

            char count[64];
            if (doc.Entries.size() == 1)
                std::snprintf(count, sizeof(count), "%s", T(Str::Main_CountEntryOne));
            else
                std::snprintf(count, sizeof(count), T(Str::Main_CountEntryMany), doc.Entries.size());
            ImGui::PushFont(f.Strong, Fonts::Sizes::Title);
            ImGui::TextUnformatted(count);
            ImGui::PopFont();

            char favouritesText[64];
            if (favourites == 1)
                std::snprintf(favouritesText, sizeof(favouritesText), "%s", T(Str::Main_CountFavouriteOne));
            else
                std::snprintf(favouritesText, sizeof(favouritesText), T(Str::Main_CountFavouriteMany),
                              favourites);
            char foldersText[64];
            if (folders.Folders.size() == 1)
                std::snprintf(foldersText, sizeof(foldersText), "%s", T(Str::Main_CountFolderOne));
            else
                std::snprintf(foldersText, sizeof(foldersText), T(Str::Main_CountFolderMany),
                              folders.Folders.size());
            const std::string sub = UppercaseGerman(std::string(favouritesText) +
                                                    "  \xc2\xb7  " + foldersText);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::PushFont(f.Mono, Fonts::Sizes::Label);
            ImGui::TextUnformatted(FitEnd(sub, f.Mono, labelSize, w).c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();

            EndBox(Theme::U32(Token::Elevated, 0.45f), Theme::U32(Token::Line),
                   Theme::Round(Px(10.0f)));
            ImGui::Dummy(Px(0, 6));
        }

        void DrawSidebar(Application& app, VaultSession& session,
                         float width, float height, float headHeight)
        {
            const Vault::Document& doc = session.Vault().Doc();
            const Fonts::Set& f = Fonts::Current();
            g_sidebarCompact = doc.VaultSettings.SidebarCollapsed;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Color(Token::Panel));
            if (g_sidebarCompact)
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PxWhole(8, 10));
            ImGui::BeginChild("##sidebar", ImVec2(width, height),
                              ImGuiChildFlags_AlwaysUseWindowPadding);
            if (g_sidebarCompact)
                ImGui::PopStyleVar();

            ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, Theme::Round(Px(6.0f)));

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

            if (Editor::TakeDiscardedNotice())
                g_discardedNoticeUntil = ImGui::GetTime() + 12.0;

            DrawSidebarHead(app, session, headHeight);

            if (g_sidebarCompact)
            {
                MenuRowLook toggleLook;
                toggleLook.KeyName = Shortcuts::KeyName(Shortcuts::Action::ToggleSidebar);
                if (MenuRowWidget("##sidebar-toggle", ICON_FA_CHEVRON_RIGHT,
                                  T(Str::Main_ExpandSidebar), toggleLook))
                    ToggleSidebar(app, session);
                SidebarDivider();
            }

            if (ImGui::GetTime() < g_discardedNoticeUntil && g_sidebarCompact)
            {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                ImGui::Dummy(ImVec2(w, Px(kCompactRowHeight)));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    UI::Tooltip(T(Str::Main_DraftDiscarded));
                CentredGlyph(ImGui::GetWindowDrawList(), f.Text, Px(Fonts::Sizes::Body),
                             ImVec2(at.x + w * 0.5f, at.y + Px(kCompactRowHeight) * 0.5f),
                             Theme::U32(Token::WarnBright), ICON_FA_TRIANGLE_EXCLAMATION);
            }
            else if (ImGui::GetTime() < g_discardedNoticeUntil)
            {
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                                   T(Str::Main_DraftDiscarded));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::Dummy(Px(0, 10));
            }

            const Vault::FolderSurvey folders = Vault::SurveyFolders(doc);
            const bool screenOpen = Settings::IsOpen() || Audit::IsOpen();

            std::size_t favourites = 0;
            for (const Entry& e : doc.Entries)
                if (e.Favourite)
                    ++favourites;

            if (!g_sidebarCompact)
                DrawVaultCard(app, doc, folders, favourites);

            {
                MenuRowLook allLook;
                allLook.Selected = g_folderScope == FolderScope::All && !screenOpen;
                allLook.Marked = g_folderScope == FolderScope::All;
                allLook.Tally = folders.Total;
                allLook.TallyColour = Token::TextMuted;
                if (MenuRowWidget("##all", ICON_FA_LAYER_GROUP, T(Str::Main_FolderAll), allLook))
                    ShowFolder(doc, FolderScope::All, {});
            }

            const bool favouritesScope = g_folderScope == FolderScope::Favourites;
            MenuRowLook favouritesLook;
            favouritesLook.Selected = favouritesScope && !screenOpen;
            favouritesLook.Marked = favouritesScope;
            favouritesLook.OwnIconColour = favouritesScope;
            favouritesLook.IconColour = Token::WarnCore;
            favouritesLook.Tally = favourites;
            favouritesLook.TallyColour = Token::TextMuted;
            const bool favouritesClicked = MenuRowWidget("##favourites", ICON_FA_STAR,
                                                         T(Str::Main_Favourites), favouritesLook);
            if (const std::uint64_t dropped = EntryDroppedHere())
                DropEntry(app, session, dropped, FolderScope::Favourites, {});
            if (favouritesClicked)
            {
                Editor::RequestClose(doc, [&session] {
                    Settings::Close();
                    Audit::Close();
                    SelectFolder(session.Vault().Doc(), FolderScope::Favourites, {});
                });
            }

            MenuRowLook securityLook;
            securityLook.Selected = Audit::IsOpen();
            securityLook.Marked = g_folderScope == FolderScope::Finding;
            securityLook.Tally = Audit::FindingCount(session);
            securityLook.TallyColour = Token::WarnBright;
            securityLook.BadgeWhenCompact = true;
            if (MenuRowWidget("##security", ICON_FA_SHIELD_HALVED, T(Str::Main_Security),
                              securityLook))
            {
                Editor::RequestClose(doc, [] {
                    Settings::Close();
                    Audit::Open();
                });
            }

            const float buttonHeight = Px(34.0f);

            if (folders.Unfiled == 0 && g_folderScope == FolderScope::Unfiled)
                SelectFolder(doc, FolderScope::All, {});

            const float overhang = std::floor(ImGui::GetStyle().ItemSpacing.y * 0.5f);
            if (g_sidebarCompact)
                SidebarDivider(overhang);
            else
            {
                ImGui::Dummy(Px(0, 8));
                SectionLabel(T(Str::Main_SectionFolders));
            }

            const float spacingY = ImGui::GetStyle().ItemSpacing.y;
            const float menuRow = g_sidebarCompact ? Px(kCompactRowHeight) : Px(kRowHeight);
            const float bottomBlock =
                4.0f * spacingY + Px(10.0f) + 2.0f * menuRow + buttonHeight + Px(2.0f);

            ImGui::BeginChild("##folderlist", ImVec2(0, -bottomBlock),
                              ImGuiChildFlags_None,
                              g_sidebarCompact ? ImGuiWindowFlags_NoScrollbar
                                               : ImGuiWindowFlags_None);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + overhang);

            std::vector<std::string> monograms;
            if (g_sidebarCompact)
            {
                std::vector<std::string> names;
                names.reserve(folders.Folders.size());
                for (const Vault::FolderTally& row : folders.Folders)
                    names.push_back(row.Name);
                monograms = Vault::FolderMonograms(names);
            }

            for (std::size_t i = 0; i < folders.Folders.size(); ++i)
            {
                const Vault::FolderTally& row = folders.Folders[i];
                const bool selected = g_folderScope == FolderScope::One &&
                                      Vault::SameFolder(row.Name, g_folderName);
                ImGui::PushID(static_cast<int>(i));
                if (FolderRowWidget(ICON_FA_FOLDER, row.Name, row.Count,
                                    selected && !screenOpen, selected,
                                    i < monograms.size() ? monograms[i].c_str() : nullptr,
                                    FolderAccent(row)))
                    ShowFolder(doc, FolderScope::One, row.Name);
                if (const std::uint64_t dropped = EntryDroppedHere())
                    DropEntry(app, session, dropped, FolderScope::One, row.Name);

                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::Selectable(T(Str::Main_NewEntryInFolder)))
                    {
                        const std::string folder = row.Name;
                        Editor::RequestClose(doc, [&session, folder] {
                            ShowFolder(session.Vault().Doc(), FolderScope::One, folder);
                            Editor::BeginNew(folder);
                        });
                    }
                    ImGui::Separator();
                    if (ImGui::Selectable(T(Str::Main_RenameFolder)))
                    {
                        g_folderTarget = row.Name;
                        std::snprintf(g_folderNameBuf, sizeof(g_folderNameBuf),
                                      "%s", row.Name.c_str());
                        g_folderProblem.clear();
                        g_folderRenamePending = true;
                    }
                    if (ImGui::Selectable(T(Str::Main_DissolveFolder)))
                    {
                        g_folderTarget = row.Name;
                        g_folderDissolvePending = true;
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }

            if (folders.Unfiled != 0)
            {
                ImGui::PushID("unfiled");
                if (FolderRowWidget(ICON_FA_CIRCLE_MINUS, T(Str::Main_FolderUnfiled),
                                    folders.Unfiled,
                                    g_folderScope == FolderScope::Unfiled && !screenOpen,
                                    g_folderScope == FolderScope::Unfiled))
                    ShowFolder(doc, FolderScope::Unfiled, {});
                if (const std::uint64_t dropped = EntryDroppedHere())
                    DropEntry(app, session, dropped, FolderScope::Unfiled, {});
                ImGui::PopID();
            }

            ImGui::Dummy(ImVec2(0.0f, 0.0f));

            ScrollFade(Token::Panel);

            ImGui::PopStyleColor();
            ImGui::EndChild();

            SidebarDivider();
            MenuRowLook settingsLook;
            settingsLook.Selected = Settings::IsOpen();
            if (MenuRowWidget("##settings", ICON_FA_GEAR, T(Str::Main_Settings), settingsLook))
            {
                Editor::RequestClose(doc, [] {
                    Audit::Close();
                    Settings::Open();
                });
            }
            MenuRowLook helpLook;
            helpLook.KeyName = Shortcuts::KeyName(Shortcuts::Action::Help);
            if (MenuRowWidget("##help", ICON_FA_KEYBOARD, T(Str::Main_ShortcutsTitle), helpLook))
                g_helpPending = true;

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.16f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Color(Token::RedCore, 0.26f));
            constexpr ImVec2 kAlignCentre{ 0.5f, 0.5f };
            constexpr ImVec2 kAlignLeft{ 0.0f, 0.5f };
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,
                                g_sidebarCompact ? kAlignCentre : kAlignLeft);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(Px(12.0f), ImGui::GetStyle().FramePadding.y));
            char lockLabel[96];
            if (g_sidebarCompact)
                std::snprintf(lockLabel, sizeof(lockLabel), ICON_FA_LOCK "###lock");
            else
                std::snprintf(lockLabel, sizeof(lockLabel),
                              ICON_FA_LOCK "   %s###lock", T(Str::Main_LockButton));
            if (ImGui::Button(lockLabel, ImVec2(-1, buttonHeight)))
                LockFromUi(app, LockReason::Manual);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            if (g_sidebarCompact)
                RowTooltip(T(Str::Main_LockButton), Shortcuts::KeyName(Shortcuts::Action::Lock));
            else
            {
                const char* key = Shortcuts::KeyName(Shortcuts::Action::Lock);
                const float keySize = Px(Fonts::Sizes::Label);
                const ImVec2 kt = f.Mono->CalcTextSizeA(keySize, FLT_MAX, 0.0f, key);
                const ImVec2 bmax = ImGui::GetItemRectMax();
                const ImVec2 bmin = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    f.Mono, keySize,
                    ImVec2(bmax.x - Px(12.0f) - kt.x, (bmin.y + bmax.y - kt.y) * 0.5f),
                    Theme::U32(Token::TextMuted), key);
            }

            ImGui::PopStyleVar(2);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            g_sidebarCompact = false;
        }

        void DrawEmptyState(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, 60));

            const char* icon = ICON_FA_FOLDER;
            const ImVec2 t = f.Strong->CalcTextSizeA(Px(Fonts::Sizes::Display), FLT_MAX, 0.0f, icon);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (ImGui::GetContentRegionAvail().x - t.x) * 0.5f);
            ImGui::PushFont(f.Strong, Fonts::Sizes::Display);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::LineStrong));
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 14));
            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextWrapped("%s", T(Str::Main_VaultEmpty));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 16));
            char importLabel[96];
            std::snprintf(importLabel, sizeof(importLabel), ICON_FA_FILE_IMPORT "   %s",
                          T(Str::Main_ImportCsv));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::AccentCore));
            if (ImGui::Button(importLabel, Px(-1, 34)))
                Settings::StartImport();
            ImGui::PopStyleColor();
            ImGui::Dummy(Px(0, 6));
            ImGui::PushFont(f.Text, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::Main_ImportCsvHint));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 16));
            if (ImGui::Button(T(Str::Main_AddExampleEntries), Px(-1, 34)))
                AddSeedEntries(session);

            ImGui::Dummy(Px(0, 8));
            ImGui::PushFont(f.Text, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
            ImGui::TextWrapped("%s", T(Str::Main_ExampleEntriesHint));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        void DrawRowMenu(const Vault::Document& doc, const Entry& e)
        {
            const bool editing = Editor::IsOpen();
            auto want = [&e](RowAction action) {
                g_rowAction = action;
                g_rowActionTarget = e.Id;
            };
            char label[160];

            ImGui::BeginDisabled(editing);
            std::snprintf(label, sizeof(label), ICON_FA_PEN "   %s", T(Str::Main_EditButton));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::EditEntry)))
                want(RowAction::Edit);
            ImGui::EndDisabled();

            ImGui::Separator();
            const Field* pw = FindByRole(e, Vault::FieldRole::Password);
            const Field* user = FindByRole(e, Vault::FieldRole::Username);
            if (!user || user->Value.Empty())
                user = FindByRole(e, Vault::FieldRole::Email);
            std::snprintf(label, sizeof(label), ICON_FA_KEY "   %s", T(Str::Main_MenuCopyPassword));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::CopyPassword),
                                false, pw && !pw->Value.Empty()))
                want(RowAction::CopyPassword);
            std::snprintf(label, sizeof(label), ICON_FA_COPY "   %s", T(Str::Main_MenuCopyUsername));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::CopyUsername),
                                false, user && !user->Value.Empty()))
                want(RowAction::CopyUsername);
            std::snprintf(label, sizeof(label), ICON_FA_CLOCK "   %s", T(Str::Main_MenuCopyCode));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::CopyTwoFactor),
                                false, e.HasTotp))
                want(RowAction::CopyTwoFactor);
            std::snprintf(label, sizeof(label), ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "   %s",
                          T(Str::Main_OpenAndCopy));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::OpenLogin),
                                false, OpenableSite(e) != nullptr))
                want(RowAction::OpenLogin);

            ImGui::Separator();
            ImGui::BeginDisabled(editing);
            std::snprintf(label, sizeof(label), ICON_FA_STAR "   %s",
                          e.Favourite ? T(Str::Main_RemoveFavourite) : T(Str::Main_AddFavourite));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::ToggleFavourite)))
                want(RowAction::ToggleFavourite);

            std::snprintf(label, sizeof(label), ICON_FA_FOLDER "   %s", T(Str::Main_MenuMoveToFolder));
            if (ImGui::BeginMenu(label))
            {
                if (ImGui::MenuItem(T(Str::Editor_FolderNone), nullptr, e.Folder.empty()))
                {
                    want(RowAction::MoveToFolder);
                    g_rowActionFolder.clear();
                }
                const Vault::FolderSurvey folders = Vault::SurveyFolders(doc);
                if (!folders.Folders.empty())
                    ImGui::Separator();
                for (std::size_t i = 0; i < folders.Folders.size(); ++i)
                {
                    const Vault::FolderTally& row = folders.Folders[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::MenuItem(row.Name.c_str(), nullptr,
                                        Vault::SameFolder(row.Name, e.Folder)))
                    {
                        want(RowAction::MoveToFolder);
                        g_rowActionFolder = row.Name;
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            std::snprintf(label, sizeof(label), ICON_FA_PEN "   %s", T(Str::Main_MenuRename));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::Rename)))
                want(RowAction::Rename);
            std::snprintf(label, sizeof(label), ICON_FA_COPY "   %s", T(Str::Main_Duplicate));
            if (ImGui::MenuItem(label))
                want(RowAction::Duplicate);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            std::snprintf(label, sizeof(label), ICON_FA_TRASH "   %s", T(Str::Main_MenuDelete));
            if (ImGui::MenuItem(label, Shortcuts::KeyName(Shortcuts::Action::Delete)))
                want(RowAction::Delete);
            ImGui::PopStyleColor();
            ImGui::EndDisabled();

            if (editing)
            {
                ImGui::Separator();
                ImGui::TextDisabled("%s", T(Str::Main_MenuEditorOpen));
            }
        }

        void ColumnRules(bool left, bool right)
        {
            if (Theme::SeparatesByTone())
                return;
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 colour = Theme::U32(Token::Line);
            const float half = Px(1.0f) * 0.5f;
            dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), false);
            if (left)
                dl->AddLine(ImVec2(pos.x + half, pos.y), ImVec2(pos.x + half, pos.y + size.y),
                            colour, Px(1.0f));
            if (right)
                dl->AddLine(ImVec2(pos.x + size.x - half, pos.y),
                            ImVec2(pos.x + size.x - half, pos.y + size.y), colour, Px(1.0f));
            dl->PopClipRect();
        }

        void ListColumnRules()
        {
            ColumnRules(true, true);
        }

        void SetListOrder(Application& app, VaultSession& session, Vault::SortOrder order)
        {
            Vault::Document& doc = session.Vault().Doc();
            if (Vault::SortOrderFrom(doc.VaultSettings.ListOrder) == order)
                return;
            Vault::Settings draft = doc.VaultSettings;
            draft.ListOrder = static_cast<std::uint32_t>(order);
            Vault::SetVaultSettings(doc, draft);
            SaveNow(app, session);
            g_scrollToSelection = true;
        }

        void DrawListHeader(Application& app, VaultSession& session,
                            const std::vector<Vault::SearchHit>& hits)
        {
            const Fonts::Set& f = Fonts::Current();
            const Vault::Document& doc = session.Vault().Doc();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImGui::Dummy(Px(0, 6));
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float left = at.x + Px(12.0f);
            const float right = at.x + width - Px(8.0f);
            const float lineHeight = Px(28.0f);

            const Vault::SortOrder order = Vault::SortOrderFrom(doc.VaultSettings.ListOrder);
            ImGui::SetCursorScreenPos(ImVec2(right - lineHeight, at.y));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
            char sortTip[160];
            std::snprintf(sortTip, sizeof(sortTip), "%s: %s", T(Str::Main_SortTooltip),
                          order == Vault::SortOrder::Modified ? T(Str::Main_SortMenuModified)
                                                              : T(Str::Main_SortMenuName));
            if (UI::IconButton("###sortmenu-open", ICON_FA_SORT, ImVec2(lineHeight, lineHeight),
                               Token::TextSecondary, sortTip))
                ImGui::OpenPopup("##sortmenu");
            ImGui::PopStyleColor();
            if (ImGui::BeginPopup("##sortmenu"))
            {
                if (ImGui::MenuItem(T(Str::Main_SortMenuName), nullptr,
                                    order == Vault::SortOrder::Title))
                    SetListOrder(app, session, Vault::SortOrder::Title);
                if (ImGui::MenuItem(T(Str::Main_SortMenuModified), nullptr,
                                    order == Vault::SortOrder::Modified))
                    SetListOrder(app, session, Vault::SortOrder::Modified);
                ImGui::EndPopup();
            }

            const bool filtered = g_folderScope != FolderScope::All;
            char title[192];
            if (g_folderScope == FolderScope::Favourites)
                std::snprintf(title, sizeof(title), "%s", T(Str::Main_Favourites));
            else if (g_folderScope == FolderScope::Unfiled)
                std::snprintf(title, sizeof(title), "%s", T(Str::Main_FolderScopeUnfiled));
            else if (g_folderScope == FolderScope::One)
                std::snprintf(title, sizeof(title), "%s", g_folderName.c_str());
            else if (g_folderScope == FolderScope::Finding)
                std::snprintf(title, sizeof(title), T(Str::Main_ScopeFinding),
                              Audit::KindName(g_findingKind));
            else
                std::snprintf(title, sizeof(title), "%s", T(Str::Main_FolderAll));

            const float titleSize = Px(Fonts::Sizes::Title);
            const float closeSize = Px(22.0f);
            const float titleRoom = Max(Px(40.0f), right - lineHeight - Px(10.0f) -
                                                       (filtered ? closeSize + Px(6.0f) : 0.0f) - left);
            const std::string shownTitle = FitEnd(title, f.Strong, titleSize, titleRoom);
            const float titleWidth =
                f.Strong->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, shownTitle.c_str()).x;
            const ImVec2 titleAt(left, at.y + (lineHeight - titleSize) * 0.5f - Px(1.0f));
            dl->AddText(f.Strong, titleSize, titleAt, Theme::U32(Token::TextPrimary),
                        shownTitle.c_str());
            if (shownTitle != title &&
                ImGui::IsMouseHoveringRect(titleAt, ImVec2(titleAt.x + titleWidth,
                                                          titleAt.y + titleSize)) &&
                ImGui::IsWindowHovered())
                UI::Tooltip(title);

            if (filtered)
            {
                ImGui::SetCursorScreenPos(ImVec2(left + titleWidth + Px(6.0f),
                                                 at.y + (lineHeight - closeSize) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (UI::IconButton("###scopeclear", ICON_FA_XMARK,
                                   ImVec2(closeSize, closeSize), Token::TextMuted,
                                   T(Str::Main_ShowAllFolders)))
                    SelectFolder(doc, FolderScope::All, {});
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            std::size_t favourites = 0;
            for (const Vault::SearchHit& hit : hits)
                if (const Entry* e = Vault::FindEntry(doc, hit.EntryId); e && e->Favourite)
                    ++favourites;
            char entriesText[64];
            if (hits.size() == 1)
                std::snprintf(entriesText, sizeof(entriesText), "%s", T(Str::Main_CountEntryOne));
            else
                std::snprintf(entriesText, sizeof(entriesText), T(Str::Main_CountEntryMany),
                              hits.size());
            char favouritesText[64];
            if (favourites == 1)
                std::snprintf(favouritesText, sizeof(favouritesText), "%s",
                              T(Str::Main_CountFavouriteOne));
            else
                std::snprintf(favouritesText, sizeof(favouritesText),
                              T(Str::Main_CountFavouriteMany), favourites);
            const float labelSize = Px(Fonts::Sizes::Label);
            const float countsY = at.y + lineHeight + Px(2.0f);
            const std::string counts = UppercaseGerman(std::string(entriesText) +
                                                       "  \xc2\xb7  " + favouritesText);
            dl->AddText(f.Mono, labelSize, ImVec2(left, countsY), Theme::U32(Token::TextMuted),
                        FitEnd(counts, f.Mono, labelSize, right - left).c_str());

            const float chipHeight = Px(26.0f);
            const float chipY = countsY + labelSize + Px(12.0f);
            const float chipSize = Px(Fonts::Sizes::Small);
            float chipX = left;
            auto chip = [&](const char* id, const char* text, bool on) {
                const ImVec2 t = f.Medium->CalcTextSizeA(chipSize, FLT_MAX, 0.0f, text);
                const float w = t.x + Px(22.0f);
                ImGui::SetCursorScreenPos(ImVec2(chipX, chipY));
                const bool hit = ImGui::InvisibleButton(id, ImVec2(w, chipHeight));
                const bool hovered = ImGui::IsItemHovered();
                if (hovered)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const ImVec2 mn = ImGui::GetItemRectMin();
                const ImVec2 mx = ImGui::GetItemRectMax();
                const float round = Theme::Round(chipHeight * 0.5f);
                if (on)
                {
                    dl->AddRectFilled(mn, mx, Theme::U32(Token::AccentCore, 0.14f), round);
                    dl->AddRect(mn, mx, Theme::U32(Token::AccentCore, 0.65f), round, 0, 1.0f);
                }
                else
                {
                    if (hovered)
                        dl->AddRectFilled(mn, mx, Theme::U32(Token::Elevated, 0.60f), round);
                    dl->AddRect(mn, mx, Theme::U32(Token::Line), round, 0, 1.0f);
                }
                dl->AddText(f.Medium, chipSize,
                            ImVec2(mn.x + (w - t.x) * 0.5f, mn.y + (chipHeight - t.y) * 0.5f),
                            Theme::U32(on ? Token::AccentBright
                                          : (hovered ? Token::TextPrimary : Token::TextSecondary)),
                            text);
                chipX += w + Px(8.0f);
                return hit;
            };
            if (chip("##chip-all", T(Str::Main_ChipAll), g_folderScope == FolderScope::All))
                ShowFolder(doc, FolderScope::All, {});
            if (chip("##chip-favourites", T(Str::Main_ChipFavourites),
                     g_folderScope == FolderScope::Favourites))
                ShowFolder(doc,
                           g_folderScope == FolderScope::Favourites ? FolderScope::All
                                                                    : FolderScope::Favourites,
                           {});
            if (chip("##chip-recent", T(Str::Main_ChipRecent), order == Vault::SortOrder::Modified))
                SetListOrder(app, session,
                             order == Vault::SortOrder::Modified ? Vault::SortOrder::Title
                                                                 : Vault::SortOrder::Modified);

            ImGui::SetCursorScreenPos(ImVec2(at.x, chipY + chipHeight + Px(8.0f)));
        }

        void DrawEntryList(Application& app, VaultSession& session, float width, float height)
        {
            const Fonts::Set& f = Fonts::Current();
            const std::vector<Entry>& entries = session.Vault().Doc().Entries;
            (void)entries;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Color(Token::Surface));
            ImGui::BeginChild("##list", ImVec2(width, height), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            g_listEntrance.Begin(1, Px(6.0f), 1.0f);

            if (entries.empty())
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PxWhole(22, 10));
                ImGui::BeginChild("##empty", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
                DrawEmptyState(session);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ListColumnRules();
                g_listEntrance.End(Theme::Color(Token::Surface));
                ImGui::EndChild();
                ImGui::PopStyleColor();
                return;
            }

            const Visible visible = VisibleHits(session.Vault().Doc());
            const std::vector<Vault::SearchHit>& hits = visible.Hits;

            DrawListHeader(app, session, hits);

            const float footerHeight = Px(34.0f);
            ImGui::BeginChild("##rows", ImVec2(0, -footerHeight));

            const float gutter = Px(4.0f);
            const float spill = (ImGui::GetStyle().ItemSpacing.y - gutter) * 0.5f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gutter + spill);
            const float rowHeight = Px(46.0f);
            const float iconSize = Px(Fonts::Sizes::IconRow + 9.0f);

            const bool scrollToSelection = g_scrollToSelection;
            g_scrollToSelection = false;

            if (!g_multi.empty())
            {
                g_multi.erase(std::remove_if(g_multi.begin(), g_multi.end(),
                                             [&hits](std::uint64_t id) {
                                                 for (const Vault::SearchHit& h : hits)
                                                     if (h.EntryId == id)
                                                         return false;
                                                 return true;
                                             }),
                              g_multi.end());
                if (g_multi.size() < 2)
                    ClearMulti();
            }

            const std::string foldedQuery = Vault::FoldForSearch(g_search);

            if (hits.empty())
            {
                ImGui::Dummy(Px(0, 24));
                ImGui::PushFont(f.Text, Fonts::Sizes::Body);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(18));
                const bool filtered = g_folderScope != FolderScope::All;
                const char* empty = T(Str::Main_NoMatches);
                if (filtered && g_search[0])
                    empty = T(Str::Main_NoMatchesHere);
                else if (g_folderScope == FolderScope::Favourites)
                    empty = T(Str::Main_NoFavourites);
                else if (g_folderScope == FolderScope::Finding)
                    empty = T(Str::Main_FindingEmpty);
                else if (filtered)
                    empty = T(Str::Main_FolderEmpty);
                ImGui::TextWrapped("%s", empty);
                ImGui::PopStyleColor();
                ImGui::PopFont();

                if (filtered && visible.Outside > 0)
                {
                    ImGui::Dummy(Px(0, 8));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(18));
                    ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::Text(T(Str::Main_HitsElsewhere), visible.Outside);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();

                    ImGui::Dummy(Px(0, 6));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(18));
                    if (ImGui::Button(T(Str::Main_SearchEverywhere), Px(-18, 30)))
                        SelectFolder(session.Vault().Doc(), FolderScope::All, {});
                }
            }

            std::vector<std::string_view> subtitles;
            subtitles.reserve(hits.size());
            for (const Vault::SearchHit& hit : hits)
            {
                const Entry* entry = Vault::FindEntry(session.Vault().Doc(), hit.EntryId);
                const Field* user = entry ? FindLogin(*entry) : nullptr;
                subtitles.push_back(user ? user->Value.View() : std::string_view());
            }
            std::vector<std::string> shown = RedactAccountHints(subtitles);
            struct WipeOnExit
            {
                std::vector<std::string>& Strings;
                ~WipeOnExit()
                {
                    for (std::string& s : Strings)
                        WipeString(s);
                }
            } wipeShown{ shown };

            std::size_t row = 0;
            bool firstRow = true;
            bool previousPicked = false;
            for (const Vault::SearchHit& hit : hits)
            {
                const Entry* entry = Vault::FindEntry(session.Vault().Doc(), hit.EntryId);
                if (!entry)
                {
                    ++row;
                    continue;
                }
                const Entry& e = *entry;

                ImGui::PushID(static_cast<int>(hit.EntryId));
                const ImVec2 rowStart = ImGui::GetCursorScreenPos();
                const bool selected = (g_selectedId != 0 && g_selectedId == e.Id);
                const bool picked = selected || (MultiActive() && InMulti(e.Id));
                if (!firstRow && !picked && !previousPicked)
                {
                    const float y = std::floor(rowStart.y - ImGui::GetStyle().ItemSpacing.y * 0.5f) + 0.5f;
                    const float w = ImGui::GetContentRegionAvail().x;
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(rowStart.x + gutter + Px(12.0f), y),
                        ImVec2(rowStart.x + w - gutter - Px(12.0f), y),
                        Theme::U32(Token::Line, 0.75f), 1.0f);
                }
                firstRow = false;
                previousPicked = picked;

                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
                const bool rowClicked =
                    ImGui::Selectable("##row", selected,
                                      ImGuiSelectableFlags_AllowDoubleClick |
                                          ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0, rowHeight));
                const bool rowDoubleClicked =
                    rowClicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                const bool rowHovered = ImGui::IsItemHovered();
                const bool rowTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
                const ImVec2 afterRow = ImGui::GetCursorScreenPos();
                ImGui::PopStyleColor(3);

                if (!Editor::IsOpen() && ImGui::BeginDragDropSource())
                {
                    const std::uint64_t dragged = e.Id;
                    ImGui::SetDragDropPayload(kEntryPayload, &dragged, sizeof(dragged));
                    ImGui::TextUnformatted(e.Title.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !Editor::IsOpen())
                    g_selectedId = e.Id;
                if (ImGui::BeginPopupContextItem("##rowmenu"))
                {
                    DrawRowMenu(session.Vault().Doc(), e);
                    ImGui::EndPopup();
                }

                if (selected && scrollToSelection)
                    ImGui::SetScrollHereY(0.5f);

                const float rowWidth = ImGui::GetContentRegionAvail().x;

                const bool rowHot =
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                    !ImGui::GetDragDropPayload() &&
                    ImGui::IsMouseHoveringRect(rowStart,
                                               ImVec2(rowStart.x + rowWidth, rowStart.y + rowHeight));

                const float hoverFade =
                    Motion::Toward(ImGui::GetID("##fade-hover"), rowHovered || rowHot, 0.12f);
                const float pickFade = Motion::Toward(ImGui::GetID("##fade-pick"), picked, 0.16f);

                {
                    ImDrawList* bg = ImGui::GetWindowDrawList();
                    const float radius = Theme::Round(Px(6.0f));
                    const ImVec2 a(rowStart.x + gutter, rowStart.y - spill);
                    const ImVec2 b(rowStart.x + rowWidth - gutter,
                                   rowStart.y + rowHeight + spill);
                    if (pickFade > 0.0f)
                    {
                        bg->AddRectFilled(a, b, Theme::U32(Token::AccentCore, 0.12f * pickFade),
                                          radius);
                        bg->AddRect(a, b, Theme::U32(Token::AccentCore, 0.30f * pickFade), radius, 0,
                                    1.0f);
                    }
                    const float hoverWash = hoverFade * (1.0f - pickFade);
                    if (hoverWash > 0.0f)
                        bg->AddRectFilled(a, b, Theme::U32(Token::Elevated, 0.60f * hoverWash),
                                          radius);
                }

                const ImGuiIO& io = ImGui::GetIO();
                const bool extend = rowClicked && !Editor::IsOpen() && (io.KeyCtrl || io.KeyShift);
                if (extend && io.KeyShift)
                    SelectRangeTo(hits, e.Id);
                else if (extend)
                    ToggleInMulti(e.Id);
                else if (rowClicked)
                {
                    ClearMulti();
                    const std::uint64_t clicked = e.Id;
                    auto select = [clicked] {
                        g_selectedId = clicked;
                        g_leaveChoseEntry = true;
                        Audit::Close();
                        Settings::Close();
                    };
                    if (Editor::IsOpen() && Editor::EditingId() != clicked)
                        Editor::RequestClose(session.Vault().Doc(), select);
                    else
                        select();
                }

                if (rowDoubleClicked && !extend && !Editor::IsOpen() && g_selectedId == e.Id)
                    Editor::BeginEdit(e);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (pickFade > 0.0f)
                {
                    const float barInset = Px(9.0f);
                    const float half =
                        (rowHeight * 0.5f - barInset) * Curve::EaseOut(pickFade);
                    const float middle = rowStart.y + rowHeight * 0.5f;
                    dl->AddRectFilled(ImVec2(rowStart.x + Px(7), middle - half),
                                      ImVec2(rowStart.x + Px(10), middle + half),
                                      Theme::U32(Token::AccentCore, pickFade),
                                      Theme::Round(Px(2.0f)));
                }

                const ImVec2 iconPos(rowStart.x + Px(14), rowStart.y + (rowHeight - iconSize) * 0.5f);
                IconSpec spec = SpecFor(e);
                char monogram[5] = {};
                if (!HasGlyph(spec))
                {
                    DeriveMonogram(e.Title.c_str(), monogram, sizeof(monogram));
                    spec.Monogram = monogram;
                }
                DrawPlatformIconAt(dl, iconPos, iconSize, spec);

                const float textX = iconPos.x + iconSize + Px(12);

                const Field* pwField = FindByRole(e, Vault::FieldRole::Password);
                const bool hasPassword = pwField && !pwField->Value.Empty();
                const Field* userField = FindByRole(e, Vault::FieldRole::Username);
                if (!userField || userField->Value.Empty())
                    userField = FindByRole(e, Vault::FieldRole::Email);
                const bool hasUser = userField && !userField->Value.Empty();
                const bool hasSite = OpenableSite(e) != nullptr;

                const float button = Px(26.0f);
                const float buttonGap = Px(2.0f);
                const int buttonCount = (hasUser ? 1 : 0) + (hasPassword ? 1 : 0) +
                                        (e.HasTotp ? 1 : 0) + (hasSite ? 1 : 0);
                const float rowEnd = rowStart.x + rowWidth - gutter - Px(6.0f);
                const float chevronSize = Px(Fonts::Sizes::Small);
                const ImVec2 chevron =
                    f.Text->CalcTextSizeA(chevronSize, FLT_MAX, 0.0f, ICON_FA_CHEVRON_RIGHT);
                const float restRight = rowEnd - chevron.x - Px(10.0f);
                float clusterRight = restRight;
                float quickShown = 0.0f;
                if (buttonCount > 0)
                {
                    const float quickFade =
                        Motion::Toward(ImGui::GetID("##fade-quick"), rowHot, 0.10f);
                    quickShown = Curve::EaseOut(quickFade);
                    const float clusterWidth =
                        buttonCount * button + (buttonCount - 1) * buttonGap + Px(4.0f);
                    float x = rowEnd - clusterWidth + Px(4.0f);
                    clusterRight = std::floor(
                        Lerp(restRight, Min(restRight, rowEnd - clusterWidth), quickShown) + 0.5f);

                    if (rowHot)
                    {
                        const float y = rowStart.y + (rowHeight - button) * 0.5f;
                        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * quickShown);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::LineStrong, 0.55f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Color(Token::LineStrong, 0.85f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                        const double now = ImGui::GetTime();
                        char tip[160];
                        auto quick = [&](int which, const char* id, const char* icon, Str what,
                                         Shortcuts::Action key) {
                            const bool done = g_quickDone.Entry == e.Id &&
                                              g_quickDone.Which == which && now < g_quickDone.Until;
                            std::snprintf(tip, sizeof(tip), "%s   %s", T(what), Shortcuts::KeyName(key));
                            ImGui::SetCursorScreenPos(ImVec2(x, y));
                            const bool hit = UI::IconButton(id, done ? ICON_FA_CHECK : icon,
                                                            ImVec2(button, button),
                                                            done ? Token::GreenCore : Token::TextSecondary,
                                                            tip);
                            x += button + buttonGap;
                            return hit;
                        };
                        auto confirm = [&](int which, bool worked) {
                            if (worked)
                                g_quickDone = { e.Id, which, now + kQuickDoneSeconds };
                        };
                        if (hasUser && quick(0, "###quickuser", ICON_FA_USER, Str::Main_MenuCopyUsername,
                                             Shortcuts::Action::CopyUsername))
                            confirm(0, CopyUsernameOf(app, &e));
                        if (hasPassword && quick(1, "###quickpw", ICON_FA_KEY, Str::Main_MenuCopyPassword,
                                                 Shortcuts::Action::CopyPassword))
                            confirm(1, CopyPasswordOf(app, session, &e));
                        if (e.HasTotp && quick(2, "###quickcode", ICON_FA_CLOCK, Str::Main_MenuCopyCode,
                                               Shortcuts::Action::CopyTwoFactor))
                            confirm(2, CopyTwoFactorOf(app, &e));
                        if (hasSite && quick(3, "###quicksite", ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE,
                                             Str::Main_OpenAndCopy, Shortcuts::Action::OpenLogin))
                            confirm(3, OpenLoginOf(app, session, &e));
                        ImGui::PopStyleVar(2);
                        ImGui::PopStyleColor(3);
                        ImGui::SetCursorScreenPos(afterRow);
                    }
                }

                if (quickShown < 1.0f)
                    dl->AddText(f.Text, chevronSize,
                                ImVec2(rowEnd - chevron.x, rowStart.y + (rowHeight - chevron.y) * 0.5f),
                                Theme::U32(Token::TextMuted, 0.8f * (1.0f - quickShown)),
                                ICON_FA_CHEVRON_RIGHT);

                const float markSize = Px(Fonts::Sizes::Small);
                if (e.Favourite)
                {
                    const ImVec2 measured =
                        f.Text->CalcTextSizeA(markSize, FLT_MAX, 0.0f, ICON_FA_STAR);
                    clusterRight -= measured.x;
                    dl->AddText(f.Text, markSize,
                                ImVec2(clusterRight, rowStart.y + (rowHeight - measured.y) * 0.5f),
                                Theme::U32(Token::WarnCore, selected ? 1.0f : 0.70f),
                                ICON_FA_STAR);
                    clusterRight -= Px(8.0f);
                }

                if (const Vault::AuditFinding* finding = Audit::FindingFor(session, e.Id);
                    finding && Audit::IsSerious(*finding))
                {
                    const ImVec2 measured = f.Text->CalcTextSizeA(
                        markSize, FLT_MAX, 0.0f, ICON_FA_TRIANGLE_EXCLAMATION);
                    clusterRight -= measured.x;
                    const ImVec2 at(clusterRight, rowStart.y + (rowHeight - measured.y) * 0.5f);
                    dl->AddText(f.Text, markSize, at, Theme::U32(Token::WarnBright),
                                ICON_FA_TRIANGLE_EXCLAMATION);
                    if (rowTooltip &&
                        ImGui::IsMouseHoveringRect(ImVec2(at.x - Px(4), rowStart.y),
                                                   ImVec2(at.x + measured.x + Px(4),
                                                          rowStart.y + rowHeight)))
                    {
                        char why[256];
                        std::snprintf(why, sizeof(why), T(Str::Main_FindingTooltip),
                                      Audit::TagsOf(*finding).c_str());
                        UI::Tooltip(why);
                    }
                    clusterRight -= Px(8.0f);
                }

                const float textRight = Max(textX, clusterRight - Px(2.0f));
                const ImVec4 clip(textX, rowStart.y, textRight, rowStart.y + rowHeight);
                const std::string title =
                    FitEnd(e.Title, f.Medium, Px(Fonts::Sizes::Body), textRight - textX);

                std::size_t matchBegin = 0;
                std::size_t matchEnd = 0;
                if (!foldedQuery.empty() &&
                    Vault::FindFoldedMatch(e.Title, foldedQuery, matchBegin, matchEnd))
                {
                    const std::size_t visibleBytes =
                        title == e.Title ? title.size() : (title.size() >= 3 ? title.size() - 3 : 0);
                    if (matchBegin < visibleBytes)
                    {
                        matchEnd = std::min(matchEnd, visibleBytes);
                        const char* base = e.Title.c_str();
                        const float x0 = textX + f.Medium->CalcTextSizeA(
                                                     Px(Fonts::Sizes::Body), FLT_MAX, 0.0f,
                                                     base, base + matchBegin).x;
                        const float x1 = textX + f.Medium->CalcTextSizeA(
                                                     Px(Fonts::Sizes::Body), FLT_MAX, 0.0f,
                                                     base, base + matchEnd).x;
                        const float top = rowStart.y + Px(6);
                        dl->AddRectFilled(ImVec2(x0 - Px(1), top),
                                          ImVec2(std::min(x1 + Px(1), textRight),
                                                 top + Px(Fonts::Sizes::Body) + Px(3)),
                                          Theme::U32(Token::AccentCore, 0.30f),
                                          Theme::Round(Px(3.0f)));
                    }
                }

                dl->AddText(f.Medium, Px(Fonts::Sizes::Body), ImVec2(textX, rowStart.y + Px(7)),
                            Theme::U32(picked ? Token::TextPrimary : Token::TextSecondary),
                            title.c_str(), nullptr, 0.0f, &clip);
                if (rowTooltip && title != e.Title &&
                    ImGui::IsMouseHoveringRect(ImVec2(textX, rowStart.y),
                                               ImVec2(textRight, rowStart.y + rowHeight)))
                    UI::Tooltip(e.Title);

                if (row < shown.size() && !shown[row].empty())
                    dl->AddText(f.Mono, Px(Fonts::Sizes::Label), ImVec2(textX, rowStart.y + Px(26)),
                                Theme::U32(Token::TextMuted), shown[row].c_str(),
                                nullptr, 0.0f, &clip);

                ++row;

                ImGui::PopID();
            }

            if (!hits.empty())
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - spill);
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }

            ScrollFade(Token::Surface);
            ImGui::EndChild();

            {
                const ImVec2 rowsMax = ImGui::GetItemRectMax();
                const ImVec2 rowsMin = ImGui::GetItemRectMin();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float y = std::floor(rowsMax.y + Px(2.0f)) + 0.5f;
                dl->AddLine(ImVec2(rowsMin.x, y), ImVec2(rowsMax.x, y), Theme::U32(Token::Line), 1.0f);
                const float labelSize = Px(Fonts::Sizes::Label);
                const float shieldSize = Px(Fonts::Sizes::Small);
                const float textY = rowsMax.y + Px(2.0f) + (footerHeight - labelSize) * 0.5f;
                const float x = rowsMin.x + Px(14.0f);
                dl->AddText(f.Text, shieldSize, ImVec2(x, textY - (shieldSize - labelSize) * 0.5f),
                            Theme::U32(Token::GreenCore, 0.85f), ICON_FA_SHIELD_HALVED);
                const float textX = x + f.Text->CalcTextSizeA(shieldSize, FLT_MAX, 0.0f,
                                                              ICON_FA_SHIELD_HALVED).x + Px(8.0f);
                dl->AddText(f.Mono, labelSize, ImVec2(textX, textY), Theme::U32(Token::TextMuted),
                            FitEnd(T(Str::Main_ListFooter), f.Mono, labelSize,
                                   rowsMax.x - textX - Px(8.0f)).c_str());
            }

            ListColumnRules();
            g_listEntrance.End(Theme::Color(Token::Surface));
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        constexpr double kRevealCapSeconds = 10.0;

        bool RowButton(const char* icon, const char* id,
                       Token colour = Token::TextMuted)
        {
            const float height = ImGui::GetFrameHeight();
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
            const bool hit = UI::IconButton(id, icon,
                                            ImVec2(height + Px(8.0f), height), colour);
            ImGui::PopStyleColor();
            return hit;
        }

        float RevealedTagRoom()
        {
            const Fonts::Set& f = Fonts::Current();
            return f.Strong->CalcTextSizeA(Px(Fonts::Sizes::Label), FLT_MAX, 0.0f,
                                           T(Str::Main_Revealed)).x + Px(8.0f);
        }

        std::uint64_t g_largeEntry = 0;
        std::size_t g_largeIndex = static_cast<std::size_t>(-1);
        double g_largeStarted = 0.0;

        void DrawLargeSecret(std::string_view value)
        {
            const Fonts::Set& f = Fonts::Current();
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImGuiViewport* vp = ImGui::GetMainViewport();

            std::size_t count = 0;
            for (std::size_t i = 0; i < value.size(); ++count)
                i += Utf8SequenceLength(static_cast<unsigned char>(value[i]));
            if (count == 0)
                return;

            const float glyph = Px(32.0f);
            const float cellW = Px(40.0f);
            const float cellH = Px(62.0f);
            const float pad = Px(20.0f);
            const int fit = static_cast<int>((vp->Size.x - pad * 4.0f) / cellW);
            const int perRow = std::max(1, std::min({ static_cast<int>(count), 16, fit }));
            const int rows = static_cast<int>((count + static_cast<std::size_t>(perRow) - 1) /
                                              static_cast<std::size_t>(perRow));
            const ImVec2 size(perRow * cellW + pad * 2.0f, rows * cellH + pad * 2.0f);
            const ImVec2 min(vp->Pos.x + (vp->Size.x - size.x) * 0.5f,
                             vp->Pos.y + (vp->Size.y - size.y) * 0.5f);
            const ImVec2 max(min.x + size.x, min.y + size.y);

            dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                              Theme::U32(Token::Shade, 0.55f));
            dl->AddRectFilled(min, max, Theme::U32(Token::Panel), Theme::Round(Px(10.0f)));
            dl->AddRect(min, max, Theme::U32(Token::LineStrong), Theme::Round(Px(10.0f)), 0,
                        Px(1.0f));

            char one[8] = {};
            char number[8] = {};
            std::size_t at = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                const unsigned char lead = static_cast<unsigned char>(value[at]);
                const std::size_t len =
                    std::min<std::size_t>(Utf8SequenceLength(lead), value.size() - at);
                std::memcpy(one, value.data() + at, std::min<std::size_t>(len, sizeof(one) - 1));
                one[std::min<std::size_t>(len, sizeof(one) - 1)] = 0;
                at += len;

                const int col = static_cast<int>(i % static_cast<std::size_t>(perRow));
                const int row = static_cast<int>(i / static_cast<std::size_t>(perRow));
                const ImVec2 c0(min.x + pad + col * cellW + Px(2), min.y + pad + row * cellH);
                const ImVec2 c1(c0.x + cellW - Px(4), c0.y + cellH - Px(6));
                dl->AddRectFilled(c0, c1, Theme::U32(Token::Elevated, (i % 2) ? 0.35f : 0.65f),
                                  Theme::Round(Px(5.0f)));

                Token colour = Token::WarnBright;
                if (lead >= '0' && lead <= '9')
                    colour = Token::AccentBright;
                else if ((lead >= 'a' && lead <= 'z') || (lead >= 'A' && lead <= 'Z'))
                    colour = Token::TextPrimary;

                if (lead == ' ')
                {
                    const float y = c0.y + Px(8) + glyph * 0.9f;
                    dl->AddRectFilled(ImVec2(c0.x + Px(8), y), ImVec2(c1.x - Px(8), y + Px(3)),
                                      Theme::U32(Token::TextMuted), Theme::Round(Px(1.0f)));
                }
                else
                {
                    const ImVec2 m = f.Mono->CalcTextSizeA(glyph, FLT_MAX, 0.0f, one);
                    dl->AddText(f.Mono, glyph,
                                ImVec2((c0.x + c1.x - m.x) * 0.5f, c0.y + Px(6)),
                                Theme::U32(colour), one);
                }

                std::snprintf(number, sizeof(number), "%zu", i + 1);
                const ImVec2 n = f.Text->CalcTextSizeA(Px(Fonts::Sizes::Label), FLT_MAX, 0.0f, number);
                dl->AddText(f.Text, Px(Fonts::Sizes::Label),
                            ImVec2((c0.x + c1.x - n.x) * 0.5f, c1.y - n.y - Px(4)),
                            Theme::U32(Token::TextMuted), number);
            }
            sodium_memzero(one, sizeof(one));
        }

        constexpr float kCardGap = 12.0f;
        constexpr float kTileGap = 10.0f;
        constexpr float kTwoColumnsFrom = 760.0f;
        constexpr float kTileMinWidth = 240.0f;

        float BulletsWidth()
        {
            return Fonts::Current().Mono->CalcTextSizeA(Px(Fonts::Sizes::Body), FLT_MAX, 0.0f,
                                                        "••••••••••••").x;
        }

        void DrawFieldTile(Application& app, const Field& field, std::uint64_t entryId,
                           std::size_t index, const char* note, Token noteColour)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushID(static_cast<int>(index));

            const bool secret = field.IsSecret();
            const bool redacted = !secret &&
                                  (field.Role == Vault::FieldRole::Username ||
                                   field.Role == Vault::FieldRole::Email);
            const bool hidden = secret || redacted;

            const bool holdingThis = g_revealEntry == entryId && g_revealIndex == index;
            const bool revealed = hidden && holdingThis &&
                                  (ImGui::GetTime() - g_revealStarted) < kRevealCapSeconds;

            const ImVec2 top = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            const float labelSize = Px(Fonts::Sizes::Label);
            const char* corner = revealed ? T(Str::Main_Revealed) : note;
            float cornerRoom = 0.0f;
            if (corner && *corner)
            {
                const float w = f.Strong->CalcTextSizeA(labelSize, FLT_MAX, 0.0f, corner).x;
                ImGui::GetWindowDrawList()->AddText(
                    f.Strong, labelSize, ImVec2(top.x + avail - w, top.y),
                    Theme::U32(revealed ? Token::WarnBright : noteColour), corner);
                cornerRoom = w + Px(8.0f);
            }

            ImGui::PushFont(f.Text, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            const std::string label = UppercaseGerman(
                field.Role == Vault::FieldRole::Custom
                    ? field.Key
                    : T(FieldRoleText(field.Role)));
            ImGui::TextUnformatted(
                FitEnd(label, f.Text, labelSize, Max(Px(20.0f), avail - cornerRoom)).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 1));

            const bool openable = !hidden && IsOpenableWebLink(field.Value.View());
            const float rowButton = ImGui::GetFrameHeight() + Px(8.0f);
            const float smallGap = Px(4.0f);
            int buttonCount = 1;
            if (hidden)
                ++buttonCount;
            if (secret)
                ++buttonCount;
            if (openable)
                ++buttonCount;
            const float buttons = buttonCount * rowButton + (buttonCount - 1) * smallGap;
            const ImVec2 line = ImGui::GetCursorScreenPos();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float valueNeeds = BulletsWidth();
            const bool stacked = avail - buttons - spacing < valueNeeds;
            const float valueRoom = stacked ? avail : Max(Px(60.0f), avail - buttons - spacing);

            ImFont* valueFont = secret ? f.Mono : f.Text;
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(valueFont, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  Theme::Color(revealed ? Token::WarnBright : Token::TextPrimary));
            if (!hidden || revealed)
            {
                WipedText shown;
                TextView(FitEndInto(shown, field.Value.View(), valueFont,
                                    Px(Fonts::Sizes::Body), valueRoom));
                if (!hidden && shown.Truncated() && ImGui::IsItemHovered())
                    UI::Tooltip(std::string_view(field.Value.CStr(), field.Value.Size()));
            }
            else if (secret)
            {
                ImGui::TextUnformatted("••••••••••••");
            }
            else
            {
                WipedText hint;
                hint.Take(RedactAccountHint(field.Value.View(), kRedactKeepDefault));
                WipedText shownHint;
                TextView(FitEndInto(shownHint, hint.Str(), valueFont,
                                    Px(Fonts::Sizes::Body), valueRoom));
            }
            ImGui::PopStyleColor();
            ImGui::PopFont();

            if (stacked)
            {
                ImGui::SetCursorScreenPos(ImVec2(line.x + Max(0.0f, avail - buttons),
                                                 ImGui::GetCursorScreenPos().y));
            }
            else
            {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(line.x + avail - buttons, line.y));
            }

            if (hidden)
            {
                RowButton(revealed ? ICON_FA_EYE_SLASH : ICON_FA_EYE, "###reveal",
                          revealed ? Token::WarnBright : Token::TextMuted);
                if (ImGui::IsItemActive())
                {
                    if (!holdingThis)
                    {
                        g_revealEntry = entryId;
                        g_revealIndex = index;
                        g_revealStarted = ImGui::GetTime();
                    }
                }
                else if (holdingThis)
                {
                    StopRevealing();
                }
                ImGui::SameLine(0, smallGap);
            }

            if (secret)
            {
                const bool largeThis = g_largeEntry == entryId && g_largeIndex == index;
                RowButton(ICON_FA_TEXT_HEIGHT, "###large",
                          largeThis ? Token::WarnBright : Token::TextMuted);
                if (ImGui::IsItemActive())
                {
                    if (!largeThis)
                    {
                        g_largeEntry = entryId;
                        g_largeIndex = index;
                        g_largeStarted = ImGui::GetTime();
                    }
                    if (ImGui::GetTime() - g_largeStarted < kRevealCapSeconds)
                        DrawLargeSecret(field.Value.View());
                }
                else
                {
                    if (largeThis)
                    {
                        g_largeEntry = 0;
                        g_largeIndex = static_cast<std::size_t>(-1);
                    }
                    if (ImGui::IsItemHovered())
                        UI::Tooltip(T(Str::Main_ShowLarge));
                }
                ImGui::SameLine(0, smallGap);
            }

            if (RowButton(ICON_FA_COPY, "###copy"))
            {
                const std::uint32_t seconds = Vault::ClipboardSeconds(
                    app.Session().Vault().Doc().VaultSettings);
                if (secret)
                    app.Clipboard().CopySecret(field.Value.CStr(), field.Value.Size(),
                                               static_cast<double>(seconds), Application::Now());
                else
                    app.Clipboard().CopyPlain(field.Value.CStr(), field.Value.Size());
                app.Clipboard().SetLastLabel(field.Key);
                if (!secret)
                    NoticeCopied(field.Key.c_str());
            }

            if (openable)
            {
                ImGui::SameLine(0, smallGap);
                if (RowButton(ICON_FA_GLOBE, "###open"))
                    OpenWebLink(field.Value.View());
                if (ImGui::IsItemHovered())
                    UI::Tooltip(T(Str::Main_OpenLink));
            }

            ImGui::PopID();
        }

        void DrawFieldTiles(Application& app, const Entry& e,
                            const std::vector<std::size_t>& fields,
                            const char* passwordNote, Token passwordNoteColour)
        {
            if (fields.empty())
                return;
            const float avail = ImGui::GetContentRegionAvail().x;
            const float gap = Px(kTileGap);
            const int fit = static_cast<int>((avail + gap) / (Px(kTileMinWidth) + gap));
            const int perRow =
                Clamp(Min(fit, static_cast<int>(fields.size())), 1, 3);
            const float tileWidth = std::floor((avail - gap * (perRow - 1)) / perRow);

            const ImVec2 start = ImGui::GetCursorScreenPos();
            float rowTop = start.y;
            float rowBottom = start.y;
            for (std::size_t n = 0; n < fields.size(); ++n)
            {
                const int column = static_cast<int>(n % static_cast<std::size_t>(perRow));
                if (column == 0 && n > 0)
                    rowTop = rowBottom + gap;
                ImGui::SetCursorScreenPos(
                    ImVec2(start.x + column * (tileWidth + gap), rowTop));
                const std::size_t index = fields[n];
                const Field& field = e.Fields[index];
                char id[32];
                std::snprintf(id, sizeof(id), "##tile%zu", index);
                BeginBox(id, tileWidth, TilePadding());
                DrawFieldTile(app, field, e.Id, index,
                              field.Role == Vault::FieldRole::Password ? passwordNote : nullptr,
                              passwordNoteColour);
                EndTile();
                rowBottom = Max(rowBottom, ImGui::GetItemRectMax().y);
            }
            ImGui::SetCursorScreenPos(ImVec2(start.x, rowBottom));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }

        std::uint64_t g_codeRevealEntry = 0;
        std::size_t g_codeRevealIndex = static_cast<std::size_t>(-1);

        std::uint64_t g_listRevealEntry = 0;
        std::size_t g_listRevealIndex = static_cast<std::size_t>(-1);

        void StopListReveal()
        {
            g_listRevealEntry = 0;
            g_listRevealIndex = static_cast<std::size_t>(-1);
        }

        constexpr std::size_t kListCollapsedRows = 10;
        std::uint64_t g_listExpandedEntry = 0;
        std::uint32_t g_listExpandedKinds = 0;

        constexpr std::size_t kArmWholeList = static_cast<std::size_t>(-2);
        constexpr double kArmWakeSeconds = 0.35;
        constexpr double kArmExpirySeconds = 8.0;

        std::uint64_t g_codeArmEntry = 0;
        std::size_t g_codeArmIndex = static_cast<std::size_t>(-1);
        double g_codeArmStarted = 0.0;

        void DisarmCode()
        {
            g_codeArmEntry = 0;
            g_codeArmIndex = static_cast<std::size_t>(-1);
        }

        void ClearSelection()
        {
            g_selectedId = 0;
            g_duplicateFailedFor = 0;
            g_scrollToSelection = false;
            StopRevealing();
            g_codeRevealEntry = 0;
            g_codeRevealIndex = static_cast<std::size_t>(-1);
            StopListReveal();
            g_listExpandedEntry = 0;
            g_listExpandedKinds = 0;
            DisarmCode();
            ClearMulti();
        }

        void ResetUiState()
        {
            ClearSelection();
            g_itemActiveLastFrame = false;
            g_popupOpenLastFrame = false;
            g_search[0] = 0;
            g_focusSearch = false;
            g_folderScope = FolderScope::All;
            g_folderName.clear();
            ForgetFolderDialogs();
            ForgetFolderJump();
            ForgetRowMenu();
            Audit::Close();
            Audit::Forget();
            g_quitRequested = false;
            g_notice.clear();
            g_noticeUntil = 0.0;
            g_findingKind = Audit::FindingKind::None;
            g_findingIds.clear();
            ClearMulti();
            Trash::Forget();
            ForgetPalette();
            g_detailEntrance.Forget();
            g_listEntrance.Forget();
            g_quickDone = {};
        }

        void ArmCode(std::uint64_t entryId, std::size_t index)
        {
            g_codeArmEntry = entryId;
            g_codeArmIndex = index;
            g_codeArmStarted = ImGui::GetTime();
            g_codeRevealEntry = 0;
            g_codeRevealIndex = static_cast<std::size_t>(-1);
        }

        bool CodeArmed(std::uint64_t entryId, std::size_t index)
        {
            return g_codeArmEntry == entryId && g_codeArmIndex == index &&
                   (ImGui::GetTime() - g_codeArmStarted) < kArmExpirySeconds;
        }

        bool DrawArmedPair(const char* confirmLabel)
        {
            const float height = ImGui::GetFrameHeight();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
            char cancelLabel[96];
            std::snprintf(cancelLabel, sizeof(cancelLabel),
                          ICON_FA_XMARK "  %s###armcancel", T(Str::Main_Cancel));
            const bool cancel = ImGui::Button(cancelLabel,
                                              ImVec2(0, height));
            ImGui::PopStyleColor(2);
            if (cancel)
            {
                DisarmCode();
                return false;
            }

            ImGui::SameLine(0, Px(6));
            const bool awake = (ImGui::GetTime() - g_codeArmStarted) >= kArmWakeSeconds;
            ImGui::BeginDisabled(!awake);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::RedCore, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.32f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Color(Token::RedCore, 0.44f));
            const bool hit = ImGui::Button(confirmLabel, ImVec2(0, height));
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();
            return hit;
        }

        void DrawTotpPanel(Application& app, const Entry& e)
        {
            const Fonts::Set& f = Fonts::Current();
            const char* title = T(Str::Main_CardTwoFactor);

            const std::uint64_t millis = Vault::SystemUtcMillis();
            const std::int64_t seconds = static_cast<std::int64_t>(millis / 1000ull);
            const std::uint32_t period = e.Totp.Config.Period;

            const auto code = TotpFromConfig(e.Totp.Config, seconds);
            if (!code.IsOk())
            {
                CardHeader(ICON_FA_CLOCK, title, T(Str::Main_CardTotpBadge));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s",
                                   ToUserMessage(code.Code()));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                return;
            }

            std::string grouped;
            for (std::size_t i = 0; i < code.Value().size(); ++i)
            {
                if (i > 0 && i % 3 == 0)
                    grouped.push_back(' ');
                grouped.push_back(code.Value()[i]);
            }

            const std::uint32_t left = SecondsRemaining(seconds, period);
            const double elapsed =
                static_cast<double>(millis % (static_cast<std::uint64_t>(period) * 1000ull));
            const float fraction =
                1.0f - static_cast<float>(elapsed / (period * 1000.0));

            const bool expiring = left <= 5;
            const Token accent = expiring ? Token::RedCore : Token::AccentCore;

            ImVec2 origin = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            const float right = origin.x + avail;
            const float ringRadius = Px(15.0f);
            const float ringStroke = Px(3.0f);
            const float ringWidth = ringRadius * 2.0f + ringStroke;
            const float copyWidth = ImGui::GetFrameHeight() + Px(8.0f);
            const float codeSize = Px(Fonts::Sizes::Title);
            const float rowHeight = Max(ringWidth, codeSize * 1.3f);
            const ImVec2 codeExtent =
                f.Mono->CalcTextSizeA(codeSize, FLT_MAX, 0.0f, grouped.c_str());

            const float codeGap = Px(16.0f);
            const float ringGap = Px(12.0f);
            const float group = codeExtent.x + codeGap + ringWidth + ringGap + copyWidth;

            const char* badge = T(Str::Main_CardTotpBadge);
            const float titleSize = Px(Fonts::Sizes::Body);
            const float badgeSize = Px(Fonts::Sizes::Label);
            const float iconWidth =
                f.Text->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, ICON_FA_CLOCK).x;
            const float textWidth = f.Strong->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title).x;
            const float badgeWidth = f.Mono->CalcTextSizeA(badgeSize, FLT_MAX, 0.0f, badge).x;
            const float titleWidth = iconWidth + Px(9.0f) + textWidth + Px(10.0f) + badgeWidth;
            const bool oneLine = titleWidth + Px(20.0f) + group <= avail;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (oneLine)
            {
                const float y = origin.y + (rowHeight - titleSize) * 0.5f;
                float x = origin.x;
                dl->AddText(f.Text, titleSize, ImVec2(x, y), Theme::U32(Token::AccentCore),
                            ICON_FA_CLOCK);
                x += iconWidth + Px(9.0f);
                dl->AddText(f.Strong, titleSize, ImVec2(x, y), Theme::U32(Token::TextPrimary), title);
                x += textWidth + Px(10.0f);
                dl->AddText(f.Mono, badgeSize, ImVec2(x, y + (titleSize - badgeSize) * 0.55f),
                            Theme::U32(Token::TextMuted), badge);
            }
            else
            {
                CardHeader(ICON_FA_CLOCK, title, badge);
                origin = ImGui::GetCursorScreenPos();
            }

            const float codeX = oneLine ? right - group : origin.x;
            dl->AddText(f.Mono, codeSize,
                        ImVec2(codeX, origin.y + (rowHeight - codeExtent.y) * 0.5f),
                        Theme::U32(expiring ? Token::RedBright : Token::TextPrimary),
                        grouped.c_str());

            ImGui::SetCursorScreenPos(ImVec2(codeX, origin.y));
            ImGui::Dummy(ImVec2(codeExtent.x, rowHeight));
            if ((!e.Totp.Config.Issuer.empty() || !e.Totp.Config.AccountName.empty()) &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                std::string who = e.Totp.Config.Issuer;
                if (!e.Totp.Config.AccountName.empty())
                    who += (who.empty() ? "" : " · ") + e.Totp.Config.AccountName;
                UI::Tooltip(who);
            }

            const ImVec2 ringCentre(right - copyWidth - ringGap - ringRadius - ringStroke * 0.5f,
                                    origin.y + rowHeight * 0.5f);
            dl->AddCircle(ringCentre, ringRadius, Theme::U32(Token::Line), 48, ringStroke);
            if (fraction > 0.0f)
            {
                const float start = -1.5707963f;
                dl->PathArcTo(ringCentre, ringRadius, start,
                              start + 6.2831853f * fraction, 48);
                dl->PathStroke(Theme::U32(accent), 0, ringStroke);
            }
            {
                char secs[8];
                std::snprintf(secs, sizeof(secs), "%u", left);
                const ImVec2 t =
                    f.Medium->CalcTextSizeA(Px(Fonts::Sizes::Small), FLT_MAX, 0.0f, secs);
                dl->AddText(f.Medium, Px(Fonts::Sizes::Small),
                            ImVec2(ringCentre.x - t.x * 0.5f, ringCentre.y - t.y * 0.5f),
                            Theme::U32(accent), secs);
            }

            ImGui::SetCursorScreenPos(ImVec2(right - copyWidth,
                                             origin.y + (rowHeight - ImGui::GetFrameHeight()) * 0.5f));
            if (RowButton(ICON_FA_COPY, "###totpcopy"))
            {
                app.Clipboard().CopySecret(code.Value().c_str(), code.Value().size(),
                                           static_cast<double>(left), Application::Now());
                app.Clipboard().SetLastLabel(T(Str::Main_ClipboardTwoFactor));
            }

            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rowHeight));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }

        bool CopyTwoFactorOf(Application& app, const Entry* e)
        {
            if (!e)
            {
                Notice(T(Str::Main_NothingSelectedToCopy), true);
                return false;
            }
            if (!e->HasTotp)
            {
                Notice(T(Str::Main_NoTwoFactorCode), true);
                return false;
            }
            const std::int64_t seconds =
                static_cast<std::int64_t>(Vault::SystemUtcMillis() / 1000ull);
            const auto code = TotpFromConfig(e->Totp.Config, seconds);
            if (!code.IsOk())
            {
                Notice(ToUserMessage(code.Code()), true);
                return false;
            }
            const std::uint32_t left = SecondsRemaining(seconds, e->Totp.Config.Period);
            app.Clipboard().CopySecret(code.Value().c_str(), code.Value().size(),
                                       static_cast<double>(left), Application::Now());
            app.Clipboard().SetLastLabel(T(Str::Main_ClipboardTwoFactor));
            return true;
        }

        void DrawRecoveryCodes(Application& app, VaultSession& session, const Entry& e)
        {
            const Fonts::Set& f = Fonts::Current();

            std::size_t pendingDelete = static_cast<std::size_t>(-1);
            bool pendingClear = false;

            const float rowWidth = ImGui::GetContentRegionAvail().x;
            const float rowButton = ImGui::GetFrameHeight() + Px(8.0f);
            const float smallGap = Px(4.0f);
            const float rowButtons = rowButton * 4.0f + smallGap * 3.0f;

            for (std::size_t i = 0; i < e.RecoveryCodes.size(); ++i)
            {
                const Vault::RecoveryCode& c = e.RecoveryCodes[i];
                char boxId[32];
                std::snprintf(boxId, sizeof(boxId), "##code%zu", i);
                BeginBox(boxId, rowWidth, Px(10.0f, 6.0f));
                ImGui::PushID(static_cast<int>(1000 + i));
                const ImVec2 line = ImGui::GetCursorScreenPos();
                const float inner = ImGui::GetContentRegionAvail().x;

                const bool armed = CodeArmed(e.Id, i);
                const bool holding = g_codeRevealEntry == e.Id && g_codeRevealIndex == i;
                const bool revealed = holding &&
                    (ImGui::GetTime() - g_revealStarted) < kRevealCapSeconds;

                ImGui::AlignTextToFramePadding();
                ImGui::PushFont(f.Mono, Fonts::Sizes::Body);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(armed ? Token::RedBright
                                                         : (c.IsUsed() ? Token::TextDisabled
                                                            : (revealed ? Token::WarnBright
                                                                        : Token::TextPrimary))));
                ImGui::TextUnformatted(revealed ? c.Value.CStr() : "••••••••••••");
                ImGui::PopStyleColor();
                ImGui::PopFont();

                ImGui::SameLine();
                if (armed)
                {
                    char deleteLabel[96];
                    std::snprintf(deleteLabel, sizeof(deleteLabel),
                                  ICON_FA_TRASH "  %s###armconfirm", T(Str::Main_DeleteCode));
                    if (DrawArmedPair(deleteLabel))
                        pendingDelete = i;
                    ImGui::PopID();
                    EndTile();
                    continue;
                }

                if (inner - rowButtons - ImGui::GetStyle().ItemSpacing.x < BulletsWidth())
                    ImGui::SetCursorScreenPos(
                        ImVec2(line.x + Max(0.0f, inner - rowButtons),
                               line.y + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y));
                else
                    ImGui::SetCursorScreenPos(ImVec2(line.x + inner - rowButtons, line.y));
                RowButton(revealed ? ICON_FA_EYE_SLASH : ICON_FA_EYE,
                          "###codereveal",
                          revealed ? Token::WarnBright : Token::TextMuted);
                if (ImGui::IsItemActive())
                {
                    if (!holding)
                    {
                        g_codeRevealEntry = e.Id;
                        g_codeRevealIndex = i;
                        g_revealStarted = ImGui::GetTime();
                    }
                }
                else if (holding)
                {
                    g_codeRevealEntry = 0;
                    g_codeRevealIndex = static_cast<std::size_t>(-1);
                }

                ImGui::SameLine(0, Px(4));
                if (RowButton(ICON_FA_COPY, "###codecopy"))
                {
                    const std::uint32_t seconds = Vault::ClipboardSeconds(
                        app.Session().Vault().Doc().VaultSettings);
                    app.Clipboard().CopySecret(c.Value.CStr(), c.Value.Size(),
                                               static_cast<double>(seconds), Application::Now());
                    app.Clipboard().SetLastLabel(T(Str::Main_ClipboardRecoveryCode));
                }

                ImGui::SameLine(0, Px(4));
                if (RowButton(c.IsUsed() ? ICON_FA_ROTATE : ICON_FA_CHECK,
                              "###codeused",
                              c.IsUsed() ? Token::TextMuted : Token::GreenCore))
                {
                    Vault::SetRecoveryCodeUsed(session.Vault().Doc(), e.Id, i,
                                               !c.IsUsed(), Vault::SystemUtcNow());
                    SaveNow(app, session);
                }
                if (ImGui::IsItemHovered())
                    UI::Tooltip(c.IsUsed() ? T(Str::Main_CodeMarkOpen)
                                                       : T(Str::Main_CodeMarkUsed));

                ImGui::SameLine(0, smallGap);
                if (RowButton(ICON_FA_TRASH, "###codedelete", Token::RedCore))
                    ArmCode(e.Id, i);
                if (ImGui::IsItemHovered())
                    UI::Tooltip(T(Str::Main_CodeRemove));

                ImGui::PopID();
                EndTile();
            }

            ImGui::Dummy(Px(0, 2));
            if (CodeArmed(e.Id, kArmWholeList))
            {
                char confirmText[64];
                std::snprintf(confirmText, sizeof(confirmText),
                              T(Str::Main_DeleteAllCodes), e.RecoveryCodes.size());
                char confirm[80];
                std::snprintf(confirm, sizeof(confirm),
                              ICON_FA_TRASH "  %s###armconfirm", confirmText);
                if (DrawArmedPair(confirm))
                    pendingClear = true;
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
                char clearLabel[96];
                std::snprintf(clearLabel, sizeof(clearLabel),
                              ICON_FA_TRASH "  %s", T(Str::Main_ClearList));
                if (ImGui::Button(clearLabel,
                                  ImVec2(0, ImGui::GetFrameHeight())))
                    ArmCode(e.Id, kArmWholeList);
                ImGui::PopStyleColor(2);
            }

            if (pendingClear)
            {
                Vault::ClearRecoveryCodes(session.Vault().Doc(), e.Id,
                                          Vault::SystemUtcNow());
                DisarmCode();
                SaveNow(app, session);
            }
            else if (pendingDelete != static_cast<std::size_t>(-1))
            {
                Vault::DeleteRecoveryCode(session.Vault().Doc(), e.Id, pendingDelete,
                                          Vault::SystemUtcNow());
                DisarmCode();
                SaveNow(app, session);
            }
        }

        void DrawListItemRow(Application& app, const Vault::ListItem& item,
                             std::uint64_t entryId, std::size_t index, float labelWanted)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushID(static_cast<int>(index));

            const bool holding = g_listRevealEntry == entryId && g_listRevealIndex == index;
            const bool revealed = holding &&
                (ImGui::GetTime() - g_revealStarted) < kRevealCapSeconds;

            const float rowButton = ImGui::GetFrameHeight() + Px(8.0f);
            const float smallGap = Px(4.0f);
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float avail = ImGui::GetContentRegionAvail().x;
            const float rightRoom = rowButton * 2.0f + smallGap + RevealedTagRoom();
            const float monoSize = Px(Fonts::Sizes::Body);
            const float keyWidth =
                f.Mono->CalcTextSizeA(monoSize, FLT_MAX, 0.0f, "0").x * 29.0f;
            const float bulletWidth =
                f.Mono->CalcTextSizeA(monoSize, FLT_MAX, 0.0f, "••••••••••••").x;
            const float rest = Max(0.0f, avail - rightRoom - spacing * 2.0f);
            const float valueRoom = Max(Min(keyWidth, rest * 0.5f), Min(bulletWidth, rest));
            const float labelRoom = Min(labelWanted, Max(0.0f, rest - valueRoom));

            const ImVec2 labelAt = ImGui::GetCursorScreenPos();
            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            bool labelTruncated = false;
            if (item.Label.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                WipedText shownNone;
                TextView(FitEndInto(shownNone, T(Str::List_NoLabel), f.Text,
                                    Px(Fonts::Sizes::Body), labelRoom));
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                WipedText shownLabel;
                TextView(FitEndInto(shownLabel, item.Label, f.Text,
                                    Px(Fonts::Sizes::Body), labelRoom));
                labelTruncated = shownLabel.Truncated();
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && (labelTruncated || item.AddedUtc > 1))
            {
                if (BeginWrappedTooltip())
                {
                    if (!item.Label.empty())
                        TextView(item.Label);
                    if (item.AddedUtc > 1)
                    {
                        const std::string when = FormatLocalTimestamp(item.AddedUtc);
                        ImGui::TextDisabled(T(Str::List_Recorded), when.c_str());
                    }
                    EndWrappedTooltip();
                }
            }
            ImGui::PopFont();

            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(ImVec2(labelAt.x + labelRoom + spacing, labelAt.y));
            ImGui::PushFont(f.Mono, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  Theme::Color(revealed ? Token::WarnBright : Token::TextPrimary));
            if (revealed)
            {
                WipedText shown;
                TextView(FitEndInto(shown, item.Secret.View(), f.Mono,
                                    Px(Fonts::Sizes::Body), valueRoom));
            }
            else
            {
                ImGui::TextUnformatted("••••••••••••");
            }
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(
                ImVec2(labelAt.x + labelRoom + spacing + valueRoom + spacing, labelAt.y));
            RowButton(revealed ? ICON_FA_EYE_SLASH : ICON_FA_EYE, "###listreveal",
                      revealed ? Token::WarnBright : Token::TextMuted);
            if (ImGui::IsItemActive())
            {
                if (!holding)
                {
                    g_listRevealEntry = entryId;
                    g_listRevealIndex = index;
                    g_revealStarted = ImGui::GetTime();
                }
            }
            else if (holding)
            {
                StopListReveal();
            }

            ImGui::SameLine(0, smallGap);
            if (RowButton(ICON_FA_COPY, "###listcopy"))
            {
                const std::uint32_t seconds = Vault::ClipboardSeconds(
                    app.Session().Vault().Doc().VaultSettings);
                app.Clipboard().CopySecret(item.Secret.CStr(), item.Secret.Size(),
                                           static_cast<double>(seconds), Application::Now());
                app.Clipboard().SetLastLabel(T(Vault::ListKindTextsFor(item.Kind).Name));
            }

            if (revealed)
            {
                ImGui::SameLine(0, Px(8));
                ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                ImGui::TextUnformatted(T(Str::Main_Revealed));
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            ImGui::PopID();
            ImGui::Dummy(Px(0, 2));
        }

        void DrawListSection(Application& app, const Entry& e, std::uint32_t kind)
        {
            std::size_t count = 0;
            for (const Vault::ListItem& item : e.ListItems)
                if (item.Kind == kind)
                    ++count;
            if (count == 0)
                return;

            const bool knownBit = kind < 32;
            const bool expanded = !knownBit ||
                (g_listExpandedEntry == e.Id && (g_listExpandedKinds & (1u << kind)) != 0);
            const std::size_t limit = expanded ? count : kListCollapsedRows;

            float labelWanted = 0.0f;
            {
                const Fonts::Set& f = Fonts::Current();
                std::size_t measured = 0;
                for (std::size_t i = 0; i < e.ListItems.size() && measured < limit; ++i)
                {
                    const Vault::ListItem& item = e.ListItems[i];
                    if (item.Kind != kind)
                        continue;
                    ++measured;
                    const char* text = item.Label.empty() ? T(Str::List_NoLabel)
                                                          : item.Label.c_str();
                    const char* end = item.Label.empty() ? nullptr
                                                         : text + item.Label.size();
                    labelWanted = Max(labelWanted,
                                      f.Text->CalcTextSizeA(Px(Fonts::Sizes::Body), FLT_MAX,
                                                            0.0f, text, end).x);
                }
                labelWanted += Px(16.0f);
            }

            std::size_t shown = 0;
            for (std::size_t i = 0; i < e.ListItems.size() && shown < limit; ++i)
            {
                if (e.ListItems[i].Kind != kind)
                    continue;
                DrawListItemRow(app, e.ListItems[i], e.Id, i, labelWanted);
                ++shown;
            }

            if (knownBit && count > kListCollapsedRows)
            {
                char toggle[96];
                if (expanded)
                    std::snprintf(toggle, sizeof(toggle), "%s###listmore%u",
                                  T(Str::List_ShowFewer), static_cast<unsigned>(kind));
                else
                {
                    char text[64];
                    std::snprintf(text, sizeof(text), T(Str::List_ShowAll), count);
                    std::snprintf(toggle, sizeof(toggle), "%s###listmore%u", text,
                                  static_cast<unsigned>(kind));
                }
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.55f));
                if (ImGui::Button(toggle, ImVec2(0, ImGui::GetFrameHeight())))
                {
                    if (g_listExpandedEntry != e.Id)
                    {
                        g_listExpandedEntry = e.Id;
                        g_listExpandedKinds = 0;
                    }
                    g_listExpandedKinds ^= (1u << kind);
                    StopListReveal();
                }
                ImGui::PopStyleColor(2);
            }
        }

        void CollectListKinds(const Entry& e, std::vector<std::uint32_t>& out)
        {
            out.clear();
            for (const std::uint32_t kind : Vault::kListKindOrder)
                for (const Vault::ListItem& item : e.ListItems)
                    if (item.Kind == kind)
                    {
                        out.push_back(kind);
                        break;
                    }

            std::uint32_t last = 0;
            bool any = false;
            for (;;)
            {
                bool found = false;
                std::uint32_t next = 0;
                for (const Vault::ListItem& item : e.ListItems)
                {
                    if (Vault::IsKnownListKind(item.Kind))
                        continue;
                    if (any && item.Kind <= last)
                        continue;
                    if (!found || item.Kind < next)
                    {
                        next = item.Kind;
                        found = true;
                    }
                }
                if (!found)
                    break;
                out.push_back(next);
                last = next;
                any = true;
            }
        }

        bool IsLoginRole(Vault::FieldRole role)
        {
            return role == Vault::FieldRole::Username || role == Vault::FieldRole::Email ||
                   role == Vault::FieldRole::Password || role == Vault::FieldRole::Website;
        }

        void DrawPasswordStrip(const Entry& e, const Vault::AuditFinding* finding)
        {
            const Fonts::Set& f = Fonts::Current();
            BeginBox("##strip", ImGui::GetContentRegionAvail().x, Px(12.0f, 7.0f));
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

            struct Part
            {
                bool Good;
                Str Text;
            };
            const bool reused = finding && finding->ReusedPassword;
            const bool common = finding && finding->CommonPassword;
            Part parts[4];
            int count = 0;
            parts[count++] = { !reused, reused ? Str::Main_StripReused : Str::Main_StripUnique };
            if (common)
                parts[count++] = { false, Str::Main_StripCommon };
            if (finding && finding->WeakPassword)
                parts[count++] = { false, Str::Main_StripWeak };
            if (finding && finding->OldPassword)
                parts[count++] = { false, Str::Main_StripOld };

            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            const float partGap = Px(14.0f);
            const float iconGap = Px(6.0f);
            for (int i = 0; i < count; ++i)
            {
                const char* icon = parts[i].Good ? ICON_FA_CIRCLE_CHECK : ICON_FA_TRIANGLE_EXCLAMATION;
                const char* text = T(parts[i].Text);
                const float wanted = ImGui::CalcTextSize(icon).x + iconGap +
                                     ImGui::CalcTextSize(text).x;
                if (i > 0 && ImGui::GetItemRectMax().x + partGap + wanted <= right)
                    ImGui::SameLine(0, partGap);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(parts[i].Good ? Token::GreenCore : Token::WarnBright));
                ImGui::TextUnformatted(icon);
                ImGui::SameLine(0, iconGap);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      Theme::Color(parts[i].Good ? Token::TextSecondary : Token::WarnBright));
                ImGui::TextUnformatted(text);
                ImGui::PopStyleColor(2);
            }

            char since[160];
            const std::uint64_t set = Vault::CurrentPasswordSince(e);
            if (set == 0)
                std::snprintf(since, sizeof(since), "%s", T(Str::Main_PasswordNeverChanged));
            else
                std::snprintf(since, sizeof(since), T(Str::Main_PasswordSet),
                              AgeText(set).c_str());
            const float sinceWidth = ImGui::CalcTextSize(since).x;
            if (ImGui::GetItemRectMax().x + partGap + sinceWidth <= right)
            {
                ImGui::SameLine(0, 0);
                ImGui::SetCursorScreenPos(
                    ImVec2(right - sinceWidth, ImGui::GetCursorScreenPos().y));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextUnformatted(since);
            ImGui::PopStyleColor();
            if (set != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                UI::Tooltip(FormatStamp(set));
            ImGui::PopFont();

            EndBox(Theme::U32(Token::Inset, 0.35f), 0, Theme::Round(Px(6.0f)));
        }

        void DrawLoginCard(Application& app, VaultSession& session, const Entry& e,
                           const std::vector<std::size_t>& fields, float width)
        {
            const Vault::AuditFinding* finding = Audit::FindingFor(session, e.Id);
            std::string note;
            if (finding && finding->CommonPassword)
                note = UppercaseGerman(T(Str::Audit_TagCommon));
            else if (finding && finding->ReusedPassword)
                note = UppercaseGerman(T(Str::Audit_TagReused));
            else if (finding && finding->WeakPassword)
                note = UppercaseGerman(T(Str::Audit_TagWeak));

            BeginBox("##card-login", width, CardPadding());
            CardHeader(ICON_FA_KEY, T(Str::Main_CardLogin), nullptr);
            DrawFieldTiles(app, e, fields, note.empty() ? nullptr : note.c_str(),
                           Token::WarnBright);
            const Field* password = FindByRole(e, Vault::FieldRole::Password);
            if (password && !password->Value.Empty())
                DrawPasswordStrip(e, finding);
            EndCard();
        }

        enum class CardKind
        {
            TwoFactor,
            MoreFields,
            Codes,
            List
        };

        struct CardSlot
        {
            CardKind Kind;
            std::uint32_t ListKind;
        };

        void DrawCardSlot(Application& app, VaultSession& session, const Entry& e,
                          const CardSlot& slot, const std::vector<std::size_t>& moreFields,
                          float width)
        {
            switch (slot.Kind)
            {
            case CardKind::TwoFactor:
                BeginBox("##card-totp", width, CardPadding());
                DrawTotpPanel(app, e);
                EndCard();
                break;

            case CardKind::MoreFields:
            {
                char badge[48];
                if (moreFields.size() == 1)
                    std::snprintf(badge, sizeof(badge), "%s", T(Str::Main_CardFieldOne));
                else
                    std::snprintf(badge, sizeof(badge), T(Str::Main_CardFieldMany),
                                  moreFields.size());
                BeginBox("##card-fields", width, CardPadding());
                CardHeader(ICON_FA_LAYER_GROUP, T(Str::Main_CardMoreFields), badge);
                DrawFieldTiles(app, e, moreFields, nullptr, Token::TextMuted);
                EndCard();
                break;
            }

            case CardKind::Codes:
            {
                std::size_t unused = 0;
                for (const Vault::RecoveryCode& c : e.RecoveryCodes)
                    if (!c.IsUsed())
                        ++unused;
                char badge[64];
                std::snprintf(badge, sizeof(badge), T(Str::Main_CardCodesLeft), unused,
                              e.RecoveryCodes.size());
                BeginBox("##card-codes", width, CardPadding());
                CardHeader(ICON_FA_LIST, T(Str::Main_CardRecoveryCodes), badge);
                DrawRecoveryCodes(app, session, e);
                EndCard();
                break;
            }

            case CardKind::List:
            {
                std::size_t count = 0;
                for (const Vault::ListItem& item : e.ListItems)
                    if (item.Kind == slot.ListKind)
                        ++count;
                char title[96];
                if (Vault::IsKnownListKind(slot.ListKind))
                    std::snprintf(title, sizeof(title), "%s",
                                  T(Vault::ListKindTextsFor(slot.ListKind).Title));
                else
                    std::snprintf(title, sizeof(title), T(Str::Main_CardListUnknown),
                                  static_cast<unsigned>(slot.ListKind));
                char badge[24];
                std::snprintf(badge, sizeof(badge), "%zu", count);
                char id[32];
                std::snprintf(id, sizeof(id), "##card-list%u", static_cast<unsigned>(slot.ListKind));
                BeginBox(id, width, CardPadding());
                CardHeader(ICON_FA_TAG, title, badge);
                ImGui::PushID("##lists");
                DrawListSection(app, e, slot.ListKind);
                ImGui::PopID();
                EndCard();
                break;
            }
            }
        }

        void DrawNoSelection(bool vaultEmpty)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, 60));

            const char* icon = ICON_FA_LIST;
            const ImVec2 t = f.Strong->CalcTextSizeA(Px(Fonts::Sizes::Display),
                                                     FLT_MAX, 0.0f, icon);
            const float iconRoom = ImGui::GetContentRegionAvail().x;
            if (t.x < iconRoom)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (iconRoom - t.x) * 0.5f);
            ImGui::PushFont(f.Strong, Fonts::Sizes::Display);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::LineStrong));
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(Px(0, 14));
            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));

            const char* text = vaultEmpty ? T(Str::Main_NoEntrySelected)
                                          : T(Str::Main_PickAnEntry);
            const float room = ImGui::GetContentRegionAvail().x;
            const ImVec2 size =
                f.Text->CalcTextSizeA(Px(Fonts::Sizes::Body), FLT_MAX, 0.0f, text);
            if (size.x <= room)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (room - size.x) * 0.5f);
                ImGui::TextUnformatted(text);
            }
            else
            {
                ImGui::TextWrapped("%s", text);
            }

            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        std::uint64_t DuplicateEntry(Application& app, VaultSession& session, std::uint64_t id)
        {
            const Entry* e = Vault::FindEntry(session.Vault().Doc(), id);
            if (!e)
                return 0;

            Entry copy;
            const bool copied = Vault::CopyEntry(*e, copy);
            if (copied)
            {
                Vault::DropListItems(copy);
                copy.Title += T(Str::Main_DuplicateSuffix);
                if (copy.Title.size() > Vault::kMaxTitleBytes)
                    copy.Title.resize(Vault::kMaxTitleBytes);
            }

            const auto added = copied
                ? Vault::AddEntry(session.Vault().Doc(), std::move(copy), Vault::SystemUtcNow())
                : Result<std::uint64_t>(Error{ ErrorCode::OutOfMemory,
                                               "value could not be stored" });
            if (!added.IsOk())
            {
                g_duplicateFailedFor = id;
                return 0;
            }
            g_duplicateFailedFor = 0;
            g_selectedId = added.Value();
            g_scrollToSelection = true;
            SaveNow(app, session);
            return added.Value();
        }

        bool CopyUsernameOf(Application& app, const Entry* e)
        {
            if (!e)
            {
                Notice(T(Str::Main_NothingSelectedToCopy), true);
                return false;
            }
            const Field* user = FindByRole(*e, Vault::FieldRole::Username);
            if (!user || user->Value.Empty())
                user = FindByRole(*e, Vault::FieldRole::Email);
            if (!user || user->Value.Empty())
            {
                Notice(T(Str::Main_NoUsername), true);
                return false;
            }
            app.Clipboard().CopyPlain(user->Value.CStr(), user->Value.Size());
            app.Clipboard().SetLastLabel(T(FieldRoleText(user->Role)));
            NoticeCopied(T(FieldRoleText(user->Role)));
            return true;
        }

        void ApplyRowAction(Application& app, VaultSession& session)
        {
            const RowAction action = g_rowAction;
            const std::uint64_t id = g_rowActionTarget;
            g_rowAction = RowAction::None;
            g_rowActionTarget = 0;
            if (action == RowAction::None)
                return;

            Vault::Document& doc = session.Vault().Doc();
            const Entry* e = Vault::FindEntry(doc, id);
            if (!e)
                return;
            const bool changes = action != RowAction::CopyPassword &&
                                 action != RowAction::CopyUsername;
            if (changes && Editor::IsOpen())
                return;

            const std::uint64_t now = Vault::SystemUtcNow();
            switch (action)
            {
            case RowAction::Edit:
                g_selectedId = id;
                g_scrollToSelection = true;
                Settings::Close();
                Audit::Close();
                Editor::BeginEdit(*e);
                break;
            case RowAction::CopyPassword:
                CopyPasswordOf(app, session, e);
                break;
            case RowAction::CopyUsername:
                CopyUsernameOf(app, e);
                break;
            case RowAction::CopyTwoFactor:
                CopyTwoFactorOf(app, e);
                break;
            case RowAction::OpenLogin:
                OpenLoginOf(app, session, e);
                break;
            case RowAction::ToggleFavourite:
                if (Vault::SetFavourite(doc, id, !e->Favourite, now).IsOk())
                    SaveNow(app, session);
                break;
            case RowAction::MoveToFolder:
                if (Vault::SetEntryFolder(doc, id, g_rowActionFolder, now).IsOk())
                    SaveNow(app, session);
                g_rowActionFolder.clear();
                break;
            case RowAction::Rename:
            {
                g_renameTarget = id;
                const std::size_t n = e->Title.size() < sizeof(g_renameBuf) - 1
                                          ? e->Title.size()
                                          : sizeof(g_renameBuf) - 1;
                std::memcpy(g_renameBuf, e->Title.data(), n);
                g_renameBuf[n] = 0;
                g_renameProblem.clear();
                g_renamePending = true;
                break;
            }
            case RowAction::Duplicate:
                DuplicateEntry(app, session, id);
                break;
            case RowAction::Delete:
                g_deleteTarget = id;
                g_deletePending = true;
                break;
            case RowAction::None:
            default:
                break;
            }
        }

        bool DrawRenamePopup(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            if (g_renamePending)
            {
                ImGui::OpenPopup("###entry-rename");
                g_renamePending = false;
            }

            ImGui::SetNextWindowSize(Px(420, 0), ImGuiCond_Appearing);
            if (!UI::BeginTitledModal(T(Str::Main_RenameTitle), "###entry-rename",
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoSavedSettings))
                return false;

            bool documentChanged = false;
            Vault::Document& doc = session.Vault().Doc();
            if (!Vault::FindEntry(doc, g_renameTarget))
            {
                g_renameTarget = 0;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return false;
            }

            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            const bool entered = ImGui::InputTextWithHint(
                "##renametitle", T(Str::Editor_TitleHint), g_renameBuf, sizeof(g_renameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            if (!g_renameProblem.empty())
            {
                ImGui::Dummy(Px(0, 4));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                ImGui::TextWrapped("%s", g_renameProblem.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::Dummy(Px(0, 12));

            bool close = false;
            if (ImGui::Button(T(Str::Common_Save), Px(140, 32)) || entered)
            {
                const Status st = Vault::SetEntryTitle(doc, g_renameTarget, g_renameBuf,
                                                       Vault::SystemUtcNow());
                if (st.IsOk())
                {
                    documentChanged = true;
                    close = true;
                }
                else
                {
                    g_renameProblem = st.GetError().Detail ? st.GetError().Detail
                                                           : T(Str::Editor_SaveFailed);
                }
            }
            ImGui::SameLine(0, Px(8));
            if (ImGui::Button(T(Str::Common_Cancel), Px(120, 32)) ||
                (!ImGui::IsWindowAppearing() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
                close = true;

            if (close)
            {
                g_renameTarget = 0;
                g_renameBuf[0] = 0;
                g_renameProblem.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return documentChanged;
        }

        void DrawMultiPanel(Application& app, VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            Vault::Document& doc = session.Vault().Doc();
            const std::uint64_t now = Vault::SystemUtcNow();

            ImGui::Dummy(Px(0, 6));
            char head[96];
            std::snprintf(head, sizeof(head), T(Str::Main_MultiSelected), g_multi.size());
            ImGui::PushFont(f.Strong, Fonts::Sizes::Title);
            ImGui::TextUnformatted(head);
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 10));

            bool allFavourite = true;
            for (std::uint64_t id : g_multi)
                if (const Entry* e = Vault::FindEntry(doc, id); e && !e->Favourite)
                    allFavourite = false;

            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool first = true;
            auto button = [&](const char* label, Token colour) {
                const float width = ImGui::CalcTextSize(label).x +
                                    ImGui::GetStyle().FramePadding.x * 2.0f;
                if (!first)
                {
                    const float next = ImGui::GetItemRectMax().x +
                                       ImGui::GetStyle().ItemSpacing.x + width;
                    if (next < right)
                        ImGui::SameLine();
                }
                first = false;
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
                const bool hit = ImGui::Button(label, ImVec2(width, Px(32.0f)));
                ImGui::PopStyleColor();
                return hit;
            };

            char label[128];
            std::snprintf(label, sizeof(label), ICON_FA_FOLDER "   %s", T(Str::Main_MenuMoveToFolder));
            if (button(label, Token::TextSecondary))
                ImGui::OpenPopup("##multimove");
            if (ImGui::BeginPopup("##multimove"))
            {
                std::string target;
                bool chosen = false;
                if (ImGui::MenuItem(T(Str::Editor_FolderNone)))
                    chosen = true;
                const Vault::FolderSurvey folders = Vault::SurveyFolders(doc);
                if (!folders.Folders.empty())
                    ImGui::Separator();
                for (std::size_t i = 0; i < folders.Folders.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::MenuItem(folders.Folders[i].Name.c_str()))
                    {
                        target = folders.Folders[i].Name;
                        chosen = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndPopup();
                if (chosen)
                {
                    std::size_t moved = 0;
                    for (std::uint64_t id : std::vector<std::uint64_t>(g_multi))
                        if (Vault::SetEntryFolder(doc, id, target, now).IsOk())
                            ++moved;
                    if (moved != 0)
                    {
                        SaveNow(app, session);
                        char line[96];
                        std::snprintf(line, sizeof(line), T(Str::Main_MultiMoved), moved);
                        Notice(line, false);
                    }
                }
            }

            std::snprintf(label, sizeof(label), ICON_FA_STAR "   %s",
                          allFavourite ? T(Str::Main_RemoveFavourite) : T(Str::Main_AddFavourite));
            if (button(label, Token::WarnCore))
            {
                bool changed = false;
                for (std::uint64_t id : std::vector<std::uint64_t>(g_multi))
                    changed |= Vault::SetFavourite(doc, id, !allFavourite, now).IsOk();
                if (changed)
                    SaveNow(app, session);
            }

            std::snprintf(label, sizeof(label), ICON_FA_TRASH "   %s", T(Str::Main_MenuDelete));
            if (button(label, Token::RedBright))
            {
                g_deleteMulti = true;
                g_deletePending = true;
            }

            std::snprintf(label, sizeof(label), ICON_FA_XMARK "   %s", T(Str::Main_MultiClear));
            const bool clear = button(label, Token::TextMuted);

            ImGui::Dummy(Px(0, 10));
            ImGui::Separator();
            ImGui::Dummy(Px(0, 8));

            constexpr std::size_t kShown = 12;
            const float icon = Px(22.0f);
            std::size_t drawn = 0;
            for (std::uint64_t id : g_multi)
            {
                if (drawn == kShown)
                    break;
                const Entry* e = Vault::FindEntry(doc, id);
                if (!e)
                    continue;
                const ImVec2 at = ImGui::GetCursorScreenPos();
                IconSpec spec = SpecFor(*e);
                char monogram[5] = {};
                if (!HasGlyph(spec))
                {
                    DeriveMonogram(e->Title.c_str(), monogram, sizeof(monogram));
                    spec.Monogram = monogram;
                }
                DrawPlatformIconAt(ImGui::GetWindowDrawList(), at, icon, spec);
                ImGui::SetCursorScreenPos(ImVec2(at.x + icon + Px(10), at.y + Px(2)));
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                ImGui::TextUnformatted(e->Title.c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + icon + Px(6)));
                ++drawn;
            }
            if (g_multi.size() > drawn)
            {
                char more[64];
                std::snprintf(more, sizeof(more), T(Str::Main_MultiMore), g_multi.size() - drawn);
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::TextUnformatted(more);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            if (clear)
                ClearSelection();
        }

        void DrawDetailBody(Application& app, VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            const std::vector<Entry>& entries = session.Vault().Doc().Entries;

            if (Audit::IsOpen())
            {
                if (Audit::Draw(app, session))
                    SaveNow(app, session);
                if (const Audit::FindingKind kind = Audit::TakeFilterRequest();
                    kind != Audit::FindingKind::None)
                {
                    g_findingKind = kind;
                    Audit::EntriesWith(session, kind, g_findingIds);
                    SelectFolder(session.Vault().Doc(), FolderScope::Finding, {});
                    Audit::Close();
                }
                if (const std::uint64_t jump = Audit::TakeJumpTarget())
                {
                    if (!InScope(session.Vault().Doc(), jump))
                        SelectFolder(session.Vault().Doc(), FolderScope::All, {});
                    g_selectedId = jump;
                    g_scrollToSelection = true;
                    Audit::Close();
                }
                ColumnRules(true, false);
                return;
            }

            if (Settings::IsOpen())
            {
                if (Settings::Draw(app, session))
                    SaveNow(app, session);
                ColumnRules(true, false);
                return;
            }

            if (Editor::IsOpen())
            {
                const std::size_t countBefore = session.Vault().Doc().Entries.size();
                if (Editor::Draw(app, session))
                {
                    SaveNow(app, session);
                    const Vault::Document& doc = session.Vault().Doc();
                    if (doc.Entries.size() > countBefore)
                    {
                        g_selectedId = doc.Entries.back().Id;
                        g_scrollToSelection = true;
                    }
                    else if (g_selectedId != 0 && !Vault::FindEntry(doc, g_selectedId))
                    {
                        ClearSelection();
                    }
                }
                if (const std::uint64_t show = Editor::TakeShowTarget())
                {
                    Editor::RequestClose(session.Vault().Doc(), [&session, show] {
                        const Vault::Document& d = session.Vault().Doc();
                        if (!InScope(d, show))
                            SelectFolder(d, FolderScope::All, {});
                        g_selectedId = show;
                        g_scrollToSelection = true;
                        g_leaveChoseEntry = true;
                    });
                }
                return;
            }

            if (MultiActive())
            {
                DrawMultiPanel(app, session);
                return;
            }

            if (entries.empty())
            {
                DrawNoSelection(true);
                return;
            }

            const Entry* selected = SelectedEntry(session.Vault().Doc());
            if (selected && !InScope(session.Vault().Doc(), g_selectedId))
                selected = nullptr;
            if (!selected && g_selectedId != 0)
            {
                ClearSelection();
            }
            if (!selected)
            {
                DrawNoSelection(false);
                return;
            }
            const Entry& e = *selected;

            ImGui::Dummy(Px(0, 6));
            const float iconSize = Px(Fonts::Sizes::IconLarge + 16.0f);
            const ImVec2 headerPos = ImGui::GetCursorScreenPos();

            IconSpec spec = SpecFor(e);
            char monogram[5] = {};
            if (!HasGlyph(spec))
            {
                DeriveMonogram(e.Title.c_str(), monogram, sizeof(monogram));
                spec.Monogram = monogram;
            }
            DrawPlatformIconAt(ImGui::GetWindowDrawList(), headerPos, iconSize, spec);

            char editLabel[96];
            std::snprintf(editLabel, sizeof(editLabel),
                          ICON_FA_PEN "   %s", T(Str::Main_EditButton));
            char copyLabel[96];
            std::snprintf(copyLabel, sizeof(copyLabel),
                          ICON_FA_COPY "   %s", T(Str::Main_Duplicate));

            const float headerRight = headerPos.x + ImGui::GetContentRegionAvail().x;
            const float buttonHeight = Px(30.0f);
            const float gap = Px(8.0f);
            const float pad = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float starButton = ImGui::GetFrameHeight();
            const float titleX = headerPos.x + iconSize + Px(14);
            const float forTitleAndButtons = headerRight - titleX;

            float editButton = Max(Px(110.0f), ImGui::CalcTextSize(editLabel).x + pad);
            float copyButton = Max(Px(110.0f), ImGui::CalcTextSize(copyLabel).x + pad);

            const float minTitle = Min(Px(150.0f), forTitleAndButtons * 0.42f);

            bool copyIconOnly = false;
            bool editIconOnly = false;
            if (starButton + gap + copyButton + gap + editButton >
                forTitleAndButtons - minTitle)
            {
                copyIconOnly = true;
                copyButton = starButton;
                std::snprintf(copyLabel, sizeof(copyLabel), ICON_FA_COPY "###duplicate");
            }
            if (starButton + gap + copyButton + gap + editButton >
                forTitleAndButtons - minTitle)
            {
                editIconOnly = true;
                editButton = starButton;
                std::snprintf(editLabel, sizeof(editLabel), ICON_FA_PEN "###edit");
            }

            const float cluster = starButton + gap + copyButton + gap + editButton;
            const float titleWidth = Max(Px(40.0f), forTitleAndButtons - cluster - gap);

            ImGui::SetCursorScreenPos(ImVec2(titleX, headerPos.y + Px(1)));
            ImGui::BeginGroup();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Title);
            ImGui::TextUnformatted(
                FitEnd(e.Title, f.Strong, Px(Fonts::Sizes::Title), titleWidth).c_str());
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 2));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::PushFont(f.Text, Fonts::Sizes::Label);
            ImGui::TextUnformatted(ICON_FA_FOLDER);
            const float folderIcon = ImGui::GetItemRectSize().x + Px(6.0f);
            ImGui::PopFont();
            ImGui::SameLine(0, Px(6.0f));
            ImGui::PushFont(f.Mono, Fonts::Sizes::Label);
            {
                std::string meta = UppercaseGerman(
                    e.Folder.empty() ? T(Str::Editor_FolderNone) : e.Folder.c_str());
                if (e.ModifiedUtc != 0)
                {
                    char changed[160];
                    std::snprintf(changed, sizeof(changed), T(Str::Main_ChangedAgo),
                                  AgeText(e.ModifiedUtc).c_str());
                    meta += "  \xc2\xb7  ";
                    meta += UppercaseGerman(changed);
                }
                ImGui::TextUnformatted(FitEnd(meta, f.Mono, Px(Fonts::Sizes::Label),
                                              Max(Px(20.0f), titleWidth - folderIcon)).c_str());
                if (e.ModifiedUtc != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    UI::Tooltip(FormatStamp(e.ModifiedUtc));
            }
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(
                ImVec2(headerRight - editButton - gap - copyButton - gap - starButton,
                       headerPos.y + Px(2)));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  Theme::Color(Token::WarnCore, 0.16f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  Theme::Color(Token::WarnCore, 0.28f));
            const bool toggleFavourite =
                UI::IconButton("##favourite", ICON_FA_STAR,
                               ImVec2(starButton, buttonHeight),
                               e.Favourite ? Token::WarnCore : Token::TextMuted,
                               e.Favourite ? T(Str::Main_RemoveFavourite)
                                           : T(Str::Main_AddFavourite));
            ImGui::PopStyleColor(3);
            if (toggleFavourite)
            {
                if (Vault::SetFavourite(session.Vault().Doc(), e.Id, !e.Favourite,
                                        Vault::SystemUtcNow()).IsOk())
                    SaveNow(app, session);
            }

            ImGui::SetCursorScreenPos(
                ImVec2(headerRight - editButton - gap - copyButton,
                       headerPos.y + Px(2)));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            const bool duplicate =
                ImGui::Button(copyLabel, ImVec2(copyButton, buttonHeight));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                if (g_duplicateFailedFor == e.Id)
                    UI::Tooltip(T(Str::Main_DuplicateFailed));
                else if (copyIconOnly)
                    {
                        char both[512];
                        std::snprintf(both, sizeof(both), "%s \xe2\x80\x93 %s",
                                      T(Str::Main_Duplicate), T(Str::Main_DuplicateNote));
                        UI::Tooltip(both);
                    }
                else
                    UI::Tooltip(T(Str::Main_DuplicateNote));
            }

            ImGui::SameLine(0, gap);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::AccentCore));
            const bool edit = ImGui::Button(editLabel, ImVec2(editButton, buttonHeight));
            ImGui::PopStyleColor();
            if (editIconOnly && ImGui::IsItemHovered())
                UI::Tooltip(T(Str::Main_EditButton));
            if (edit)
            {
                Settings::Close();
                Editor::BeginEdit(e);
            }

            if (duplicate && DuplicateEntry(app, session, e.Id) != 0)
            {
                return;
            }

            ImGui::SetCursorScreenPos(ImVec2(headerPos.x, headerPos.y + iconSize + Px(14)));

            BeginScrollBody("##detailbody");
            ImGui::Dummy(Px(0, 2));

            std::vector<std::size_t> loginFields;
            std::vector<std::size_t> moreFields;
            for (std::size_t i = 0; i < e.Fields.size(); ++i)
            {
                if (e.Fields[i].Value.Empty())
                    continue;
                (IsLoginRole(e.Fields[i].Role) ? loginFields : moreFields).push_back(i);
            }

            std::vector<CardSlot> slots;
            if (e.HasTotp)
                slots.push_back({ CardKind::TwoFactor, 0 });
            if (!moreFields.empty())
                slots.push_back({ CardKind::MoreFields, 0 });
            if (!e.RecoveryCodes.empty())
                slots.push_back({ CardKind::Codes, 0 });
            {
                std::vector<std::uint32_t> kinds;
                CollectListKinds(e, kinds);
                for (const std::uint32_t kind : kinds)
                    slots.push_back({ CardKind::List, kind });
            }

            if (loginFields.empty() && slots.empty())
            {
                ImGui::Dummy(Px(0, 8));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextDisabled));
                ImGui::TextUnformatted(T(Str::Main_EntryEmpty));
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            else
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                const float cardGap = Px(kCardGap);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                float top = origin.y;
                if (!loginFields.empty())
                {
                    DrawLoginCard(app, session, e, loginFields, avail);
                    top = ImGui::GetItemRectMax().y + cardGap;
                }

                const bool twoColumns = avail >= Px(kTwoColumnsFrom) && slots.size() > 1;
                const float columnWidth = twoColumns ? std::floor((avail - cardGap) * 0.5f) : avail;
                float bottom[2] = { top, top };
                for (const CardSlot& slot : slots)
                {
                    const int column = (twoColumns && bottom[1] < bottom[0]) ? 1 : 0;
                    ImGui::SetCursorScreenPos(
                        ImVec2(origin.x + column * (columnWidth + cardGap), bottom[column]));
                    DrawCardSlot(app, session, e, slot, moreFields, columnWidth);
                    bottom[column] = ImGui::GetItemRectMax().y + cardGap;
                }
                ImGui::SetCursorScreenPos(ImVec2(origin.x, Max(bottom[0], bottom[1]) - cardGap));
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
            ImGui::Dummy(Px(0, 8));

            ScrollFade(Token::Panel);
            ImGui::EndChild();
        }

        enum class DetailPlace : std::uint64_t
        {
            Audit = 1,
            Settings,
            Editor,
            Multi,
            Entry
        };

        std::uint64_t DetailKey(DetailPlace place, std::uint64_t id)
        {
            return (static_cast<std::uint64_t>(place) << 56) | (id & 0x00FFFFFFFFFFFFFFull);
        }

        DetailPlace PlaceOf(std::uint64_t key)
        {
            return static_cast<DetailPlace>(key >> 56);
        }

        std::uint64_t CurrentDetailKey()
        {
            if (Audit::IsOpen())
                return DetailKey(DetailPlace::Audit, 0);
            if (Settings::IsOpen())
                return DetailKey(DetailPlace::Settings, 0);
            if (Editor::IsOpen())
                return DetailKey(DetailPlace::Editor, Editor::EditingId());
            if (MultiActive())
                return DetailKey(DetailPlace::Multi, 0);
            return DetailKey(DetailPlace::Entry, g_selectedId);
        }

        void DrawDetail(Application& app, VaultSession& session, float width, float height)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Color(Token::Panel));
            ImGui::BeginChild("##detail", ImVec2(width, height),
                              ImGuiChildFlags_AlwaysUseWindowPadding);

            const std::uint64_t key = CurrentDetailKey();
            const bool entryToEntry = PlaceOf(key) == DetailPlace::Entry &&
                                      PlaceOf(g_detailEntrance.Key()) == DetailPlace::Entry;
            g_detailEntrance.Begin(key, entryToEntry ? 0.0f : Px(6.0f), entryToEntry ? 0.5f : 1.0f);

            DrawDetailBody(app, session);

            g_detailEntrance.End(Theme::Color(Token::Panel));
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    namespace
    {
        void DrawSaveBanner(Application& app, VaultSession& session)
        {
            SaveGuard& guard = app.Saves();
            if (!guard.Failing())
                return;

            const Fonts::Set& f = Fonts::Current();
            const double now = Application::Now();
            const float height = Px(52.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                              Theme::U32(Token::RedCore, 0.16f));
            dl->AddLine(ImVec2(p.x, p.y + height - 1), ImVec2(p.x + width, p.y + height - 1),
                        Theme::U32(Token::RedCore, 0.55f));

            ImGui::SetCursorScreenPos(ImVec2(p.x + Px(14), p.y + Px(8)));
            ImGui::PushFont(f.Strong, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            char bannerTitle[96];
            std::snprintf(bannerTitle, sizeof(bannerTitle),
                          ICON_FA_TRIANGLE_EXCLAMATION "  %s", T(Str::Main_SaveFailed));
            ImGui::TextUnformatted(bannerTitle);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            const float buttonStrip = Px(382.0f);

            ImGui::SetCursorScreenPos(ImVec2(p.x + Px(14), p.y + Px(28)));
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            char line[240];
            const double holding = SaveGuard::kHoldSeconds - guard.SecondsFailing(now);
            if (holding > 0.0)
                std::snprintf(line, sizeof(line),
                              T(Str::Main_SaveFailedHolding),
                              ToUserMessage(guard.Code()),
                              guard.Detail() ? guard.Detail() : T(Str::Main_NoDetail),
                              static_cast<int>(holding / 60.0) + 1);
            else
                std::snprintf(line, sizeof(line),
                              T(Str::Main_SaveFailedExpired),
                              ToUserMessage(guard.Code()),
                              guard.Detail() ? guard.Detail() : T(Str::Main_NoDetail));
            const float lineRoom =
                Max(Px(80.0f), width - Px(14.0f) - buttonStrip - Px(12.0f));
            ImGui::TextUnformatted(
                FitEnd(line, f.Text, Px(Fonts::Sizes::Small), lineRoom).c_str());
            if (ImGui::IsItemHovered())
                UI::Tooltip(line);
            ImGui::PopStyleColor();
            ImGui::PopFont();

            const float buttonY = p.y + (height - Px(30.0f)) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(p.x + width - buttonStrip, buttonY));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::Elevated, 0.8f));
            char retryLabel[96];
            std::snprintf(retryLabel, sizeof(retryLabel),
                          ICON_FA_ROTATE "   %s", T(Str::Main_Retry));
            if (ImGui::Button(retryLabel, Px(180, 30)))
                SaveNow(app, session);
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0, Px(8));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::RedCore, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.34f));
            char lockAnywayLabel[96];
            std::snprintf(lockAnywayLabel, sizeof(lockAnywayLabel),
                          ICON_FA_LOCK "   %s", T(Str::Main_LockAnyway));
            if (ImGui::Button(lockAnywayLabel, Px(180, 30)))
            {
                guard.Acknowledge();
                LockFromUi(app, LockReason::Manual);
            }
            ImGui::PopStyleColor(3);

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + height));
        }

        bool DrawDeletePopup(Application& app, VaultSession& session)
        {
            (void)app;
            const Fonts::Set& f = Fonts::Current();
            bool documentChanged = false;

            if (g_deletePending)
            {
                ImGui::OpenPopup("###entry-delete");
                g_deletePending = false;
            }

            ImGui::SetNextWindowSize(Px(420, 0), ImGuiCond_Appearing);
            const bool many = g_deleteMulti && MultiActive();
            if (!UI::BeginTitledModal(many ? T(Str::Main_DeleteManyTitle)
                                           : T(Str::Editor_DeleteTitle),
                                      "###entry-delete",
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoSavedSettings))
                return false;

            const Entry* target = many ? nullptr
                                       : Vault::FindEntry(session.Vault().Doc(), g_deleteTarget);
            if (!many && !target)
            {
                g_deleteMulti = false;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return false;
            }

            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            if (many)
                ImGui::TextWrapped(T(Str::Main_DeleteManyWarning), g_multi.size());
            else
                ImGui::TextWrapped(T(Str::Editor_DeleteWarning), target->Title.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 14));

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::RedCore, 0.18f));
            char confirm[96];
            std::snprintf(confirm, sizeof(confirm), ICON_FA_TRASH "   %s",
                          T(Str::Editor_DeleteConfirm));
            if (ImGui::Button(confirm, Px(220, 32)))
            {
                const std::vector<std::uint64_t> ids =
                    many ? g_multi : std::vector<std::uint64_t>{ g_deleteTarget };
                if (Trash::Delete(session.Vault().Doc(), ids, Application::Now()) != 0)
                {
                    documentChanged = true;
                    g_noticeUntil = 0.0;
                    if (many || g_selectedId == g_deleteTarget)
                        ClearSelection();
                }
                g_deleteTarget = 0;
                g_deleteMulti = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0, Px(8));
            const bool cancel = ImGui::Button(T(Str::Common_Cancel), Px(120, 32));
            ImGui::SetItemDefaultFocus();
            if (cancel || (!ImGui::IsWindowAppearing() &&
                           ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            {
                g_deleteTarget = 0;
                g_deleteMulti = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return documentChanged;
        }

        int FolderJumpCallback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
            {
                if (data->EventKey == ImGuiKey_UpArrow)
                    --g_folderJumpIndex;
                else if (data->EventKey == ImGuiKey_DownArrow)
                    ++g_folderJumpIndex;
                g_folderJumpScroll = true;
            }
            else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
            {
                g_folderJumpIndex = 0;
                g_folderJumpScroll = true;
            }
            return 0;
        }

        void DrawFolderJump(VaultSession& session)
        {
            constexpr const char* kPopup = "###folder-jump";
            if (g_folderJumpPending)
            {
                g_folderJumpPending = false;
                g_folderJumpQuery[0] = 0;
                g_folderJumpIndex = 0;
                g_folderJumpScroll = true;
                ImGui::OpenPopup(kPopup);
            }

            constexpr ImVec2 kTopCentre{ 0.5f, 0.0f };
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                           vp->WorkPos.y + Px(72.0f)),
                                    ImGuiCond_Always, kTopCentre);
            ImGui::SetNextWindowSize(ImVec2(Px(380.0f), 0.0f));
            if (!ImGui::BeginPopup(kPopup))
                return;

            if (ImGui::IsWindowAppearing() || g_folderJumpRefocus)
                ImGui::SetKeyboardFocusHere();
            g_folderJumpRefocus = false;
            ImGui::SetNextItemWidth(-1.0f);
            const bool enter = ImGui::InputTextWithHint(
                "##folder-jump-query", T(Str::Main_FolderJumpHint), g_folderJumpQuery,
                sizeof(g_folderJumpQuery),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                    ImGuiInputTextFlags_CallbackEdit,
                FolderJumpCallback);
            ImGui::Dummy(Px(0, 4));

            const Vault::Document& doc = session.Vault().Doc();
            const Vault::FolderSurvey folders = Vault::SurveyFolders(doc);
            std::vector<std::string> names;
            names.reserve(folders.Folders.size());
            for (const Vault::FolderTally& row : folders.Folders)
                names.push_back(row.Name);
            const std::vector<std::string> monograms = Vault::FolderMonograms(names);

            struct Choice
            {
                FolderScope Scope;
                std::size_t Row;
                const char* Icon;
                const char* Label;
                std::size_t Count;
            };
            std::vector<Choice> choices;
            const std::string needle = Vault::FoldForSearch(g_folderJumpQuery);
            const auto special = [&](const char* label) {
                return needle.empty() || Vault::FoldForSearch(label).find(needle) != std::string::npos;
            };
            if (special(T(Str::Main_FolderAll)))
                choices.push_back({ FolderScope::All, 0, ICON_FA_LAYER_GROUP,
                                    T(Str::Main_FolderAll), folders.Total });
            for (std::size_t i : Vault::MatchFolders(folders, g_folderJumpQuery))
                choices.push_back({ FolderScope::One, i, nullptr,
                                    folders.Folders[i].Name.c_str(), folders.Folders[i].Count });
            if (folders.Unfiled != 0 && special(T(Str::Main_FolderUnfiled)))
                choices.push_back({ FolderScope::Unfiled, 0, ICON_FA_CIRCLE_MINUS,
                                    T(Str::Main_FolderUnfiled), folders.Unfiled });

            const int count = static_cast<int>(choices.size());
            g_folderJumpIndex = count == 0 ? 0 : std::clamp(g_folderJumpIndex, 0, count - 1);

            int chosen = -1;
            if (count == 0)
            {
                ImGui::TextDisabled("%s", T(Str::Main_FolderJumpNone));
            }
            else
            {
                const Fonts::Set& f = Fonts::Current();
                const float rowHeight = Px(30.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::BeginChild("##folder-jump-list",
                                  ImVec2(0, std::min(rowHeight * static_cast<float>(count) +
                                                         Px(4.0f),
                                                     Px(320.0f))));
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, Theme::Round(Px(6.0f)));
                for (int i = 0; i < count; ++i)
                {
                    const Choice& c = choices[static_cast<std::size_t>(i)];
                    ImGui::PushID(i);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float width = ImGui::GetContentRegionAvail().x;
                    if (ImGui::Selectable("##choice", i == g_folderJumpIndex, 0,
                                          ImVec2(0, rowHeight)))
                        chosen = i;
                    if (i == g_folderJumpIndex && g_folderJumpScroll)
                        ImGui::SetScrollHereY(0.5f);

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 mark(at.x + Px(16.0f), at.y + rowHeight * 0.5f);
                    if (c.Scope == FolderScope::One)
                        DrawFolderTile(dl, mark, Px(kFolderTileSize) * 0.9f,
                                       monograms[c.Row].c_str(),
                                       FolderAccent(folders.Folders[c.Row]));
                    else
                        CentredGlyph(dl, f.Text, Px(Fonts::Sizes::Body), mark,
                                     Theme::U32(Token::TextMuted), c.Icon);

                    char tally[24];
                    std::snprintf(tally, sizeof(tally), "%zu", c.Count);
                    const ImVec2 t = f.Text->CalcTextSizeA(Px(Fonts::Sizes::Small), FLT_MAX,
                                                           0.0f, tally);
                    const float tallyX = at.x + width - t.x - Px(kRowTallyInset);
                    dl->AddText(f.Text, Px(Fonts::Sizes::Small),
                                ImVec2(tallyX, at.y + (rowHeight - t.y) * 0.5f),
                                Theme::U32(Token::TextDisabled), tally);

                    const float textX = at.x + Px(36.0f);
                    dl->PushClipRect(ImVec2(textX, at.y),
                                     ImVec2(tallyX - Px(kRowTallyGap), at.y + rowHeight), true);
                    dl->AddText(f.Text, Px(Fonts::Sizes::Body),
                                ImVec2(textX, at.y + (rowHeight - Px(Fonts::Sizes::Body)) * 0.5f -
                                                  Px(1.0f)),
                                Theme::U32(i == g_folderJumpIndex ? Token::TextPrimary
                                                                  : Token::TextSecondary),
                                c.Label);
                    dl->PopClipRect();
                    ImGui::PopID();
                }
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            g_folderJumpScroll = false;

            if (enter && count > 0)
                chosen = g_folderJumpIndex;
            else if (enter)
                g_folderJumpRefocus = true;
            if (chosen >= 0)
            {
                const Choice& c = choices[static_cast<std::size_t>(chosen)];
                ShowFolder(doc, c.Scope,
                           c.Scope == FolderScope::One ? folders.Folders[c.Row].Name
                                                       : std::string());
                ImGui::CloseCurrentPopup();
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        bool g_palettePending = false;
        char g_paletteQuery[128] = {};
        int g_paletteIndex = 0;
        bool g_paletteScroll = false;
        bool g_paletteRefocus = false;

        void ForgetPalette()
        {
            g_palettePending = false;
            sodium_memzero(g_paletteQuery, sizeof(g_paletteQuery));
            g_paletteIndex = 0;
            g_paletteScroll = false;
            g_paletteRefocus = false;
            g_helpPending = false;
        }

        int PaletteCallback(ImGuiInputTextCallbackData* data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
            {
                if (data->EventKey == ImGuiKey_UpArrow)
                    --g_paletteIndex;
                else if (data->EventKey == ImGuiKey_DownArrow)
                    ++g_paletteIndex;
                g_paletteScroll = true;
            }
            else if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
            {
                g_paletteIndex = 0;
                g_paletteScroll = true;
            }
            return 0;
        }

        enum class PaletteCommand : std::uint8_t
        {
            NewEntry,
            Generate,
            Favourites,
            ShowAll,
            JumpToFolder,
            Security,
            Settings,
            SortName,
            SortModified,
            ToggleSidebar,
            ToggleIcons,
            Import,
            Shortcuts,
            Theme,
            Lock,
        };

        struct PaletteItem
        {
            enum class Kind : std::uint8_t { Entry, Folder, Command };
            Kind What = Kind::Command;
            PaletteCommand Command = PaletteCommand::NewEntry;
            std::uint64_t Id = 0;
            std::string Label;
            const char* Icon = nullptr;
            const char* Key = nullptr;
        };

        template <typename Change>
        void ChangeVaultSettings(Application& app, VaultSession& session, Change change)
        {
            Vault::Document& doc = session.Vault().Doc();
            Vault::Settings draft = doc.VaultSettings;
            change(draft);
            Vault::SetVaultSettings(doc, draft);
            SaveNow(app, session);
            app.ApplyVaultSettings();
            if (Settings::IsOpen())
                Settings::Open();
        }

        bool RunPaletteItem(Application& app, VaultSession& session, const PaletteItem& item)
        {
            const Vault::Document& doc = session.Vault().Doc();
            if (item.What == PaletteItem::Kind::Entry)
            {
                const std::uint64_t id = item.Id;
                Editor::RequestClose(doc, [&session, id] {
                    Settings::Close();
                    Audit::Close();
                    const Vault::Document& d = session.Vault().Doc();
                    if (!InScope(d, id))
                        SelectFolder(d, FolderScope::All, {});
                    ClearMulti();
                    g_selectedId = id;
                    g_scrollToSelection = true;
                    g_leaveChoseEntry = true;
                });
                return false;
            }
            if (item.What == PaletteItem::Kind::Folder)
            {
                ShowFolder(doc, FolderScope::One, item.Label);
                return false;
            }
            switch (item.Command)
            {
            case PaletteCommand::NewEntry:
            {
                const std::string folder =
                    g_folderScope == FolderScope::One ? g_folderName : std::string();
                Editor::RequestClose(doc, [folder] {
                    Settings::Close();
                    Audit::Close();
                    Editor::BeginNew(folder);
                });
                break;
            }
            case PaletteCommand::Generate:
                Editor::OpenStandaloneGenerator();
                break;
            case PaletteCommand::Favourites:
                Editor::RequestClose(doc, [&session] {
                    Settings::Close();
                    Audit::Close();
                    SelectFolder(session.Vault().Doc(), FolderScope::Favourites, {});
                });
                break;
            case PaletteCommand::ShowAll:
                ShowFolder(doc, FolderScope::All, {});
                break;
            case PaletteCommand::JumpToFolder:
                g_folderJumpPending = true;
                break;
            case PaletteCommand::Security:
                Editor::RequestClose(doc, [] {
                    Settings::Close();
                    Audit::Open();
                });
                break;
            case PaletteCommand::Settings:
                Editor::RequestClose(doc, [] {
                    Audit::Close();
                    Settings::Open();
                });
                break;
            case PaletteCommand::SortName:
                SetListOrder(app, session, Vault::SortOrder::Title);
                break;
            case PaletteCommand::SortModified:
                SetListOrder(app, session, Vault::SortOrder::Modified);
                break;
            case PaletteCommand::ToggleSidebar:
                ToggleSidebar(app, session);
                break;
            case PaletteCommand::ToggleIcons:
                ChangeVaultSettings(app, session, [](Vault::Settings& s) {
                    s.MonochromeIcons = !s.MonochromeIcons;
                });
                break;
            case PaletteCommand::Import:
                Settings::StartImport();
                break;
            case PaletteCommand::Shortcuts:
                g_helpPending = true;
                break;
            case PaletteCommand::Theme:
            {
                const std::size_t index = static_cast<std::size_t>(item.Id);
                ChangeVaultSettings(app, session, [index](Vault::Settings& s) {
                    s.Theme = index == 0 ? std::string() : std::string(PaletteAt(index).Id);
                });
                break;
            }
            case PaletteCommand::Lock:
                return true;
            }
            return false;
        }

        bool DrawPalette(Application& app, VaultSession& session)
        {
            constexpr const char* kPopup = "###command-palette";
            if (g_palettePending)
            {
                g_palettePending = false;
                g_paletteQuery[0] = 0;
                g_paletteIndex = 0;
                g_paletteScroll = true;
                ImGui::OpenPopup(kPopup);
            }

            constexpr ImVec2 kTopCentre{ 0.5f, 0.0f };
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                           vp->WorkPos.y + Px(72.0f)),
                                    ImGuiCond_Always, kTopCentre);
            ImGui::SetNextWindowSize(ImVec2(Px(520.0f), 0.0f));
            if (!ImGui::BeginPopup(kPopup))
                return false;

            if (ImGui::IsWindowAppearing() || g_paletteRefocus)
                ImGui::SetKeyboardFocusHere();
            g_paletteRefocus = false;
            ImGui::SetNextItemWidth(-1.0f);
            const bool enter = ImGui::InputTextWithHint(
                "##palette-query", T(Str::Palette_Hint), g_paletteQuery, sizeof(g_paletteQuery),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
                    ImGuiInputTextFlags_CallbackEdit,
                PaletteCallback);
            ImGui::Dummy(Px(0, 4));

            const Vault::Document& doc = session.Vault().Doc();
            const std::string needle = Vault::FoldForSearch(g_paletteQuery);
            std::vector<PaletteItem> items;

            if (!needle.empty())
            {
                const auto hits = Vault::Search(doc, g_paletteQuery,
                                                Vault::SortOrderFrom(doc.VaultSettings.ListOrder));
                for (std::size_t i = 0, shown = 0; i < hits.size() && shown < 6; ++i)
                {
                    const Entry* e = Vault::FindEntry(doc, hits[i].EntryId);
                    if (!e)
                        continue;
                    PaletteItem item;
                    item.What = PaletteItem::Kind::Entry;
                    item.Id = e->Id;
                    item.Label = e->Title;
                    items.push_back(std::move(item));
                    ++shown;
                }
                const Vault::FolderSurvey folders = Vault::SurveyFolders(doc);
                const std::vector<std::size_t> matches = Vault::MatchFolders(folders, g_paletteQuery);
                for (std::size_t i = 0; i < matches.size() && i < 4; ++i)
                {
                    PaletteItem item;
                    item.What = PaletteItem::Kind::Folder;
                    item.Label = folders.Folders[matches[i]].Name;
                    item.Icon = ICON_FA_FOLDER;
                    items.push_back(std::move(item));
                }
            }

            using Shortcuts::Action;
            const auto command = [&](PaletteCommand c, const char* icon, const char* label,
                                     const char* key, std::uint64_t id = 0) {
                if (!needle.empty() && !Vault::FoldedContains(label, needle))
                    return;
                PaletteItem item;
                item.What = PaletteItem::Kind::Command;
                item.Command = c;
                item.Id = id;
                item.Label = label;
                item.Icon = icon;
                item.Key = key;
                items.push_back(std::move(item));
            };
            command(PaletteCommand::NewEntry, ICON_FA_CIRCLE_PLUS, T(Str::Main_NewEntry),
                    Shortcuts::KeyName(Action::NewEntry));
            command(PaletteCommand::Generate, ICON_FA_DICE, T(Str::Main_GeneratePassword), nullptr);
            command(PaletteCommand::Favourites, ICON_FA_STAR, T(Str::Main_Favourites), nullptr);
            command(PaletteCommand::ShowAll, ICON_FA_LAYER_GROUP, T(Str::Main_ShowAllFolders),
                    nullptr);
            command(PaletteCommand::JumpToFolder, ICON_FA_FOLDER, T(Str::Settings_ShortcutCtrlO),
                    Shortcuts::KeyName(Action::JumpToFolder));
            command(PaletteCommand::Security, ICON_FA_SHIELD_HALVED, T(Str::Main_Security), nullptr);
            command(PaletteCommand::Settings, ICON_FA_GEAR, T(Str::Main_Settings), nullptr);
            command(PaletteCommand::SortName, ICON_FA_SORT, T(Str::Palette_SortName), nullptr);
            command(PaletteCommand::SortModified, ICON_FA_SORT, T(Str::Palette_SortModified), nullptr);
            command(PaletteCommand::ToggleSidebar, ICON_FA_BARS,
                    doc.VaultSettings.SidebarCollapsed ? T(Str::Main_ExpandSidebar)
                                                       : T(Str::Main_CollapseSidebar),
                    Shortcuts::KeyName(Action::ToggleSidebar));
            command(PaletteCommand::ToggleIcons, ICON_FA_TAG, T(Str::Palette_ToggleIcons), nullptr);
            command(PaletteCommand::Import, ICON_FA_FILE_IMPORT, T(Str::Main_ImportCsv), nullptr);
            command(PaletteCommand::Shortcuts, ICON_FA_KEYBOARD, T(Str::Palette_Shortcuts),
                    Shortcuts::KeyName(Action::Help));
            if (!needle.empty())
            {
                for (std::size_t i = 0; i < PaletteCount(); ++i)
                {
                    char themeLabel[96];
                    std::snprintf(themeLabel, sizeof(themeLabel), T(Str::Palette_Theme),
                                  Settings::ThemeLabel(i));
                    command(PaletteCommand::Theme, ICON_FA_CIRCLE_CHECK, themeLabel, nullptr, i);
                }
            }
            command(PaletteCommand::Lock, ICON_FA_LOCK, T(Str::Main_LockButton),
                    Shortcuts::KeyName(Action::Lock));

            const int count = static_cast<int>(items.size());
            g_paletteIndex = count == 0 ? 0 : std::clamp(g_paletteIndex, 0, count - 1);

            int chosen = -1;
            if (count == 0)
            {
                ImGui::TextDisabled("%s", T(Str::Palette_None));
            }
            else
            {
                const Fonts::Set& f = Fonts::Current();
                const float rowHeight = Px(30.0f);
                const float headHeight = Px(22.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
                ImGui::BeginChild("##palette-list",
                                  ImVec2(0, std::min(rowHeight * static_cast<float>(count) +
                                                         headHeight * 3.0f + Px(4.0f),
                                                     Px(400.0f))));
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, Theme::Round(Px(6.0f)));
                PaletteItem::Kind section = PaletteItem::Kind::Command;
                bool firstRow = true;
                for (int i = 0; i < count; ++i)
                {
                    const PaletteItem& item = items[static_cast<std::size_t>(i)];
                    if (firstRow || item.What != section)
                    {
                        section = item.What;
                        firstRow = false;
                        const Str heading = section == PaletteItem::Kind::Entry  ? Str::Palette_Entries
                                          : section == PaletteItem::Kind::Folder ? Str::Palette_Folders
                                                                                 : Str::Palette_Commands;
                        ImGui::PushFont(f.Strong, Fonts::Sizes::Label);
                        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(6.0f));
                        ImGui::TextUnformatted(T(heading));
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }

                    ImGui::PushID(i);
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float width = ImGui::GetContentRegionAvail().x;
                    if (ImGui::Selectable("##item", i == g_paletteIndex, 0, ImVec2(0, rowHeight)))
                        chosen = i;
                    if (i == g_paletteIndex && g_paletteScroll)
                        ImGui::SetScrollHereY(0.5f);

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 mark(at.x + Px(16.0f), at.y + rowHeight * 0.5f);
                    if (item.What == PaletteItem::Kind::Entry)
                    {
                        if (const Entry* e = Vault::FindEntry(doc, item.Id))
                        {
                            IconSpec spec = SpecFor(*e);
                            char monogram[5] = {};
                            if (!HasGlyph(spec))
                            {
                                DeriveMonogram(e->Title.c_str(), monogram, sizeof(monogram));
                                spec.Monogram = monogram;
                            }
                            const float icon = Px(20.0f);
                            DrawPlatformIconAt(dl, ImVec2(mark.x - icon * 0.5f, mark.y - icon * 0.5f),
                                               icon, spec);
                        }
                    }
                    else
                    {
                        CentredGlyph(dl, f.Text, Px(Fonts::Sizes::Body), mark,
                                     Theme::U32(Token::TextMuted), item.Icon);
                    }

                    float textRight = at.x + width - Px(8.0f);
                    if (item.Key)
                    {
                        const ImVec2 k = f.Text->CalcTextSizeA(Px(Fonts::Sizes::Small), FLT_MAX,
                                                               0.0f, item.Key);
                        const float keyX = at.x + width - k.x - Px(10.0f);
                        dl->AddText(f.Text, Px(Fonts::Sizes::Small),
                                    ImVec2(keyX, at.y + (rowHeight - k.y) * 0.5f),
                                    Theme::U32(Token::TextDisabled), item.Key);
                        textRight = keyX - Px(10.0f);
                    }
                    const float textX = at.x + Px(36.0f);
                    dl->PushClipRect(ImVec2(textX, at.y), ImVec2(textRight, at.y + rowHeight), true);
                    dl->AddText(f.Text, Px(Fonts::Sizes::Body),
                                ImVec2(textX, at.y + (rowHeight - Px(Fonts::Sizes::Body)) * 0.5f -
                                                  Px(1.0f)),
                                Theme::U32(i == g_paletteIndex ? Token::TextPrimary
                                                               : Token::TextSecondary),
                                item.Label.c_str());
                    dl->PopClipRect();
                    ImGui::PopID();
                }
                ImGui::PopStyleVar();
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
            g_paletteScroll = false;

            if (enter && count > 0)
                chosen = g_paletteIndex;
            else if (enter)
                g_paletteRefocus = true;

            bool lock = false;
            if (chosen >= 0)
            {
                const PaletteItem item = items[static_cast<std::size_t>(chosen)];
                ImGui::CloseCurrentPopup();
                lock = RunPaletteItem(app, session, item);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                     (!ImGui::IsWindowAppearing() &&
                      Shortcuts::Pressed(Shortcuts::Action::CommandPalette)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return lock;
        }

        void DrawShortcutHelp()
        {
            constexpr const char* kPopup = "###shortcut-help";
            if (g_helpPending)
            {
                g_helpPending = false;
                ImGui::OpenPopup(kPopup);
            }
            constexpr ImVec2 kCentre{ 0.5f, 0.5f };
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                           vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                                    ImGuiCond_Always, kCentre);
            ImGui::SetNextWindowSize(ImVec2(Px(600.0f), 0.0f));
            if (!ImGui::BeginPopup(kPopup))
                return;

            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Strong, Fonts::Sizes::Title);
            ImGui::TextUnformatted(ICON_FA_KEYBOARD "  ");
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(T(Str::Main_ShortcutsTitle));
            ImGui::PopFont();
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextUnformatted(T(Str::Main_ShortcutsClose));
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(Px(0, 6));
            ImGui::Separator();
            ImGui::Dummy(Px(0, 6));

            const float roof = vp->WorkSize.y - Px(80.0f) - ImGui::GetCursorPosY() -
                               ImGui::GetStyle().WindowPadding.y;
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                                ImVec2(FLT_MAX, std::max(Px(120.0f), roof)));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            ImGui::BeginChild("##keys-body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);

            if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingStretchProp))
            {
                std::size_t count = 0;
                const Shortcuts::Shortcut* all = Shortcuts::All(count);
                float keyColumn = 0.0f;
                for (std::size_t i = 0; i < count; ++i)
                    keyColumn = Max(keyColumn, KeyChipWidth(T(all[i].KeyName)));
                ImGui::TableSetupColumn("##key", ImGuiTableColumnFlags_WidthFixed,
                                        keyColumn + Px(12.0f));
                ImGui::TableSetupColumn("##what", ImGuiTableColumnFlags_WidthStretch);
                for (std::size_t i = 0; i < count; ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    KeyChip(T(all[i].KeyName));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(2.0f));
                    ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                    ImGui::TextWrapped("%s", T(all[i].Description));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                ImGui::EndTable();
            }
            ScrollFade(Token::Elevated);
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (!ImGui::IsWindowAppearing() && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                                 ImGui::IsKeyPressed(ImGuiKey_F1, false)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        bool DrawFolderPopups(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            Vault::Document& doc = session.Vault().Doc();
            bool documentChanged = false;

            if (g_folderRenamePending)
            {
                ImGui::OpenPopup("###folder-rename");
                g_folderRenamePending = false;
            }
            if (g_folderDissolvePending)
            {
                ImGui::OpenPopup("###folder-dissolve");
                g_folderDissolvePending = false;
            }

            ImGui::SetNextWindowSize(Px(440, 0), ImGuiCond_Appearing);
            if (UI::BeginTitledModal(T(Str::Main_RenameFolderTitle), "###folder-rename",
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##foldername", T(Str::Main_FolderNameHint),
                                         g_folderNameBuf, sizeof(g_folderNameBuf));

                const std::size_t count = Vault::CountEntriesInFolder(doc, g_folderTarget);
                const std::string typed = g_folderNameBuf;

                ImGui::Dummy(Px(0, 6));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::TextWrapped(T(Str::Main_RenameFolderNote), count);

                const std::string existing = Vault::ExistingSpelling(doc, typed);
                if (!existing.empty() && !Vault::SameFolder(existing, g_folderTarget))
                {
                    ImGui::Dummy(Px(0, 4));
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::WarnBright));
                    ImGui::TextWrapped(T(Str::Main_FolderMergeNote), existing.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::PopStyleColor();
                ImGui::PopFont();

                if (!g_folderProblem.empty())
                {
                    ImGui::Dummy(Px(0, 4));
                    ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                    ImGui::TextWrapped("%s", g_folderProblem.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }

                ImGui::Dummy(Px(0, 14));
                if (ImGui::Button(T(Str::Common_Save), Px(160, 32)))
                {
                    const auto moved = Vault::RenameFolder(doc, g_folderTarget,
                                                           typed, Vault::SystemUtcNow());
                    if (moved.IsOk())
                    {
                        documentChanged = true;
                        if (g_folderScope == FolderScope::One &&
                            Vault::SameFolder(g_folderName, g_folderTarget))
                        {
                            const std::string now = Vault::ExistingSpelling(doc, typed);
                            g_folderName = now.empty() ? typed : now;
                        }
                        ForgetFolderDialogs();
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        g_folderProblem = T(Str::Main_FolderNameBad);
                    }
                }
                ImGui::SameLine(0, Px(8));
                if (ImGui::Button(T(Str::Common_Cancel), Px(120, 32)))
                {
                    ForgetFolderDialogs();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::SetNextWindowSize(Px(440, 0), ImGuiCond_Appearing);
            if (UI::BeginTitledModal(T(Str::Main_DissolveFolderTitle), "###folder-dissolve",
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings))
            {
                const std::size_t count = Vault::CountEntriesInFolder(doc, g_folderTarget);
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::TextWrapped(T(Str::Main_DissolveFolderNote), count);
                ImGui::PopStyleColor();
                ImGui::PopFont();

                ImGui::Dummy(Px(0, 14));
                if (ImGui::Button(T(Str::Main_DissolveFolder), Px(160, 32)))
                {
                    if (Vault::DissolveFolder(doc, g_folderTarget,
                                              Vault::SystemUtcNow()).IsOk())
                        documentChanged = true;
                    if (g_folderScope == FolderScope::One &&
                        Vault::SameFolder(g_folderName, g_folderTarget))
                    {
                        g_folderScope = FolderScope::All;
                        g_folderName.clear();
                    }
                    ForgetFolderDialogs();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(0, Px(8));
                if (ImGui::Button(T(Str::Common_Cancel), Px(120, 32)))
                {
                    ForgetFolderDialogs();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            return documentChanged;
        }
    }

    void ResetMainScreenState()
    {
        ResetUiState();
    }

    namespace
    {
    }

    void DrawMain(Application& app, VaultSession& session)
    {
        app.RequestAnimationFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##shell", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
        ImGui::PopStyleVar(2);

        if (ListOwnsKeys())
            ImGui::SetNavCursorVisible(false);

        const float topBarHeight = Px(kTopBarHeight);
        const float fullHeight = ImGui::GetContentRegionAvail().y;
        const float sidebarWidth = session.Vault().Doc().VaultSettings.SidebarCollapsed
                                       ? Px(kCompactSidebarWidth)
                                       : Px(208.0f);
        const float listWidth = Px(330.0f);

        if (g_folderScope == FolderScope::Finding)
            Audit::EntriesWith(session, g_findingKind, g_findingIds);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PxWhole(14, 10));
        DrawSidebar(app, session, sidebarWidth, fullHeight, topBarHeight);
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        DrawTopBar(app, session, topBarHeight);
        DrawSaveBanner(app, session);
        const float bodyHeight = ImGui::GetContentRegionAvail().y;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PxWhole(14, 10));
        if (!Settings::IsOpen() && !Audit::IsOpen())
        {
            DrawEntryList(app, session, listWidth, bodyHeight);
            ImGui::SameLine(0, 0);
        }
        DrawDetail(app, session, ImGui::GetContentRegionAvail().x, bodyHeight);
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        ApplyRowAction(app, session);

        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                         ImGuiPopupFlags_AnyPopupLevel) &&
            !g_popupOpenLastFrame)
        {
            if (!ImGui::IsAnyItemActive() && !g_itemActiveLastFrame &&
                !SecretField::TookEscapeThisFrame() &&
                Shortcuts::Pressed(Shortcuts::Action::StepBack))
            {
                if (Audit::IsOpen())
                    Audit::Close();
                else if (Settings::IsOpen())
                    Settings::Close();
                else if (Editor::IsOpen())
                    Editor::RequestClose(session.Vault().Doc(), {});
                else if (MultiActive())
                    ClearSelection();
                else if (g_search[0] != 0)
                    g_search[0] = 0;
                else if (g_folderScope != FolderScope::All)
                    SelectFolder(session.Vault().Doc(), FolderScope::All, {});
            }
            using Shortcuts::Action;
            const bool listShown = !Settings::IsOpen() && !Audit::IsOpen();
            if (Shortcuts::Pressed(Action::Rename) && !ImGui::IsAnyItemActive() &&
                !Editor::IsOpen() && listShown)
            {
                if (const Entry* e = SelectedEntry(session.Vault().Doc()))
                {
                    g_rowAction = RowAction::Rename;
                    g_rowActionTarget = e->Id;
                }
            }
            if (Shortcuts::Pressed(Action::Search))
                g_focusSearch = true;
            if (Shortcuts::Pressed(Action::JumpToFolder))
                g_folderJumpPending = true;
            if (Shortcuts::Pressed(Action::CommandPalette))
                g_palettePending = true;
            if (Shortcuts::Pressed(Action::Help))
                g_helpPending = true;
            if (Shortcuts::Pressed(Action::Undo) && !ImGui::IsAnyItemActive() &&
                !g_itemActiveLastFrame && Trash::Pending(Application::Now()))
                g_undoRequested = true;
            const bool several = MultiActive();
            if (Shortcuts::Pressed(Action::ToggleSidebar))
                ToggleSidebar(app, session);
            if (Shortcuts::Pressed(Action::CopyPassword) && !ImGui::IsAnyItemActive() &&
                listShown)
            {
                if (several)
                    Notice(T(Str::Main_MultiNoCopy), true);
                else
                    CopySelectedPassword(app, session);
            }
            if (Shortcuts::Pressed(Action::CopyUsername) && !ImGui::IsAnyItemActive() &&
                listShown)
            {
                if (several)
                    Notice(T(Str::Main_MultiNoCopy), true);
                else
                    CopyUsernameOf(app, SelectedEntry(session.Vault().Doc()));
            }
            if (Shortcuts::Pressed(Action::CopyTwoFactor) && !ImGui::IsAnyItemActive() &&
                listShown)
            {
                if (several)
                    Notice(T(Str::Main_MultiNoCopy), true);
                else
                    CopyTwoFactorOf(app, SelectedEntry(session.Vault().Doc()));
            }
            if (Shortcuts::Pressed(Action::NewEntry))
                StartNewEntry(session.Vault().Doc());
            if (Shortcuts::Pressed(Action::Lock))
                LockFromUi(app, LockReason::Manual);
            if (Shortcuts::Pressed(Action::ToggleFavourite) && listShown)
            {
                Vault::Document& doc = session.Vault().Doc();
                if (const Entry* e = SelectedEntry(doc))
                {
                    if (Vault::SetFavourite(doc, e->Id, !e->Favourite,
                                            Vault::SystemUtcNow()).IsOk())
                        SaveNow(app, session);
                }
            }

            const bool listOwnsKeys = ListOwnsKeys();
            if (listOwnsKeys && !g_itemActiveLastFrame)
            {
                const Vault::Document& doc = session.Vault().Doc();
                const bool shift = ImGui::GetIO().KeyShift;
                const auto step = [&](int delta) {
                    const std::uint64_t before = g_selectedId;
                    if (shift && before != 0 && g_multiAnchor == 0)
                        g_multiAnchor = before;
                    StepSelection(doc, delta);
                    if (shift && before != 0)
                        SelectRangeTo(VisibleHits(doc).Hits, g_selectedId);
                    else
                        ClearMulti();
                };
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
                    step(+1);
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
                    step(-1);
                if (Shortcuts::Pressed(Action::SelectAll))
                    SelectAllVisible(doc);
                if (Shortcuts::Pressed(Action::EditEntry) && !several)
                {
                    if (const Entry* e = SelectedEntry(doc))
                        Editor::BeginEdit(*e);
                }
                if (Shortcuts::Pressed(Action::OpenLogin))
                {
                    if (several)
                        Notice(T(Str::Main_MultiNoCopy), true);
                    else
                        OpenLoginOf(app, session, SelectedEntry(doc));
                }
                if (Shortcuts::Pressed(Action::Delete) && several)
                {
                    g_deleteMulti = true;
                    g_deletePending = true;
                }
                else if (Shortcuts::Pressed(Action::Delete) && SelectedEntry(doc))
                {
                    g_deleteTarget = g_selectedId;
                    g_deleteMulti = false;
                    g_deletePending = true;
                }
            }

            if (!ImGui::IsAnyItemActive())
                Shortcuts::Claim(Action::StepBack);
            if (listOwnsKeys)
            {
                Shortcuts::Claim(Action::EditEntry);
                Shortcuts::Claim(Action::OpenLogin);
                Shortcuts::Claim(Action::SelectAll);
                Shortcuts::ClaimNavigationKeys();
            }
        }

        if (g_undoRequested)
        {
            g_undoRequested = false;
            Vault::Document& doc = session.Vault().Doc();
            const std::vector<std::uint64_t> back = Trash::Restore(doc);
            if (back.empty())
            {
                Notice(T(Str::Main_UndoFailed), true);
            }
            else
            {
                SaveNow(app, session);
                const std::uint64_t first = back.back();
                if (!InScope(doc, first))
                    SelectFolder(doc, FolderScope::All, {});
                ClearMulti();
                if (back.size() >= 2)
                {
                    g_multi.assign(back.rbegin(), back.rend());
                    g_multiAnchor = g_multi.front();
                }
                g_selectedId = first;
                g_scrollToSelection = true;
                Notice(T(Str::Main_Restored), false);
            }
        }
        Trash::Expire(Application::Now());

        if (Settings::DrawPopups(app, session))
            SaveNow(app, session);
        {
            bool worked = true;
            const std::string summary = Settings::TakeImportSummary(worked);
            if (!summary.empty())
                Notice(summary, !worked);
        }

        if (DrawFolderPopups(session))
            SaveNow(app, session);

        if (DrawPalette(app, session))
            LockFromUi(app, LockReason::Manual);
        DrawFolderJump(session);
        DrawShortcutHelp();

        if (DrawDeletePopup(app, session))
            SaveNow(app, session);

        if (DrawRenamePopup(session))
            SaveNow(app, session);

        const std::size_t countBefore = session.Vault().Doc().Entries.size();
        const std::uint64_t selectedBefore = g_selectedId;
        g_leaveChoseEntry = false;
        const bool editorSaved = Editor::DrawPopups(app, session);
        if (editorSaved)
        {
            SaveNow(app, session);
            const Vault::Document& doc = session.Vault().Doc();
            if (doc.Entries.size() > countBefore && !g_leaveChoseEntry &&
                g_selectedId == selectedBefore && InScope(doc, doc.Entries.back().Id))
            {
                g_selectedId = doc.Entries.back().Id;
                g_scrollToSelection = true;
            }
            else if (g_selectedId != 0 && !Vault::FindEntry(doc, g_selectedId))
                ClearSelection();
        }

        if (app.TakeCloseRequest())
            Editor::RequestClose(session.Vault().Doc(), [] { g_quitRequested = true; });
        if (g_quitRequested)
        {
            g_quitRequested = false;
            if (!(editorSaved && app.Saves().Failing()))
                app.Quit();
        }

        g_itemActiveLastFrame = ImGui::IsAnyItemActive();
        g_popupOpenLastFrame = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                           ImGuiPopupFlags_AnyPopupLevel);

        ImGui::End();
    }
}
