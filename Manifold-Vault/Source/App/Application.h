#pragma once

#include "App/Clipboard.h"
#include "App/VaultSession.h"
#include "Core/LockPolicy.h"
#include "Core/BackupRun.h"
#include "Core/SaveGuard.h"

#include <string>
#include <string_view>

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace Bunker
{
    class Application
    {
    public:
        Application() = default;
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        bool Initialise(HINSTANCE instance, int showCommand);

        void SetScaleOverride(float scale) { m_scaleOverride = scale; }

        int Run();

        const wchar_t* LastError() const { return m_error; }

        void RequestAnimationFrame() { m_animationFrames = 3; }

        VaultSession& Session() { return m_session; }

        HWND Window() const { return m_hwnd; }

        SaveGuard& Saves() { return m_saveGuard; }

        const Vault::BackupReport& LastBackup() const { return m_lastBackup; }
        void SetLastBackup(Vault::BackupReport report) { m_lastBackup = std::move(report); }

        void ApplyVaultSettings();

        void ApplyTheme(std::string_view paletteId);
        void ApplyTextSize(std::uint32_t percent);
        SecretClipboard& Clipboard() { return m_clipboard; }
        LockPolicy& Lock() { return m_lockPolicy; }

        LockReason LastLockReason() const { return m_lastLockReason; }
        void ClearLastLockReason() { m_lastLockReason = LockReason::None; }

        const std::string& MachineName() const { return m_machineName; }

        void LockVault(LockReason reason);

        bool TakeCloseRequest()
        {
            const bool requested = m_closeRequested;
            m_closeRequested = false;
            return requested;
        }
        void Quit() { m_running = false; }

        static double Now();

        bool CaptureBlocked() const { return m_captureBlocked; }

        void SetAllowScreenshots(bool allow) { m_allowScreenshots = allow; }

    private:
        bool CreateAppWindow(HINSTANCE instance, int showCommand);
        bool CreateDeviceAndSwapChain();
        void DestroyDeviceAndSwapChain();
        bool CreateRenderTarget();
        void DestroyRenderTarget();

        void ApplyWindowChrome();
        void ApplyPendingTheme();
        void ApplyPendingTextSize();
        void RescaleForDpi(UINT dpi);
        float WindowScale() const;
        SIZE MinimumWindowSize() const;
        void GrowToMinimumSize();
        void RenderFrame();
        void ApplyPendingResize();
        void FitInitialWindow();
        void UpdateLockState();
        void ApplyCaptureProtection(bool block);
        void RebindSwapChain();

        static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
        LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

        VaultSession m_session;
        SecretClipboard m_clipboard;
        LockPolicy m_lockPolicy;
        SaveGuard m_saveGuard;
        Vault::BackupReport m_lastBackup;
        bool m_policyFromVault = false;
        LockReason m_lastLockReason = LockReason::None;
        bool m_sessionNotificationsRegistered = false;
        bool m_captureBlocked = false;
        bool m_captureRequested = false;
        bool m_allowScreenshots = false;

        HWND m_hwnd = nullptr;
        HINSTANCE m_instance = nullptr;

        ID3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        IDXGISwapChain1* m_swapChain = nullptr;
        ID3D11RenderTargetView* m_renderTarget = nullptr;

        UINT m_pendingWidth = 0;
        UINT m_pendingHeight = 0;
        bool m_occluded = false;
        bool m_inSizeMove = false;
        bool m_inFrame = false;
        bool m_running = false;
        bool m_closeRequested = false;
        bool m_mainOnScreen = false;
        double m_mainSince = -1.0;
        double m_gateSince = -1.0;
        int m_animationFrames = 0;
        std::string m_machineName;
        UINT m_dpi = 96;
        float m_scaleOverride = 0.0f;

        void* m_baseStyle = nullptr;

        std::string m_pendingTheme;
        bool m_themePending = false;

        float m_textScale = 1.0f;
        float m_pendingTextScale = 1.0f;
        bool m_textScalePending = false;

        wchar_t m_error[256] = L"";
    };
}
