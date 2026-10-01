#include "UI/ScreenUnlock.h"

#include "App/Application.h"
#include "App/BunkerTheme.h"
#include "App/VaultSession.h"
#include "Core/GermanText.h"
#include "Core/Language.h"
#include "Core/PasswordStrength.h"
#include "UI/BunkerFonts.h"
#include "UI/Motion.h"
#include "UI/SecretInput.h"
#include "UI/Shortcuts.h"
#include "UI/UiMath.h"
#include "UI/UiScale.h"
#include "Generated/FaIcons.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Bunker::UI
{
    using Theme::Token;

    namespace
    {
        SecretField& PasswordField()
        {
            static SecretField field(512);
            return field;
        }
        SecretField& ConfirmField()
        {
            static SecretField field(512);
            return field;
        }

        float g_shakeUntil = 0.0f;
        bool g_focusRequested = true;
        bool g_showMismatch = false;

        constexpr float kPanelWidth = 440.0f;

        constexpr float kGapTight = 6.0f;
        constexpr float kGapStep = 12.0f;
        constexpr float kGapWide = 24.0f;

        float SmallLine()
        {
            return Fonts::Current().Text->CalcTextSizeA(
                Px(Fonts::Sizes::Small), FLT_MAX, 0.0f, "Ag").y;
        }

        class MinHeight
        {
        public:
            explicit MinHeight(float height)
                : m_top(ImGui::GetCursorPosY()), m_height(height) {}

            ~MinHeight()
            {
                const float used = ImGui::GetCursorPosY() - m_top;
                const float fill = m_height - used;
                if (fill > 0.0f)
                    ImGui::Dummy(ImVec2(0.0f, fill));
            }

            MinHeight(const MinHeight&) = delete;
            MinHeight& operator=(const MinHeight&) = delete;

        private:
            float m_top;
            float m_height;
        };

        void WipeFields()
        {
            PasswordField().Clear();
            ConfirmField().Clear();
            g_showMismatch = false;
        }

        float ShakeOffset()
        {
            const float now = static_cast<float>(ImGui::GetTime());
            if (now >= g_shakeUntil)
                return 0.0f;
            const float remaining = g_shakeUntil - now;
            return std::sin(remaining * 42.0f) * remaining * Px(24.0f);
        }

        std::string FitPath(const std::string& path, ImFont* font, float size, float maxWidth)
        {
            if (font->CalcTextSizeA(Px(size), FLT_MAX, 0.0f, path.c_str()).x <= maxWidth)
                return path;

            const std::string ellipsis = " … ";
            std::size_t head = 3;
            std::size_t tail = path.size() > 3 ? path.size() - 3 : 0;
            std::string best = path;
            while (head < tail)
            {
                const std::size_t h = Utf8SnapBack(path, head);
                const std::size_t t = Utf8SnapBack(path, tail);
                if (h >= t)
                    break;
                const std::string candidate = path.substr(0, h) + ellipsis + path.substr(t);
                if (font->CalcTextSizeA(Px(size), FLT_MAX, 0.0f, candidate.c_str()).x > maxWidth)
                    break;
                best = candidate;
                if (head < path.size() / 2) ++head;
                if (tail > head + 1) --tail;
            }
            if (font->CalcTextSizeA(Px(size), FLT_MAX, 0.0f, best.c_str()).x > maxWidth)
            {
                const std::size_t slash = path.find_last_of("\\/");
                best = (slash == std::string::npos) ? path : path.substr(slash + 1);
            }
            return best;
        }

        void CentredText(ImFont* font, float size, Token colour, const char* text)
        {
            const ImVec2 t = font->CalcTextSizeA(Px(size), FLT_MAX, 0.0f, text);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (ImGui::GetContentRegionAvail().x - t.x) * 0.5f);
            ImGui::PushFont(font, size);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        constexpr int kBolts = 12;
        constexpr int kSpokes = 5;
        constexpr int kCircleSegments = 64;
        constexpr double kBoltsSeconds = 0.35;
        constexpr double kRetractSeconds = 0.30;
        constexpr double kOpenSeconds = 0.55;
        constexpr double kFadeSeconds = 0.20;
        constexpr double kSettleSeconds = 0.70;

        struct DoorMotion
        {
            double BusySince = -1.0;
            double SettleSince = -1.0;
            float SettleFrom = 0.0f;
            double BoltsSince = -1.0;
            double OpenSince = -1.0;
            bool Done = false;
        };
        DoorMotion g_door;

        int g_lastWhich = 0;

        float SmoothStep(double x)
        {
            const float t = Clamp(static_cast<float>(x), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float Spin(double t)
        {
            constexpr double ramp = 0.4;
            constexpr double speed = 3.2;
            return static_cast<float>(t < ramp ? speed * t * t / (2.0 * ramp)
                                               : speed * (t - ramp * 0.5));
        }

        float SpokeResidual(float angle)
        {
            const float step = 2.0f * kPi / kSpokes;
            float r = std::fmod(angle, step);
            if (r > step * 0.5f)
                r -= step;
            return r;
        }

        float WheelAngle(double now)
        {
            const float rest = -kPi * 0.5f;
            if (g_door.BusySince >= 0.0)
                return rest + Spin(now - g_door.BusySince);
            if (g_door.SettleSince >= 0.0)
            {
                const double t = now - g_door.SettleSince;
                if (t < kSettleSeconds)
                    return rest + g_door.SettleFrom *
                                      static_cast<float>(std::exp(-7.0 * t) * std::cos(16.0 * t));
            }
            return rest;
        }

        ImVec4 Mix(ImVec4 a, ImVec4 b, float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                          a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        void DrawDoorAt(ImDrawList* dl, ImVec2 c, float radius, bool open)
        {
            const double now = ImGui::GetTime();

            float extension = open ? 0.0f : 1.0f;
            if (!open && g_door.BoltsSince >= 0.0)
                extension = SmoothStep((now - g_door.BoltsSince) / kBoltsSeconds);
            if (g_door.OpenSince >= 0.0)
                extension = Min(extension,
                                1.0f - SmoothStep((now - g_door.OpenSince) / kRetractSeconds));

            float alarm = 0.0f;
            if (g_door.SettleSince >= 0.0 && now - g_door.SettleSince < kSettleSeconds)
                alarm = static_cast<float>(std::exp(-4.0 * (now - g_door.SettleSince)));

            dl->AddCircleFilled(c, radius, Theme::U32(Token::Panel), kCircleSegments);
            dl->AddCircle(c, radius, Theme::U32(Token::LineStrong), kCircleSegments, Px(3.0f));
            dl->AddCircle(c, radius * 0.80f, Theme::U32(Token::Line), kCircleSegments, Px(2.0f));

            const ImU32 boltColour = ImGui::ColorConvertFloat4ToU32(
                Mix(Theme::Color(Token::AccentCore), Theme::Color(Token::LineStrong), extension));
            for (int i = 0; i < kBolts; ++i)
            {
                const float a = (2.0f * kPi / kBolts) * (static_cast<float>(i) + 0.5f);
                const ImVec2 dir(std::cos(a), std::sin(a));
                const float r0 = radius * 0.84f;
                const float r1 = radius * (0.87f + 0.09f * extension);
                dl->AddLine(ImVec2(c.x + dir.x * r0, c.y + dir.y * r0),
                            ImVec2(c.x + dir.x * r1, c.y + dir.y * r1), boltColour,
                            Max(Px(3.0f), radius * 0.055f));
            }

            const ImU32 wheel = ImGui::ColorConvertFloat4ToU32(
                Mix(Theme::Color(Token::AccentCore), Theme::Color(Token::RedCore), alarm));
            const ImU32 grip = ImGui::ColorConvertFloat4ToU32(
                Mix(Theme::Color(Token::AccentBright), Theme::Color(Token::RedBright), alarm));
            const float rim = radius * 0.44f;
            const float angle = WheelAngle(now);
            const float stroke = Max(Px(3.0f), radius * 0.042f);
            dl->AddCircle(c, rim, wheel, kCircleSegments, stroke);
            for (int s = 0; s < kSpokes; ++s)
            {
                const float a = angle + (2.0f * kPi / kSpokes) * static_cast<float>(s);
                const ImVec2 dir(std::cos(a), std::sin(a));
                const float reach = rim + radius * 0.09f;
                dl->AddLine(c, ImVec2(c.x + dir.x * reach, c.y + dir.y * reach), wheel, stroke);
                dl->AddCircleFilled(ImVec2(c.x + dir.x * reach, c.y + dir.y * reach),
                                    Max(Px(3.5f), radius * 0.055f), grip, 16);
            }
            dl->AddCircleFilled(c, radius * 0.13f, wheel, 32);
            dl->AddCircleFilled(c, radius * 0.05f, Theme::U32(Token::Panel), 16);
        }

        struct Band
        {
            Token Colour;
            float Strength;
            float Centre;
            float Amplitude;
            float Waves;
            float Speed;
            float Width;
            float Phase;
        };

        constexpr Band kBands[] = {
            { Token::AccentDim,  0.80f, 0.26f, 0.09f, 0.85f,  0.045f, 0.120f, 0.0f },
            { Token::GlowCore,   0.30f, 0.50f, 0.12f, 0.65f, -0.032f, 0.090f, 1.9f },
            { Token::LineStrong, 0.85f, 0.80f, 0.07f, 1.15f,  0.038f, 0.110f, 3.7f },
            { Token::AccentCore, 0.20f, 0.62f, 0.13f, 0.45f, -0.050f, 0.060f, 5.1f },
            { Token::GlowDim,    0.70f, 0.08f, 0.06f, 1.40f,  0.028f, 0.080f, 2.6f },
        };

        ImU32 SilkColour(float u, float v, float t)
        {
            constexpr float kTau = 6.2831853f;
            const float bend = 0.05f * std::sin(kTau * (u * 1.3f + t * 0.017f)) +
                               0.03f * std::sin(kTau * (u * 2.9f - t * 0.023f) + 1.3f);
            const float y = v + bend;

            const ImVec4 ground = Theme::Color(Token::Void);
            float r = ground.x, g = ground.y, b = ground.z;
            for (const Band& band : kBands)
            {
                const float centre =
                    band.Centre +
                    band.Amplitude * std::sin(kTau * (band.Waves * u + band.Speed * t) + band.Phase);
                const float d = (y - centre) / band.Width;
                const float glow = band.Strength * std::exp(-d * d);
                const ImVec4 c = Theme::Color(band.Colour);
                r += (c.x - ground.x) * glow;
                g += (c.y - ground.y) * glow;
                b += (c.z - ground.z) * glow;
            }
            const float dx = (u - 0.5f) * 2.0f;
            const float dy = (v - 0.45f) * 2.0f;
            const float vignette = Clamp(1.0f - 0.38f * (dx * dx + dy * dy), 0.45f, 1.0f);
            const float dim = 0.50f * vignette;
            r = ground.x + (r - ground.x) * dim;
            g = ground.y + (g - ground.y) * dim;
            b = ground.z + (b - ground.z) * dim;
            return ImGui::ColorConvertFloat4ToU32(
                ImVec4(Clamp(r, 0.0f, 1.0f), Clamp(g, 0.0f, 1.0f), Clamp(b, 0.0f, 1.0f), 1.0f));
        }

        void DrawAurora(ImDrawList* dl, ImVec2 min, ImVec2 max)
        {
            const float t = Motion::Enabled() ? static_cast<float>(ImGui::GetTime()) : 0.0f;
            const ImVec2 size(max.x - min.x, max.y - min.y);
            if (size.x <= 1.0f || size.y <= 1.0f)
                return;

            const int cols = Clamp(static_cast<int>(size.x / Px(24.0f)), 24, 96);
            const int rows = Clamp(static_cast<int>(size.y / Px(24.0f)), 16, 64);
            const int stride = cols + 1;
            const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

            dl->PrimReserve(cols * rows * 6, stride * (rows + 1));
            const unsigned int base = dl->_VtxCurrentIdx;
            for (int row = 0; row <= rows; ++row)
            {
                const float v = static_cast<float>(row) / static_cast<float>(rows);
                for (int col = 0; col <= cols; ++col)
                {
                    const float u = static_cast<float>(col) / static_cast<float>(cols);
                    dl->PrimWriteVtx(ImVec2(min.x + size.x * u, min.y + size.y * v), uv,
                                     SilkColour(u, v, t));
                }
            }
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col)
                {
                    const unsigned int a = base + static_cast<unsigned int>(row * stride + col);
                    const unsigned int b = a + 1;
                    const unsigned int c = a + static_cast<unsigned int>(stride);
                    const unsigned int d = c + 1;
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(b));
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(a));
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(d));
                    dl->PrimWriteIdx(static_cast<ImDrawIdx>(c));
                }
        }

        float Pill(ImDrawList* dl, float right, float centreY, const char* text, Token fg, Token bg)
        {
            const Fonts::Set& f = Fonts::Current();
            const float size = Px(Fonts::Sizes::Label);
            const ImVec2 t = f.Mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            const ImVec2 box(t.x + Px(20.0f), t.y + Px(10.0f));
            const ImVec2 p0(right - box.x, centreY - box.y * 0.5f);
            const ImVec2 p1(right, centreY + box.y * 0.5f);
            dl->AddRectFilled(p0, p1, Theme::U32(bg, 0.55f), Theme::Round(box.y * 0.5f));
            dl->AddRect(p0, p1, Theme::U32(fg, 0.35f), Theme::Round(box.y * 0.5f), 0, 1.0f);
            dl->AddText(f.Mono, size, ImVec2(p0.x + Px(10.0f), p0.y + Px(5.0f)), Theme::U32(fg), text);
            return box.x;
        }

        void DrawGateHeader(Application& app, int which, bool opening, ImVec2 min, ImVec2 max,
                            float margin, float height)
        {
            const Fonts::Set& f = Fonts::Current();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            const float tile = Px(40.0f);
            const float x = min.x + margin;
            const float y = min.y + (height - tile) * 0.5f;
            const ImVec2 t0(x, y);
            const ImVec2 t1(x + tile, y + tile);
            dl->AddRectFilled(t0, t1, Theme::U32(Token::AccentCore, 0.14f), Theme::Round(Px(10.0f)));
            dl->AddRect(t0, t1, Theme::U32(Token::AccentCore, 0.45f), Theme::Round(Px(10.0f)), 0, 1.0f);
            {
                const float size = Px(Fonts::Sizes::Body) * 1.3f;
                const ImVec2 g = f.Strong->CalcTextSizeA(size, FLT_MAX, 0.0f, ICON_FA_VAULT);
                dl->AddText(f.Strong, size,
                            ImVec2(x + (tile - g.x) * 0.5f, y + (tile - g.y) * 0.5f),
                            Theme::U32(Token::AccentCore), ICON_FA_VAULT);
            }
            const float nameSize = Px(Fonts::Sizes::Body) + Px(3.0f);
            const float tagSize = Px(Fonts::Sizes::Label);
            dl->AddText(f.Strong, nameSize, ImVec2(t1.x + Px(12.0f), y + Px(1.0f)),
                        Theme::U32(Token::TextPrimary), "Vault");
            dl->AddText(f.Mono, tagSize, ImVec2(t1.x + Px(12.0f), y + nameSize + Px(5.0f)),
                        Theme::U32(Token::TextMuted), T(Str::Main_BrandTagline));

            const float centreY = min.y + height * 0.5f;
            float right = max.x - margin;
            const std::string& machine = app.MachineName();
            if (!machine.empty())
            {
                const ImVec2 m = f.Mono->CalcTextSizeA(tagSize, FLT_MAX, 0.0f, machine.c_str());
                dl->AddText(f.Mono, tagSize, ImVec2(right - m.x, centreY - m.y * 0.5f),
                            Theme::U32(Token::TextMuted), machine.c_str());
                right -= m.x + Px(16.0f);
            }
            if (opening)
                Pill(dl, right, centreY, T(Str::Unlock_StateOpen), Token::GreenCore, Token::GreenDim);
            else if (which == 1)
                Pill(dl, right, centreY, T(Str::Unlock_StateNew), Token::AccentBright, Token::AccentDim);
            else
                Pill(dl, right, centreY, T(Str::Unlock_StateLocked), Token::RedBright, Token::RedDim);
        }

        void DrawGateFooter(ImVec2 min, ImVec2 max, float margin, float height)
        {
            const Fonts::Set& f = Fonts::Current();
            const float size = Px(Fonts::Sizes::Label);
            ImGui::GetWindowDrawList()->AddText(
                f.Mono, size, ImVec2(min.x + margin, max.y - height * 0.5f - size * 0.5f),
                Theme::U32(Token::TextDisabled), "MANIFOLD");
        }

        void StrengthReadout(const SecretField& field, const Crypto::KdfParams& params)
        {
            const Fonts::Set& f = Fonts::Current();
            if (field.IsEmpty())
                return;

            const PasswordStrength s = EstimateStrength(field.Data(), field.Length(), params);

            Token colour = Token::RedCore;
            const double years = s.SecondsToCrack / (365.25 * 24 * 3600);
            if (years >= 1.0e6)      colour = Token::GreenCore;
            else if (years >= 100.0) colour = Token::WarnBright;
            else if (years >= 1.0)   colour = Token::WarnCore;

            const float fill = Clamp(static_cast<float>(
                (std::log10(Max(s.SecondsToCrack, 1.0)) / 20.0)), 0.02f, 1.0f);

            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + Px(4)), Theme::U32(Token::Line), Px(2.0f));
            dl->AddRectFilled(p, ImVec2(p.x + w * fill, p.y + Px(4)), Theme::U32(colour), Px(2.0f));
            ImGui::Dummy(Px(0, 10));

            ImGui::PushFont(f.Medium, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
            ImGui::TextUnformatted(StrengthVerdict(s));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            char line[192];
            std::snprintf(line, sizeof(line), T(Str::Unlock_BruteForce),
                          HumanDuration(s.SecondsToCrack).c_str());
            ImGui::PushFont(f.Text, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(colour));
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextWrapped("%s", s.LooksRepetitive
                ? T(Str::Unlock_Repetitive)
                : T(Str::Unlock_AssumesRandom));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        void CapsLine()
        {
            const Fonts::Set& f = Fonts::Current();
            const bool on = SecretField::CapsLockOn();
            ImGui::PushFont(f.Mono, Fonts::Sizes::Label);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  Theme::Color(on ? Token::WarnBright : Token::TextMuted));
            ImGui::TextUnformatted(T(on ? Str::Unlock_CapsOn : Str::Unlock_CapsOff));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        void FieldLabel(const char* text, const char* tag)
        {
            const Fonts::Set& f = Fonts::Current();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::PushFont(f.Medium, Fonts::Sizes::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            if (tag && *tag)
            {
                const float size = Px(Fonts::Sizes::Label);
                const ImVec2 t = f.Mono->CalcTextSizeA(size, FLT_MAX, 0.0f, tag);
                ImGui::GetWindowDrawList()->AddText(f.Mono, size,
                                                    ImVec2(at.x + w - t.x, at.y + Px(2.0f)),
                                                    Theme::U32(Token::AccentCore), tag);
            }
            ImGui::Dummy(Px(0, 2));
        }

        void CardTop(const char* title, const char* line, const char* icon)
        {
            const Fonts::Set& f = Fonts::Current();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const float badge = Px(40.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 bc(at.x + w - badge * 0.5f, at.y + badge * 0.5f);
            dl->AddCircleFilled(bc, badge * 0.5f, Theme::U32(Token::AccentCore, 0.14f), 32);
            dl->AddCircle(bc, badge * 0.5f, Theme::U32(Token::AccentCore, 0.45f), 32, 1.0f);
            const float iconSize = Px(Fonts::Sizes::Body);
            const ImVec2 it = f.Text->CalcTextSizeA(iconSize, FLT_MAX, 0.0f, icon);
            dl->AddText(f.Text, iconSize, ImVec2(bc.x - it.x * 0.5f, bc.y - it.y * 0.5f),
                        Theme::U32(Token::AccentCore), icon);

            ImGui::PushFont(f.Strong, Fonts::Sizes::Display);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PushFont(f.Text, Fonts::Sizes::Body);
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
            ImGui::TextWrapped("%s", line);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        bool PrimaryButton(const char* label, bool enabled)
        {
            ImGui::BeginDisabled(!enabled);
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Color(Token::AccentDim));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Color(Token::AccentCore, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Color(Token::AccentCore, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextPrimary));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Round(Px(10.0f)));
            const bool hit = ImGui::Button(label, ImVec2(-1.0f, Px(46.0f)));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            ImGui::EndDisabled();
            return hit;
        }

        bool GateField(SecretField& field, const char* id, float width, bool focus)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Theme::Round(Px(10.0f)));
            const bool submitted = field.Draw(id, "", width, focus, Px(48.0f));
            ImGui::PopStyleVar();
            return submitted;
        }

        void PathLine(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            ImGui::Dummy(Px(0, kGapStep));
            CentredText(f.Text, Fonts::Sizes::Label, Token::TextDisabled,
                        FitPath(session.Path(), f.Text, Fonts::Sizes::Label,
                                ImGui::GetContentRegionAvail().x).c_str());
        }

        void DrawUnlock(Application& app, VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            const bool busy = session.IsBusy();
            const double lockout = session.LockoutRemaining();

            CardTop(T(Str::Unlock_Title), T(Str::Unlock_Welcome), ICON_FA_LOCK);
            ImGui::Dummy(Px(0, kGapStep + 6.0f));

            if (busy)
            {
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                ImGui::TextUnformatted(T(Str::Unlock_Working));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::Dummy(Px(0, kGapTight));
                ImGui::PushFont(f.Text, Fonts::Sizes::Label);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                ImGui::TextUnformatted(T(Str::Unlock_TakesAWhile));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                PathLine(session);
                return;
            }

            FieldLabel(T(Str::Unlock_MasterPassword), T(Str::Unlock_LocalTag));

            const float fieldWidth = ImGui::GetContentRegionAvail().x;
            const float shake = ShakeOffset();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shake);
            const bool submitted = GateField(PasswordField(), "master", fieldWidth, g_focusRequested);
            g_focusRequested = false;

            ImGui::Dummy(Px(0, kGapTight));
            CapsLine();
            ImGui::Dummy(Px(0, kGapStep));

            {
                const MinHeight slot(SmallLine());
                if (lockout > 0.0)
                {
                    char msg[128];
                    std::snprintf(msg, sizeof(msg), T(Str::Unlock_Lockout), lockout);
                    ImGui::PushFont(f.Medium, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                    ImGui::TextUnformatted(msg);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                else if (session.LastErrorCode() != ErrorCode::Ok)
                {
                    ImGui::PushFont(f.Medium, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                    ImGui::TextUnformatted(session.LastMessage());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                else if (app.LastLockReason() != LockReason::None &&
                         app.LastLockReason() != LockReason::Manual)
                {
                    ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::TextWrapped("%s", ToUserMessage(app.LastLockReason()));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }

            ImGui::Dummy(Px(0, kGapStep));

            const bool canSubmit = !PasswordField().IsEmpty() && lockout <= 0.0;
            char unlockLabel[96];
            std::snprintf(unlockLabel, sizeof(unlockLabel),
                          ICON_FA_LOCK_OPEN "   %s", T(Str::Unlock_UnlockButton));
            const bool clicked = PrimaryButton(unlockLabel, canSubmit);

            if ((clicked || submitted) && canSubmit)
            {
                session.BeginUnlock(PasswordField().Data(), PasswordField().Length());
                PasswordField().Clear();
            }

            PathLine(session);
        }

        void DrawCreate(VaultSession& session)
        {
            const Fonts::Set& f = Fonts::Current();
            const Crypto::KdfParams params = Crypto::KdfParams::Moderate();

            if (session.IsBusy())
            {
                CardTop(T(Str::Unlock_Creating), T(Str::Unlock_CreateWelcome),
                        ICON_FA_SHIELD_HALVED);
                ImGui::Dummy(Px(0, kGapStep + 6.0f));
                ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextSecondary));
                ImGui::TextUnformatted(T(Str::Unlock_Working));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                PathLine(session);
                return;
            }

            CardTop(T(Str::Unlock_CreateTitle), T(Str::Unlock_CreateWelcome), ICON_FA_SHIELD_HALVED);
            ImGui::Dummy(Px(0, kGapStep + 6.0f));

            const float width = ImGui::GetContentRegionAvail().x;

            bool focusConfirm = false;
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
            {
                if (PasswordField().HasFocus())
                {
                    PasswordField().SetFocus(false);
                    focusConfirm = true;
                }
                else if (ConfirmField().HasFocus())
                {
                    ConfirmField().SetFocus(false);
                    PasswordField().SetFocus(true);
                }
            }

            FieldLabel(T(Str::Unlock_MasterPassword), T(Str::Unlock_LocalTag));
            GateField(PasswordField(), "neu", width, g_focusRequested);
            g_focusRequested = false;
            ImGui::Dummy(Px(0, kGapStep));

            FieldLabel(T(Str::Unlock_Again), nullptr);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ShakeOffset());
            const bool submitted = GateField(ConfirmField(), "bestaetigen", width, focusConfirm);

            ImGui::Dummy(Px(0, kGapTight));
            CapsLine();
            ImGui::Dummy(Px(0, kGapStep));

            {
                const MinHeight slot(SmallLine() * 5.0f + Px(14.0f));
                StrengthReadout(PasswordField(), params);
            }

            const bool match = PasswordField().Length() == ConfirmField().Length() &&
                               (PasswordField().Length() == 0 ||
                                std::memcmp(PasswordField().Data(), ConfirmField().Data(),
                                            PasswordField().Length()) == 0);

            ImGui::Dummy(Px(0, kGapTight));
            {
                const MinHeight slot(SmallLine() * 2.0f);
                if (g_showMismatch && !match)
                {
                    ImGui::PushFont(f.Medium, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::RedBright));
                    ImGui::TextUnformatted(T(Str::Unlock_Mismatch));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                else
                {
                    ImGui::PushFont(f.Text, Fonts::Sizes::Small);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Color(Token::TextMuted));
                    ImGui::TextWrapped("%s", T(Str::Unlock_NoBackDoor));
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }

            ImGui::Dummy(Px(0, kGapStep));

            const bool ready = !PasswordField().IsEmpty() && !ConfirmField().IsEmpty();
            char createLabel[96];
            std::snprintf(createLabel, sizeof(createLabel),
                          ICON_FA_SHIELD_HALVED "   %s", T(Str::Unlock_CreateButton));
            const bool clicked = PrimaryButton(createLabel, ready);

            if ((clicked || submitted) && ready)
            {
                if (!match)
                {
                    g_showMismatch = true;
                    g_shakeUntil = static_cast<float>(ImGui::GetTime()) + 0.45f;
                    ConfirmField().Clear();
                }
                else
                {
                    session.BeginCreate(PasswordField().Data(), PasswordField().Length(), params);
                    WipeFields();
                }
            }

            PathLine(session);
        }
    }

    bool GateHoldsTheme()
    {
        return g_door.OpenSince >= 0.0 && !g_door.Done;
    }

    bool GateOpening(Application& app, VaultSession& session)
    {
        using State = VaultSession::State;
        static bool first = true;
        static State previous = State::NoVaultYet;
        const State state = session.GetState();
        const double now = ImGui::GetTime();

        if (first)
        {
            first = false;
            previous = state;
            g_door.BoltsSince = now;
        }
        else if (state != previous)
        {
            if (state == State::Working)
            {
                g_door.BusySince = now;
                g_door.SettleSince = -1.0;
            }
            else if (previous == State::Working)
            {
                WipeFields();
                g_focusRequested = true;
                if (state == State::Unlocked)
                {
                    g_door.OpenSince = now;
                    g_door.Done = false;
                }
                else
                {
                    g_door.SettleFrom = g_door.BusySince >= 0.0
                                            ? SpokeResidual(Spin(now - g_door.BusySince))
                                            : 0.0f;
                    g_door.SettleSince = now;
                    g_door.BusySince = -1.0;
                    if (session.LastErrorCode() == ErrorCode::WrongPassword)
                        g_shakeUntil = static_cast<float>(now) + 0.45f;
                }
            }
            if (state == State::Unlocked)
            {
                WipeFields();
                app.ClearLastLockReason();
            }
            if (previous == State::Unlocked)
            {
                g_door = DoorMotion{};
                g_door.BoltsSince = now;
                g_focusRequested = true;
            }
            previous = state;
        }

        if (state != State::Unlocked)
            return false;
        if (g_door.OpenSince < 0.0)
            return false;
        if (g_door.Done)
        {
            g_door = DoorMotion{};
            return false;
        }
        return true;
    }

    void DrawGate(Application& app, VaultSession& session)
    {
        const VaultSession::State state = session.GetState();
        const bool opening = state == VaultSession::State::Unlocked;

        ImGui::SetNavCursorVisible(false);
        Shortcuts::ClaimNavigationKeys();
        Shortcuts::Claim(Shortcuts::Action::EditEntry);

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Color(Token::Void));
        ImGui::Begin("##gate", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawAurora(dl, windowMin, windowMax);

        if (state == VaultSession::State::NoVaultYet)
            g_lastWhich = 1;
        else if (state == VaultSession::State::Locked)
            g_lastWhich = 0;
        const int which = g_lastWhich;

        const float margin = Px(40.0f);
        const float headHeight = Px(80.0f);
        const float footHeight = Px(44.0f);
        DrawGateHeader(app, which, opening, windowMin, windowMax, margin, headHeight);
        DrawGateFooter(windowMin, windowMax, margin, footHeight);

        static float measured[2] = { 0.0f, 0.0f };
        const ImVec2 areaMin(windowMin.x + margin, windowMin.y + headHeight);
        const ImVec2 areaMax(windowMax.x - margin, windowMax.y - footHeight);
        const float areaWidth = Max(Px(200.0f), areaMax.x - areaMin.x);
        const float areaHeight = Max(Px(200.0f), areaMax.y - areaMin.y);
        const bool wide = areaWidth >= Px(820.0f);
        const float cardWidth = Min(Px(kPanelWidth), areaWidth);
        const float estimate = Px(which ? 640.0f : 440.0f);
        const float cardHeight = measured[which] > 0.0f ? measured[which] : estimate;

        float doorRadius = 0.0f;
        ImVec2 doorCentre;
        ImVec2 cardAt;
        float roof = 0.0f;
        if (wide)
        {
            const float span = Min(areaWidth, Px(1100.0f));
            const float spanLeft = areaMin.x + (areaWidth - span) * 0.5f;
            const float half = span * 0.5f;
            const float centreY = areaMin.y + areaHeight * 0.47f;
            doorRadius = Px(96.0f);
            doorCentre = ImVec2(spanLeft + half * 0.5f, centreY);
            roof = areaHeight;
            cardAt = ImVec2(spanLeft + half + (half - cardWidth) * 0.5f,
                            Max(areaMin.y, centreY - Min(cardHeight, roof) * 0.5f));

            const float side = doorRadius * 2.0f + Px(84.0f);
            const ImVec2 n0(doorCentre.x - side * 0.5f, doorCentre.y - side * 0.5f);
            const ImVec2 n1(doorCentre.x + side * 0.5f, doorCentre.y + side * 0.5f);
            dl->AddRectFilled(n0, n1, Theme::U32(Token::Elevated, 0.45f), Theme::Round(Px(28.0f)));
            dl->AddRect(n0, n1, Theme::U32(Token::Line), Theme::Round(Px(28.0f)), 0, 1.0f);
        }
        else
        {
            doorRadius = Px(48.0f);
            float doorBlock = doorRadius * 2.0f + Px(24.0f);
            if (areaHeight < doorBlock + cardHeight)
            {
                doorRadius = 0.0f;
                doorBlock = 0.0f;
            }
            roof = Max(Px(160.0f), areaHeight - doorBlock);
            const float total = doorBlock + Min(cardHeight, roof);
            const float top = areaMin.y + Max(0.0f, (areaHeight - total) * 0.42f);
            doorCentre = ImVec2(areaMin.x + areaWidth * 0.5f, top + doorRadius);
            cardAt = ImVec2(areaMin.x + (areaWidth - cardWidth) * 0.5f, top + doorBlock);
        }
        if (doorRadius > 0.0f)
            DrawDoorAt(dl, doorCentre, doorRadius, which == 1);

        if (!opening)
        {
            ImGui::SetCursorScreenPos(cardAt);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(30, 24));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Theme::Round(Px(18.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Color(Token::Elevated, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Color(Token::Line));
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, roof));
            ImGui::BeginChild("##panel", ImVec2(cardWidth, 0.0f),
                              ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY |
                                  ImGuiChildFlags_Borders);
            if (which == 1)
                DrawCreate(session);
            else
                DrawUnlock(app, session);
            const float contentHeight =
                ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
            ImGui::EndChild();
            if (!session.IsBusy())
                measured[which] = contentHeight;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }

        if (opening)
        {
            const double t = ImGui::GetTime() - g_door.OpenSince;
            const float fade = SmoothStep((t - (kOpenSeconds - kFadeSeconds)) / kFadeSeconds);
            if (fade > 0.0f)
                ImGui::GetForegroundDrawList()->AddRectFilled(
                    vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                    Theme::U32(Token::Void, fade));
            if (t >= kOpenSeconds)
                g_door.Done = true;
        }

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}
