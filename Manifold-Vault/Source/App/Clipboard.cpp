#include "App/Clipboard.h"

#include <sodium.h>

#include <vector>

#include <windows.h>

namespace Bunker
{
    namespace
    {
        UINT FormatExcludeFromMonitoring()
        {
            static const UINT id =
                ::RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
            return id;
        }
        UINT FormatCanIncludeInHistory()
        {
            static const UINT id = ::RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
            return id;
        }
        UINT FormatCanUploadToCloud()
        {
            static const UINT id = ::RegisterClipboardFormatW(L"CanUploadToCloudClipboard");
            return id;
        }

        HGLOBAL AllocDword(DWORD value)
        {
            HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!h)
                return nullptr;
            auto* p = static_cast<DWORD*>(::GlobalLock(h));
            if (!p)
            {
                ::GlobalFree(h);
                return nullptr;
            }
            *p = value;
            ::GlobalUnlock(h);
            return h;
        }

        HGLOBAL AllocUtf16(const char* text, std::size_t length, bool wipeSourceCopy)
        {
            const int wide = ::MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length),
                                                   nullptr, 0);
            if (wide < 0)
                return nullptr;

            const std::size_t bytes = (static_cast<std::size_t>(wide) + 1) * sizeof(wchar_t);
            HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!h)
                return nullptr;
            auto* p = static_cast<wchar_t*>(::GlobalLock(h));
            if (!p)
            {
                ::GlobalFree(h);
                return nullptr;
            }
            ::MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(length), p, wide);
            p[wide] = L'\0';
            ::GlobalUnlock(h);
            (void)wipeSourceCopy;
            return h;
        }

        bool OpenClipboardWithRetry(HWND owner)
        {
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                if (::OpenClipboard(owner))
                    return true;
                ::Sleep(30);
            }
            return false;
        }

        bool PutOnClipboard(HWND owner, const char* text, std::size_t length, bool secret)
        {
            if (!OpenClipboardWithRetry(owner))
                return false;
            if (!::EmptyClipboard())
            {
                ::CloseClipboard();
                return false;
            }

            HGLOBAL data = AllocUtf16(text, length, secret);
            if (!data)
            {
                ::CloseClipboard();
                return false;
            }

            if (!::SetClipboardData(CF_UNICODETEXT, data))
            {
                if (secret)
                {
                    if (void* p = ::GlobalLock(data))
                    {
                        sodium_memzero(p, ::GlobalSize(data));
                        ::GlobalUnlock(data);
                    }
                }
                ::GlobalFree(data);
                ::CloseClipboard();
                return false;
            }

            if (secret)
            {
                if (HGLOBAL h = AllocDword(0))
                    if (!::SetClipboardData(FormatExcludeFromMonitoring(), h))
                        ::GlobalFree(h);
                if (HGLOBAL h = AllocDword(0))
                    if (!::SetClipboardData(FormatCanIncludeInHistory(), h))
                        ::GlobalFree(h);
                if (HGLOBAL h = AllocDword(0))
                    if (!::SetClipboardData(FormatCanUploadToCloud(), h))
                        ::GlobalFree(h);
            }

            ::CloseClipboard();
            return true;
        }
    }

    bool SecretClipboard::CopySecret(const char* text, std::size_t length,
                                     double clearAfterSeconds, double now)
    {
        if (!text || length == 0)
            return false;
        if (!PutOnClipboard(nullptr, text, length, true))
            return false;

        m_sequenceAtCopy = ::GetClipboardSequenceNumber();
        m_lastCopyTime = now;
        if (clearAfterSeconds > 0.0)
        {
            m_armed = true;
            m_clearAt = now + clearAfterSeconds;
        }
        else
        {
            m_armed = false;
        }
        return true;
    }

    bool SecretClipboard::CopyPlain(const char* text, std::size_t length)
    {
        if (!text || length == 0)
            return false;
        if (!PutOnClipboard(nullptr, text, length, false))
            return false;
        m_armed = false;
        m_sequenceAtCopy = ::GetClipboardSequenceNumber();
        return true;
    }

    void SecretClipboard::Poll(double now)
    {
        if (!m_armed || now < m_clearAt)
            return;
        m_armed = false;
        ClearIfOurs();
    }

    void SecretClipboard::ClearIfOurs()
    {
        if (::GetClipboardSequenceNumber() != m_sequenceAtCopy)
        {
            m_armed = false;
            return;
        }
        if (OpenClipboardWithRetry(nullptr))
        {
            ::EmptyClipboard();
            ::CloseClipboard();
            m_sequenceAtCopy = ::GetClipboardSequenceNumber();
        }
        m_armed = false;
    }

    double SecretClipboard::SecondsUntilClear(double now) const
    {
        if (!m_armed)
            return -1.0;
        const double remaining = m_clearAt - now;
        return remaining > 0.0 ? remaining : 0.0;
    }
}
