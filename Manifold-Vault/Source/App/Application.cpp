#include "App/Application.h"

#include "Core/FileSystem.h"
#include "Core/Language.h"
#include "Core/Palette.h"
#include "Core/SettingsPolicy.h"

#include "App/BunkerTheme.h"
#include "App/ProcessHardening.h"
#include "UI/BunkerFonts.h"
#include "UI/ImGuiSecure.h"
#include "UI/EntryEditor.h"
#include "UI/Motion.h"
#include "UI/MotionCurve.h"
#include "UI/ScreenSettings.h"
#include "UI/ScreenMain.h"
#include "UI/ScreenUnlock.h"
#include "Resource.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <algorithm>
#include <chrono>

#include <dxgi1_3.h>
#include <dwmapi.h>
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct GateHints
    {
        std::string_view Theme;
        std::uint32_t TextSize = Bunker::Vault::kDefaultTextSize;
    };

    GateHints GateHintsOf(const std::string& path)
    {
        using Bunker::Vault::VaultFile;
        VaultFile::HeaderInfo info;
        GateHints hints;
        if (!VaultFile::PeekHeader(Bunker::RealFileSystem(), path, info).IsOk())
            return hints;
        hints.Theme = Bunker::PaletteIdForHeaderCode(info.GateTheme);
        if (info.GateTextSize != 0)
            hints.TextSize = Bunker::Vault::NearestTextSize(info.GateTextSize);
        return hints;
    }

    constexpr double kVeilSeconds = 0.20;

    void DrawGround(float alpha)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::GetForegroundDrawList()->AddRectFilled(
            vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
            Bunker::Theme::U32(Bunker::Theme::Token::Void, alpha));
    }

    void DrawVeil(double since)
    {
        if (since < 0.0 || !Bunker::UI::Motion::Enabled())
            return;
        const double shown = ImGui::GetTime() - since;
        if (shown < 0.0 || shown >= kVeilSeconds)
            return;
        DrawGround(1.0f - Bunker::UI::Curve::EaseOut(static_cast<float>(shown / kVeilSeconds)));
    }
}

namespace Bunker
{
    namespace
    {
        constexpr wchar_t kWindowClass[] = L"ManifoldVaultWindow";
        constexpr wchar_t kWindowTitle[] = L"Manifold Vault";

        constexpr int kWindowWidth = 1180;
        constexpr int kWindowHeight = 760;

        constexpr DWORD kUseImmersiveDarkMode = 20;
        constexpr DWORD kBorderColor = 34;
        constexpr DWORD kCaptionColor = 35;
        constexpr DWORD kTextColor = 36;

        COLORREF ToColorRef(ImVec4 c)
        {
            const auto b = static_cast<DWORD>(c.z * 255.0f + 0.5f);
            const auto g = static_cast<DWORD>(c.y * 255.0f + 0.5f);
            const auto r = static_cast<DWORD>(c.x * 255.0f + 0.5f);
            return RGB(r, g, b);
        }

        void FollowSystemAnimations()
        {
            BOOL on = TRUE;
            if (!::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0))
                on = TRUE;
            UI::Motion::SetEnabled(on != FALSE);
        }
    }

    double Application::Now()
    {
        using namespace std::chrono;
        static const steady_clock::time_point origin = steady_clock::now();
        return duration<double>(steady_clock::now() - origin).count();
    }

    Application::~Application()
    {
        if (ImGui::GetCurrentContext())
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        DestroyDeviceAndSwapChain();
        if (m_hwnd)
        {
            if (m_sessionNotificationsRegistered)
                ::WTSUnRegisterSessionNotification(m_hwnd);
            ::DestroyWindow(m_hwnd);
        }
        ::UnregisterClassW(kWindowClass, m_instance);
        delete static_cast<ImGuiStyle*>(m_baseStyle);
    }

    bool Application::CreateAppWindow(HINSTANCE instance, int showCommand)
    {
        m_instance = instance;

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &Application::WndProcThunk;
        wc.hInstance = instance;
        wc.hIcon = static_cast<HICON>(
            ::LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                         ::GetSystemMetrics(SM_CXICON),
                         ::GetSystemMetrics(SM_CYICON), 0));
        wc.hIconSm = static_cast<HICON>(
            ::LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                         ::GetSystemMetrics(SM_CXSMICON),
                         ::GetSystemMetrics(SM_CYSMICON), 0));
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kWindowClass;
        if (!::RegisterClassExW(&wc))
        {
            ::wcscpy_s(m_error, L"Could not register the window class.");
            return false;
        }

        m_hwnd = ::CreateWindowExW(0, kWindowClass, kWindowTitle,
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT,
                                   kWindowWidth, kWindowHeight,
                                   nullptr, nullptr, instance, this);
        if (!m_hwnd)
        {
            ::wcscpy_s(m_error, L"Could not create the window.");
            return false;
        }

        m_dpi = ::GetDpiForWindow(m_hwnd);
        FitInitialWindow();
        ApplyWindowChrome();

        m_sessionNotificationsRegistered =
            ::WTSRegisterSessionNotification(m_hwnd, NOTIFY_FOR_THIS_SESSION) != FALSE;
        ::ShowWindow(m_hwnd, showCommand);
        ::UpdateWindow(m_hwnd);
        return true;
    }

    void Application::FitInitialWindow()
    {
        const HMONITOR monitor = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = { sizeof(info), {}, {}, 0 };
        if (!monitor || !::GetMonitorInfoW(monitor, &info))
            return;
        const float scale = WindowScale();
        const LONG workWidth = info.rcWork.right - info.rcWork.left;
        const LONG workHeight = info.rcWork.bottom - info.rcWork.top;
        const LONG width = (std::min)(static_cast<LONG>(kWindowWidth * scale), workWidth);
        const LONG height = (std::min)(static_cast<LONG>(kWindowHeight * scale), workHeight);
        ::SetWindowPos(m_hwnd, nullptr,
                       info.rcWork.left + (workWidth - width) / 2,
                       info.rcWork.top + (workHeight - height) / 2,
                       width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Application::ApplyWindowChrome()
    {
        const BOOL dark = Theme::IsLight() ? FALSE : TRUE;
        ::DwmSetWindowAttribute(m_hwnd, kUseImmersiveDarkMode, &dark, sizeof(dark));

        const COLORREF caption = ToColorRef(Theme::Color(Theme::Token::Panel));
        const COLORREF border = ToColorRef(Theme::Color(Theme::Token::Line));
        const COLORREF text = ToColorRef(Theme::Color(Theme::Token::TextSecondary));
        ::DwmSetWindowAttribute(m_hwnd, kCaptionColor, &caption, sizeof(caption));
        ::DwmSetWindowAttribute(m_hwnd, kBorderColor, &border, sizeof(border));
        ::DwmSetWindowAttribute(m_hwnd, kTextColor, &text, sizeof(text));
    }

    bool Application::CreateDeviceAndSwapChain()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained = {};

        HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                         levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                         &m_device, &obtained, &m_context);

        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (flags & D3D11_CREATE_DEVICE_DEBUG))
        {
            flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                     &m_device, &obtained, &m_context);
        }
        if (hr == DXGI_ERROR_UNSUPPORTED)
        {
            hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                     levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                     &m_device, &obtained, &m_context);
        }
        if (FAILED(hr))
        {
            ::swprintf_s(m_error, L"Could not create the Direct3D 11 device (0x%08X).",
                         static_cast<unsigned>(hr));
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        auto release = [&] {
            if (factory) factory->Release();
            if (adapter) adapter->Release();
            if (dxgiDevice) dxgiDevice->Release();
        };

        hr = m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr))
        {
            release();
            ::swprintf_s(m_error, L"DXGI factory not available (0x%08X).",
                         static_cast<unsigned>(hr));
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = 0;
        desc.Height = 0;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_NONE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        hr = factory->CreateSwapChainForHwnd(m_device, m_hwnd, &desc, nullptr, nullptr, &m_swapChain);
        if (FAILED(hr))
        {
            release();
            ::swprintf_s(m_error, L"Could not create the swap chain (0x%08X).",
                         static_cast<unsigned>(hr));
            return false;
        }

        factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
        release();

        return CreateRenderTarget();
    }

    bool Application::CreateRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr) || !backBuffer)
            return false;
        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTarget);
        backBuffer->Release();
        return SUCCEEDED(hr);
    }

    void Application::DestroyRenderTarget()
    {
        if (m_renderTarget)
        {
            m_renderTarget->Release();
            m_renderTarget = nullptr;
        }
    }

    void Application::DestroyDeviceAndSwapChain()
    {
        DestroyRenderTarget();
        if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device) { m_device->Release(); m_device = nullptr; }
    }

    float Application::WindowScale() const
    {
        return static_cast<float>(m_dpi) / 96.0f * m_textScale;
    }

    SIZE Application::MinimumWindowSize() const
    {
        const float scale = WindowScale();
        SIZE size = { static_cast<LONG>(kWindowWidth * scale),
                      static_cast<LONG>(kWindowHeight * scale) };
        if (const HMONITOR monitor = ::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST))
        {
            MONITORINFO info = { sizeof(info), {}, {}, 0 };
            if (::GetMonitorInfoW(monitor, &info))
            {
                size.cx = (std::min)(size.cx, info.rcWork.right - info.rcWork.left);
                size.cy = (std::min)(size.cy, info.rcWork.bottom - info.rcWork.top);
            }
        }
        return size;
    }

    void Application::GrowToMinimumSize()
    {
        RECT r;
        if (!m_hwnd || ::IsZoomed(m_hwnd) || ::IsIconic(m_hwnd) || !::GetWindowRect(m_hwnd, &r))
            return;
        const SIZE least = MinimumWindowSize();
        const LONG width = (std::max)(r.right - r.left, least.cx);
        const LONG height = (std::max)(r.bottom - r.top, least.cy);
        if (width == r.right - r.left && height == r.bottom - r.top)
            return;

        LONG x = r.left;
        LONG y = r.top;
        MONITORINFO info = { sizeof(info), {}, {}, 0 };
        if (::GetMonitorInfoW(::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &info))
        {
            x = (std::max)(info.rcWork.left, (std::min)(x, info.rcWork.right - width));
            y = (std::max)(info.rcWork.top, (std::min)(y, info.rcWork.bottom - height));
        }
        ::SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Application::ApplyTextSize(std::uint32_t percent)
    {
        m_pendingTextScale = static_cast<float>(Vault::NearestTextSize(percent)) / 100.0f;
        m_textScalePending = true;
    }

    void Application::ApplyPendingTextSize()
    {
        if (!m_textScalePending)
            return;
        m_textScalePending = false;
        if (m_pendingTextScale == m_textScale)
            return;
        m_textScale = m_pendingTextScale;
        RescaleForDpi(m_dpi);
        GrowToMinimumSize();
    }

    void Application::RescaleForDpi(UINT dpi)
    {
        m_dpi = dpi;
        const float scale = (m_scaleOverride > 0.0f
                                 ? m_scaleOverride
                                 : static_cast<float>(dpi) / 96.0f) *
                            m_textScale;

        ImGuiStyle& style = ImGui::GetStyle();
        const auto* base = static_cast<const ImGuiStyle*>(m_baseStyle);
        style = *base;
        style.ScaleAllSizes(scale);

        if (scale >= 1.75f)
        {
            style.FrameBorderSize = 2.0f;
            style.ChildBorderSize = 2.0f;
            style.PopupBorderSize = 2.0f;
        }
        else if (scale < 1.0f)
        {
            // ScaleAllSizes truncates, so below 100 % every one-pixel line would vanish.
            style.WindowBorderSize = base->WindowBorderSize;
            style.ChildBorderSize = base->ChildBorderSize;
            style.PopupBorderSize = base->PopupBorderSize;
            style.FrameBorderSize = base->FrameBorderSize;
            style.ImageBorderSize = base->ImageBorderSize;
            style.TabBorderSize = base->TabBorderSize;
            style.TabBarBorderSize = base->TabBarBorderSize;
            style.TabBarOverlineSize = base->TabBarOverlineSize;
            style.TreeLinesSize = base->TreeLinesSize;
            style.DragDropTargetBorderSize = base->DragDropTargetBorderSize;
            style.InputTextCursorSize = base->InputTextCursorSize;
            style.SeparatorSize = base->SeparatorSize;
            style.SeparatorTextBorderSize = base->SeparatorTextBorderSize;
        }

        style.FontScaleDpi = scale;
    }

    void Application::ApplyPendingResize()
    {
        if (m_pendingWidth == 0 || m_pendingHeight == 0 || !m_swapChain)
            return;
        DestroyRenderTarget();
        m_swapChain->ResizeBuffers(0, m_pendingWidth, m_pendingHeight, DXGI_FORMAT_UNKNOWN, 0);
        m_pendingWidth = m_pendingHeight = 0;
        CreateRenderTarget();
    }

    void Application::RenderFrame()
    {
        if (m_inFrame)
            return;
        m_inFrame = true;
        struct FrameDone
        {
            bool& Flag;
            ~FrameDone() { Flag = false; }
        } frameDone{ m_inFrame };

        if (!UI::GateHoldsTheme())
        {
            ApplyPendingTheme();
            ApplyPendingTextSize();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        m_session.Poll();
        UpdateLockState();

        const bool gateOpening = UI::GateOpening(*this, m_session);
        if (m_session.GetState() == VaultSession::State::Unlocked && !gateOpening)
        {
            if (!m_mainOnScreen)
                m_mainSince = ImGui::GetTime();
            UI::DrawMain(*this, m_session);
            m_mainOnScreen = true;

            if (m_session.GetState() != VaultSession::State::Unlocked)
            {
                // Locked mid-frame: cover what was drawn from the wiped document.
                DrawGround(1.0f);
            }
            else
            {
                DrawVeil(m_mainSince);
            }
        }
        else
        {
            if (m_mainOnScreen)
            {
                ImGui::SetWindowFocus(nullptr);
                m_mainOnScreen = false;
                // No fade-out on lock: secrets must not stay visible, and capture
                // protection is already off. The unlock screen fades in instead.
                m_gateSince = ImGui::GetTime();
            }
            UI::DrawGate(*this, m_session);
            DrawVeil(m_gateSince);
        }

        ImGui::Render();

        const ImVec4 clear = Theme::Color(Theme::Token::Void);
        const float rgba[4] = { clear.x, clear.y, clear.z, 1.0f };
        m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
        m_context->ClearRenderTargetView(m_renderTarget, rgba);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const HRESULT hr = m_swapChain->Present(1, 0);
        m_occluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    bool Application::Initialise(HINSTANCE instance, int showCommand)
    {
        const GateHints gate = GateHintsOf(m_session.Path());
        m_pendingTheme.assign(gate.Theme.data(), gate.Theme.size());
        Theme::SetActive(m_pendingTheme);
        m_textScale = static_cast<float>(gate.TextSize) / 100.0f;
        m_pendingTextScale = m_textScale;

        if (!CreateAppWindow(instance, showCommand))
            return false;
        if (!CreateDeviceAndSwapChain())
            return false;

        IMGUI_CHECKVERSION();
        UI::InstallWipeOnFreeAllocator();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigDpiScaleFonts = false;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        Theme::Apply(ImGui::GetStyle());
        m_baseStyle = new ImGuiStyle(ImGui::GetStyle());
        RescaleForDpi(m_dpi);

        wchar_t marks[160] = L"";
        if (!kHardenedBuild)
            ::wcscat_s(marks, L"  [Debug]");
        else if (m_allowScreenshots)
            ::wcscat_s(marks, L"  [--allow-screenshots]");
        if (m_scaleOverride > 0.0f)
        {
            wchar_t scale[80];
            ::swprintf_s(scale, L"  [--scale=%.2f, actual display %u dpi]",
                         static_cast<double>(m_scaleOverride), m_dpi);
            ::wcscat_s(marks, scale);
        }
        if (marks[0] != L'\0')
        {
            wchar_t title[200];
            ::swprintf_s(title, L"%s%s", kWindowTitle, marks);
            ::SetWindowTextW(m_hwnd, title);
        }

        const char* fontError = nullptr;
        Fonts::Set fonts;
        if (!Fonts::Build(fonts, &fontError))
        {
            ::MultiByteToWideChar(CP_UTF8, 0, fontError ? fontError : "font error",
                                  -1, m_error, ARRAYSIZE(m_error));
            return false;
        }
        Fonts::UseMonospace(Theme::IsMonospace());

        if (!ImGui_ImplWin32_Init(m_hwnd) || !ImGui_ImplDX11_Init(m_device, m_context))
        {
            ::wcscpy_s(m_error, L"Could not initialise Dear ImGui.");
            return false;
        }

        FollowSystemAnimations();

        {
            wchar_t name[MAX_COMPUTERNAME_LENGTH + 1] = {};
            DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
            if (::GetComputerNameW(name, &length))
            {
                char narrow[4 * (MAX_COMPUTERNAME_LENGTH + 1)] = {};
                if (::WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow),
                                          nullptr, nullptr) > 0)
                    m_machineName = narrow;
            }
        }
        m_running = true;
        return true;
    }

    int Application::Run()
    {
        MSG msg = {};
        while (m_running)
        {
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
                if (msg.message == WM_QUIT)
                    m_running = false;
            }
            if (!m_running)
                break;

            if (m_occluded && m_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
            {
                ::Sleep(16);
                continue;
            }
            m_occluded = false;

            ApplyPendingResize();
            RenderFrame();
            if (m_animationFrames > 0)
                --m_animationFrames;
        }
        return 0;
    }

    void Application::LockVault(LockReason reason)
    {
        if (m_session.GetState() != VaultSession::State::Unlocked)
            return;

        // Clipboard first: it is the only copy of a secret outside this process.
        m_clipboard.ClearIfOurs();
        UI::Editor::AbandonForLock();
        UI::Settings::Close();
        UI::ResetMainScreenState();
        m_closeRequested = false;
        m_session.Lock();
        m_saveGuard.Reset();
        m_lastLockReason = reason;
        m_lockPolicy.Reset(Now());
        ApplyCaptureProtection(false);
    }

    void Application::ApplyVaultSettings()
    {
        if (m_session.GetState() != VaultSession::State::Unlocked)
            return;
        const Vault::Settings& settings = m_session.Vault().Doc().VaultSettings;
        m_lockPolicy.SetSettings(Vault::ToLockSettings(settings));
        SetLanguage(Vault::LanguageCode(settings));
        ApplyTheme(settings.Theme);
        ApplyTextSize(Vault::TextSizePercent(settings));
        Theme::SetColouredIcons(!settings.MonochromeIcons);
    }

    void Application::ApplyTheme(std::string_view paletteId)
    {
        m_pendingTheme.assign(paletteId.data(), paletteId.size());
        m_themePending = true;
    }

    void Application::ApplyPendingTheme()
    {
        if (!m_themePending)
            return;
        m_themePending = false;

        const std::size_t before = Theme::ActiveIndex();
        Theme::SetActive(m_pendingTheme);
        if (Theme::ActiveIndex() == before)
            return;
        if (m_baseStyle)
        {
            ImGuiStyle& base = *static_cast<ImGuiStyle*>(m_baseStyle);
            Theme::ApplyColours(base);
            Theme::ApplyShape(base);
            RescaleForDpi(m_dpi);
        }
        else
        {
            Theme::ApplyColours(ImGui::GetStyle());
        }

        Fonts::UseMonospace(Theme::IsMonospace());

        if (m_hwnd)
        {
            ApplyWindowChrome();
            ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED);
        }
    }

    void Application::UpdateLockState()
    {
        const double now = Now();
        const bool unlocked = m_session.GetState() == VaultSession::State::Unlocked;

        if (unlocked != m_policyFromVault)
        {
            if (unlocked)
            {
                ApplyVaultSettings();
                if (m_session.Vault().HeaderHintsStale())
                    (void)m_session.Vault().Save(RealFileSystem(), m_session.Path());
            }
            else
            {
                m_lockPolicy.SetSettings(LockSettings{});
                SetLanguage("en");
                const GateHints gate = GateHintsOf(m_session.Path());
                ApplyTheme(gate.Theme);
                ApplyTextSize(gate.TextSize);
                Theme::SetColouredIcons(true);
            }
            m_policyFromVault = unlocked;
        }

        if (unlocked)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
                ImGui::IsAnyMouseDown() || io.MouseWheel != 0.0f ||
                io.InputQueueCharacters.Size > 0 || ImGui::IsAnyItemActive())
            {
                m_lockPolicy.NotifyActivity(now);
            }
        }

        if (const LockReason reason = m_lockPolicy.Evaluate(now, unlocked);
            reason != LockReason::None)
        {
            const bool deferrable = reason == LockReason::Idle ||
                                    reason == LockReason::Minimised ||
                                    reason == LockReason::FocusLost;
            if (deferrable && m_saveGuard.HoldsAutoLock(now))
            {
                m_lockPolicy.NotifyActivity(now);
            }
            else
            {
                LockVault(reason);
            }
        }

        m_clipboard.Poll(now);
        ApplyCaptureProtection(m_session.GetState() == VaultSession::State::Unlocked);
    }

    void Application::ApplyCaptureProtection(bool unlocked)
    {
        const bool block = ShouldBlockCapture(unlocked, m_allowScreenshots,
                                              kHardenedBuild);
        if (block == m_captureRequested || !m_hwnd)
            return;
        m_captureRequested = block;

        BOOL ok = ::SetWindowDisplayAffinity(m_hwnd, block ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
        if (!ok && block)
            ok = ::SetWindowDisplayAffinity(m_hwnd, WDA_MONITOR);
        if (!ok)
            return;
        m_captureBlocked = block;

        RebindSwapChain();
    }

    void Application::RebindSwapChain()
    {
        if (!m_hwnd)
            return;
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                           SWP_FRAMECHANGED);
        RECT rc{};
        if (!::GetClientRect(m_hwnd, &rc))
            return;
        const UINT width = static_cast<UINT>(rc.right - rc.left);
        const UINT height = static_cast<UINT>(rc.bottom - rc.top);
        if (width == 0 || height == 0)
            return;
        m_pendingWidth = width;
        m_pendingHeight = height;
    }

    LRESULT CALLBACK Application::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        Application* self = nullptr;
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<Application*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self)
                self->m_hwnd = hwnd;
        }
        else
        {
            self = reinterpret_cast<Application*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self)
            return self->WndProc(hwnd, msg, wp, lp);
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT Application::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return 1;

        switch (msg)
        {
        case WM_SIZE:
            if (wp == SIZE_MINIMIZED)
            {
                if (m_lockPolicy.Settings().LockOnMinimise)
                    m_lockPolicy.NotifyEvent(LockReason::Minimised, Now());
            }
            else
            {
                m_pendingWidth = LOWORD(lp);
                m_pendingHeight = HIWORD(lp);
                if (m_inSizeMove && !m_inFrame && m_renderTarget &&
                    ImGui::GetCurrentContext())
                {
                    ApplyPendingResize();
                    RenderFrame();
                }
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            m_inSizeMove = true;
            return 0;

        case WM_EXITSIZEMOVE:
            m_inSizeMove = false;
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wp == WTS_SESSION_LOCK || wp == WTS_SESSION_LOGOFF ||
                wp == WTS_REMOTE_DISCONNECT)
            {
                m_lockPolicy.NotifyEvent(LockReason::SessionLocked, Now());
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wp == PBT_APMSUSPEND)
            {
                LockVault(LockReason::Suspend);
                if (m_renderTarget && m_context && m_swapChain)
                {
                    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    m_context->OMSetRenderTargets(1, &m_renderTarget, nullptr);
                    m_context->ClearRenderTargetView(m_renderTarget, black);
                    m_swapChain->Present(0, 0);
                }
            }
            return TRUE;

        case WM_ENDSESSION:
            if (wp)
            {
                LockVault(LockReason::Shutdown);
                EmergencyWipe();
            }
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE)
            {
                if (m_lockPolicy.Settings().LockOnFocusLost)
                    m_lockPolicy.NotifyEvent(LockReason::FocusLost, Now());
            }
            else
            {
                m_lockPolicy.NotifyActivity(Now());
            }
            return 0;

        case WM_DPICHANGED:
        {
            const auto* suggested = reinterpret_cast<const RECT*>(lp);
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left,
                           suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            RescaleForDpi(HIWORD(wp));
            return 0;
        }

        case WM_GETMINMAXINFO:
        {
            const SIZE least = MinimumWindowSize();
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = least.cx;
            mmi->ptMinTrackSize.y = least.cy;
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0)
                return 0;
            break;

        case WM_SETTINGCHANGE:
            FollowSystemAnimations();
            if (lp && ::lstrcmpiW(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0)
                m_themePending = true;
            break;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;

        case WM_CLOSE:
            if (m_session.GetState() == VaultSession::State::Unlocked &&
                UI::Editor::IsOpen())
            {
                m_closeRequested = true;
                if (::IsIconic(hwnd))
                    ::ShowWindow(hwnd, SW_RESTORE);
                m_occluded = false;
                return 0;
            }
            m_running = false;
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}
