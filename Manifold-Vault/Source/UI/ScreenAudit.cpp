#include "Core/Language.h"
#include "UI/Tooltip.h"
#include "UI/ScreenAudit.h"

#include "App/Application.h"
#include "App/BunkerTheme.h"
#include "App/ProcessHardening.h"
#include "App/VaultSession.h"
#include "Core/Model.h"
#include "Core/Mutate.h"
#include "Core/GermanText.h"
#include "Core/VaultAudit.h"
#include "UI/BunkerFonts.h"
#include "UI/Card.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"
#include "UI/ScrollArea.h"
#include "Generated/FaIcons.h"

#include "imgui.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace Bunker::UI::Audit
{
    using Theme::Token;

    bool IsSerious(const Vault::AuditFinding& finding);
    std::string TagsOf(const Vault::AuditFinding& finding);

    namespace
    {
        bool g_open = false;
        std::uint64_t g_jumpTarget = 0;
        FindingKind g_filterRequest = FindingKind::None;

        Vault::AuditReport g_report;
        std::uint64_t g_reportRevision = 0;
        bool g_reportValid = false;

        const Vault::AuditReport& Report(VaultSession& session)
        {
            const std::uint64_t revision = session.Vault().Generation();
            if (!g_reportValid || revision != g_reportRevision)
            {
                g_report = Vault::Audit(session.Vault().Doc(), Vault::SystemUtcNow());
                g_reportRevision = revision;
                g_reportValid = true;
            }
            return g_report;
        }

        bool Tile(const char* id, const char* caption, std::size_t value, float width)
        {
            const Fonts::Set& f = Fonts::Current();
            const float height = Px(84.0f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            const bool warn = value != 0;
            bool clicked = false;
            bool hovered = false;
            if (warn)
            {
                clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
                hovered = ImGui::IsItemHovered();
                if (hovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    UI::Tooltip(T(Str::Audit_TileFilterTip));
                }
                ImGui::SetCursorScreenPos(at);
            }
            const float round = Theme::Round(Px(10.0f));
            const ImVec2 end(at.x + width, at.y + height);
            dl->AddRectFilled(at, end, Theme::U32(Token::Elevated, hovered ? 0.85f : 0.45f), round);
            dl->AddRect(at, end,
                        Theme::U32(hovered ? Token::WarnBright : (warn ? Token::WarnDim : Token::Line)),
                        round, 0, 1.0f);

            const float labelSize = Px(Fonts::Sizes::Label);
            const std::string label = UppercaseGerman(caption);
            dl->PushClipRect(at, end, true);
            dl->AddText(f.Mono, labelSize, ImVec2(at.x + Px(14.0f), at.y + Px(12.0f)),
                        Theme::U32(Token::TextMuted), label.c_str());

            char number[24];
            std::snprintf(number, sizeof(number), "%zu", value);
            dl->AddText(f.Strong, Px(Fonts::Sizes::Display),
                        ImVec2(at.x + Px(14.0f), at.y + Px(12.0f) + labelSize + Px(8.0f)),
                        Theme::U32(warn ? Token::WarnBright : Token::TextDisabled), number);
            dl->PopClipRect();

            ImGui::Dummy(ImVec2(width, height));
            return clicked;
        }

        void Sentence(const char* icon, const char* text, Token colour)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
            ImGui::TextWrapped("%s  %s", icon, text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
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

        void FindingRow(const Vault::Entry& entry, const Vault::AuditFinding& finding)
        {
            const Fonts::Set& f = Fonts::Current();
            const float rowHeight = Px(32.0f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;

            if (ImGui::Selectable("##finding", false, 0, ImVec2(0, rowHeight)))
                g_jumpTarget = finding.EntryId;
            if (ImGui::IsItemHovered())
                UI::Tooltip(T(Str::Audit_OpenEntry));

            const std::string tags = TagsOf(finding);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 tagSize =
                f.Text->CalcTextSizeA(Px(Fonts::Sizes::Small), FLT_MAX, 0.0f, tags.c_str());
            const float tagX = at.x + width - tagSize.x - Px(4.0f);
            const float textY = at.y + (rowHeight - Px(Fonts::Sizes::Body)) * 0.5f - Px(1.0f);

            const char* title = entry.Title.empty() ? T(Str::Audit_Untitled)
                                                    : entry.Title.c_str();
            dl->PushClipRect(ImVec2(at.x + Px(4), at.y),
                             ImVec2(tagX - Px(10), at.y + rowHeight), true);
            dl->AddText(f.Text, Px(Fonts::Sizes::Body), ImVec2(at.x + Px(4), textY),
                        Theme::U32(Token::TextSecondary), title);
            dl->PopClipRect();

            const bool serious = IsSerious(finding);
            dl->AddText(f.Text, Px(Fonts::Sizes::Small),
                        ImVec2(tagX, at.y + (rowHeight - Px(Fonts::Sizes::Small)) * 0.5f),
                        Theme::U32(serious ? Token::WarnBright : Token::TextMuted), tags.c_str());
        }
    }

    bool IsSerious(const Vault::AuditFinding& finding)
    {
        return finding.ReusedPassword || finding.WeakPassword || finding.CommonPassword;
    }

    std::string TagsOf(const Vault::AuditFinding& finding)
    {
        std::string tags;
        auto append = [&](const char* tag) {
            if (!tags.empty())
                tags += "  \xc2\xb7  ";
            tags += tag;
        };
        if (finding.ReusedPassword) append(T(Str::Audit_TagReused));
        if (finding.WeakPassword)   append(T(Str::Audit_TagWeak));
        if (finding.CommonPassword) append(T(Str::Audit_TagCommon));
        if (finding.NoSecondFactor) append(T(Str::Audit_TagNoTotp));
        if (finding.InsecureLink)   append(T(Str::Audit_TagInsecure));
        if (finding.OldPassword)    append(T(Str::Audit_TagOld));
        return tags;
    }

    FindingKind TakeFilterRequest()
    {
        const FindingKind kind = g_filterRequest;
        g_filterRequest = FindingKind::None;
        return kind;
    }

    void EntriesWith(VaultSession& session, FindingKind kind, std::vector<std::uint64_t>& out)
    {
        out.clear();
        for (const Vault::AuditFinding& f : Report(session).Findings)
        {
            const bool has = (kind == FindingKind::Reused && f.ReusedPassword) ||
                             (kind == FindingKind::Weak && f.WeakPassword) ||
                             (kind == FindingKind::Common && f.CommonPassword) ||
                             (kind == FindingKind::NoTotp && f.NoSecondFactor) ||
                             (kind == FindingKind::Insecure && f.InsecureLink);
            if (has)
                out.push_back(f.EntryId);
        }
    }

    const char* KindName(FindingKind kind)
    {
        switch (kind)
        {
        case FindingKind::Reused: return T(Str::Audit_TileReused);
        case FindingKind::Weak:   return T(Str::Audit_TileWeak);
        case FindingKind::Common: return T(Str::Audit_TileCommon);
        case FindingKind::NoTotp: return T(Str::Audit_TileNoTotp);
        case FindingKind::Insecure: return T(Str::Audit_TileInsecure);
        case FindingKind::None:   break;
        }
        return "";
    }

    const Vault::AuditFinding* FindingFor(VaultSession& session, std::uint64_t entryId)
    {
        for (const Vault::AuditFinding& f : Report(session).Findings)
            if (f.EntryId == entryId)
                return &f;
        return nullptr;
    }

    bool IsOpen() { return g_open; }

    void Open()
    {
        g_open = true;
        g_jumpTarget = 0;
    }

    void Close()
    {
        g_open = false;
        g_jumpTarget = 0;
        g_filterRequest = FindingKind::None;
    }

    void Forget()
    {
        g_report = Vault::AuditReport{};
        g_reportValid = false;
        g_reportRevision = 0;
        g_jumpTarget = 0;
    }

    std::size_t FindingCount(VaultSession& session)
    {
        return Report(session).Findings.size();
    }

    std::uint64_t TakeJumpTarget()
    {
        const std::uint64_t id = g_jumpTarget;
        g_jumpTarget = 0;
        return id;
    }

    bool Draw(Application& app, VaultSession& session)
    {
        (void)app;
        const Fonts::Set& f = Fonts::Current();
        const Vault::Document& doc = session.Vault().Doc();
        const Vault::AuditReport& report = Report(session);

        char closeLabel[96];
        std::snprintf(closeLabel, sizeof(closeLabel),
                      ICON_FA_XMARK "   %s", T(Str::Audit_Close));
        const float buttonHeight = Px(30.0f);
        const float closeWidth =
            Max(Px(120.0f), ImGui::CalcTextSize(closeLabel).x +
                            ImGui::GetStyle().FramePadding.x * 2.0f);

        const float headWidth = ScreenContentWidth(ImGui::GetContentRegionAvail().x);
        const ImVec2 headAt = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(
            f.Strong, Px(Fonts::Sizes::Title),
            ImVec2(headAt.x,
                   headAt.y + (buttonHeight - Px(Fonts::Sizes::Title)) * 0.5f),
            Theme::U32(Token::TextPrimary), T(Str::Audit_Title));

        ImGui::SetCursorScreenPos(
            ImVec2(headAt.x + headWidth - closeWidth, headAt.y));
        if (ImGui::Button(closeLabel, ImVec2(closeWidth, buttonHeight)))
            Close();
        ImGui::SetCursorScreenPos(ImVec2(headAt.x, headAt.y + buttonHeight));

        ImGui::Dummy(Px(0, 14));

        BeginScreenBody("##security", headWidth);

        const float gap = Px(12.0f);
        const float tileWidth =
            std::floor((ImGui::GetContentRegionAvail().x - gap * 4.0f) / 5.0f);
        if (Tile("##tile-reused", T(Str::Audit_TileReused), report.ReusedEntries, tileWidth))
            g_filterRequest = FindingKind::Reused;
        ImGui::SameLine(0, gap);
        if (Tile("##tile-weak", T(Str::Audit_TileWeak), report.WeakPasswords, tileWidth))
            g_filterRequest = FindingKind::Weak;
        ImGui::SameLine(0, gap);
        if (Tile("##tile-common", T(Str::Audit_TileCommon), report.CommonPasswords, tileWidth))
            g_filterRequest = FindingKind::Common;
        ImGui::SameLine(0, gap);
        if (Tile("##tile-notfa", T(Str::Audit_TileNoTotp), report.WithoutSecondFactor, tileWidth))
            g_filterRequest = FindingKind::NoTotp;
        ImGui::SameLine(0, gap);
        if (Tile("##tile-insecure", T(Str::Audit_TileInsecure), report.InsecureLinks, tileWidth))
            g_filterRequest = FindingKind::Insecure;
        ImGui::Dummy(Px(0, 4));

        CardColumns columns;
        columns.Begin(Px(760.0f), report.Clean() ? 2 : 3);

        BeginCard("##card-findings", columns.Next());
        CardHeader(ICON_FA_TRIANGLE_EXCLAMATION, T(Str::Audit_CardFindings), nullptr);

        char line[260];
        if (report.UnprotectedValues != 0)
        {
            if (report.UnprotectedValues == 1)
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, T(Str::Audit_UnprotectedOne),
                         Token::WarnBright);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_Unprotected),
                              report.UnprotectedValues);
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
            }
            Hint(T(Str::Audit_UnprotectedNote));
            ImGui::Dummy(Px(0, 8));
        }
        if (report.ReuseGroups != 0)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_Reused),
                          report.ReusedEntries, report.ReuseGroups);
            Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
            Hint(T(Str::Audit_ReuseNote));
            ImGui::Dummy(Px(0, 8));
        }
        if (report.WeakPasswords != 0)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_Weak), report.WeakPasswords);
            Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
            Hint(T(Str::Audit_WeakNote));
            ImGui::Dummy(Px(0, 8));
        }
        if (report.CommonPasswords != 0)
        {
            if (report.CommonPasswords == 1)
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, T(Str::Audit_CommonOne),
                         Token::WarnBright);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_Common),
                              report.CommonPasswords);
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
            }
            Hint(T(Str::Audit_CommonNote));
            ImGui::Dummy(Px(0, 8));
        }
        if (report.InsecureLinks != 0)
        {
            if (report.InsecureLinks == 1)
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, T(Str::Audit_InsecureOne),
                         Token::WarnBright);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_Insecure), report.InsecureLinks);
                Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
            }
            Hint(T(Str::Audit_InsecureNote));
            ImGui::Dummy(Px(0, 8));
        }
        if (report.WithoutSecondFactor != 0)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_NoTotp),
                          report.WithoutSecondFactor, report.Entries);
            Sentence(ICON_FA_CIRCLE_INFO, line, Token::TextSecondary);
            ImGui::Dummy(Px(0, 8));
        }

        if (report.SecondFactorNotOffered != 0)
        {
            if (report.SecondFactorNotOffered == 1)
                Sentence(ICON_FA_CIRCLE_INFO, T(Str::Audit_NotOfferedOne),
                         Token::TextMuted);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_NotOffered),
                              report.SecondFactorNotOffered);
                Sentence(ICON_FA_CIRCLE_INFO, line, Token::TextMuted);
            }
            ImGui::Dummy(Px(0, 8));
        }
        if (report.SecondFactorElsewhere != 0)
        {
            if (report.SecondFactorElsewhere == 1)
                Sentence(ICON_FA_CIRCLE_CHECK, T(Str::Audit_ElsewhereOne),
                         Token::GreenCore);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_Elsewhere),
                              report.SecondFactorElsewhere);
                Sentence(ICON_FA_CIRCLE_CHECK, line, Token::GreenCore);
            }
            ImGui::Dummy(Px(0, 8));
        }
        if (report.OldPasswords != 0)
        {
            if (report.OldPasswords == 1)
                Sentence(ICON_FA_CIRCLE_INFO, T(Str::Audit_OldOne), Token::TextSecondary);
            else
            {
                std::snprintf(line, sizeof(line), T(Str::Audit_Old),
                              report.OldPasswords);
                Sentence(ICON_FA_CIRCLE_INFO, line, Token::TextSecondary);
            }
            Hint(T(Str::Audit_OldNote));
            ImGui::Dummy(Px(0, 8));
        }

        if (report.Clean() && report.UnprotectedValues == 0)
        {
            Sentence(ICON_FA_CIRCLE_CHECK, T(Str::Audit_AllClear), Token::GreenCore);
            ImGui::Dummy(Px(0, 8));
        }

        if (ImGui::Button(T(Str::Audit_Recheck),
                          ImVec2(Min(Px(170.0f), ImGui::GetContentRegionAvail().x), Px(30.0f))))
            g_reportValid = false;
        ImGui::Dummy(Px(0, 6));
        Hint(T(Str::Audit_Note));
        EndCard();
        columns.Placed();

        if (!report.Clean())
        {
            char badge[24];
            std::snprintf(badge, sizeof(badge), "%zu", report.Findings.size());
            BeginCard("##card-affected", columns.Next());
            CardHeader(ICON_FA_LIST, T(Str::Audit_Affected), badge);
            bool first = true;
            for (const Vault::AuditFinding& finding : report.Findings)
            {
                const Vault::Entry* entry = Vault::FindEntry(doc, finding.EntryId);
                if (!entry)
                    continue;
                if (!first)
                {
                    const ImVec2 at = ImGui::GetCursorScreenPos();
                    const float y =
                        std::floor(at.y - ImGui::GetStyle().ItemSpacing.y * 0.5f) + 0.5f;
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(at.x, y), ImVec2(at.x + ImGui::GetContentRegionAvail().x, y),
                        Theme::U32(Token::Line, 0.75f), 1.0f);
                }
                first = false;
                ImGui::PushID(static_cast<int>(finding.EntryId));
                FindingRow(*entry, finding);
                ImGui::PopID();
            }
            EndCard();
            columns.Placed();
        }

        BeginCard("##card-hardening", columns.Next());
        CardHeader(ICON_FA_SHIELD_HALVED, T(Str::Audit_CardHardening), nullptr);
        Hint(T(Str::Audit_HardeningNote));
        ImGui::Dummy(Px(0, 8));

        const HardeningReport& hardening = LastHardeningReport();

        auto fact = [&](bool on, const char* text)
        {
            if (on)
            {
                Sentence(ICON_FA_CIRCLE_CHECK, text, Token::GreenCore);
            }
            else
            {
                char off[300];
                std::snprintf(off, sizeof(off), "%s \xe2\x80\x93 %s", text,
                              T(Str::Audit_HardeningOff));
                Sentence(ICON_FA_XMARK, off, Token::TextMuted);
            }
        };

        fact(hardening.HandlersInstalled && hardening.SodiumMisuseHooked,
             T(Str::Audit_CrashWipe));

        if (hardening.DumpsExcluded)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_DumpsExcluded),
                          hardening.ExcludedRegions);
            Sentence(ICON_FA_CIRCLE_CHECK, line, Token::GreenCore);
        }
        else
        {
            Sentence(ICON_FA_XMARK, T(Str::Audit_DumpsUnavailable), Token::TextMuted);
        }
        if (hardening.ExclusionsRefused != 0)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_DumpsRefused),
                          hardening.ExclusionsRefused);
            Sentence(ICON_FA_TRIANGLE_EXCLAMATION, line, Token::WarnBright);
        }

        if (hardening.LockedMemoryVerified)
        {
            std::snprintf(line, sizeof(line), T(Str::Audit_MemoryLocked),
                          hardening.LockedMemoryBudget / (1024u * 1024u));
            Sentence(ICON_FA_CIRCLE_CHECK, line, Token::GreenCore);
        }
        else
        {
            Sentence(ICON_FA_TRIANGLE_EXCLAMATION,
                     T(Str::Audit_MemoryNotLocked), Token::WarnBright);
        }

        if (hardening.AccessRestricted)
        {
            Sentence(ICON_FA_CIRCLE_CHECK, T(Str::Audit_NoOutsideReads),
                     Token::GreenCore);
        }
        else
        {
            Sentence(ICON_FA_XMARK, T(Str::Audit_OutsideReadsOpen),
                     Token::TextMuted);
        }

        fact(hardening.DynamicCodeDisabled, T(Str::Audit_NoDynamicCode));
        fact(hardening.ExtensionPointsDisabled, T(Str::Audit_NoExtensionPoints));
        fact(hardening.StrictHandleChecks, T(Str::Audit_StrictHandles));

        ImGui::Dummy(Px(0, 8));
        Hint(T(Str::Audit_HardeningLimits));
        EndCard();
        columns.Placed();

        columns.End();
        ImGui::Dummy(Px(0, 10));

        ScrollFade(Token::Panel);
        EndScreenBody();
        return false;
    }
}
