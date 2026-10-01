#include "UI/SaveFileDialog.h"

#include "Core/GermanText.h"

#include <cstdlib>

#include <windows.h>
#include <commdlg.h>

namespace Bunker::UI
{
    namespace
    {
        std::wstring Widen(const std::string& s)
        {
            if (s.empty())
                return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                                static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                  w.data(), n);
            return w;
        }

        std::string Narrow(const wchar_t* w)
        {
            if (!w || !*w)
                return {};
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                                nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string s(static_cast<std::size_t>(n - 1), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        std::string Env(const char* name)
        {
            wchar_t buffer[MAX_PATH * 2] = {};
            const DWORD n = ::GetEnvironmentVariableW(Widen(name).c_str(), buffer,
                                                      MAX_PATH * 2);
            if (n == 0 || n >= MAX_PATH * 2)
                return {};
            return Narrow(buffer);
        }

        void AddIfPresent(std::vector<CloudRoot>& out, const char* name,
                          const std::string& path)
        {
            if (path.empty())
                return;
            const DWORD attributes = ::GetFileAttributesW(Widen(path).c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                return;
            }
            for (const CloudRoot& existing : out)
                if (existing.Path == path)
                    return;
            out.push_back(CloudRoot{ name, path });
        }
    }

    std::vector<CloudRoot> KnownCloudRoots()
    {
        std::vector<CloudRoot> roots;
        AddIfPresent(roots, "OneDrive", Env("OneDrive"));
        AddIfPresent(roots, "OneDrive", Env("OneDriveConsumer"));
        AddIfPresent(roots, "OneDrive", Env("OneDriveCommercial"));

        const std::string home = Env("USERPROFILE");
        if (!home.empty())
        {
            AddIfPresent(roots, "Dropbox", home + "\\Dropbox");
            AddIfPresent(roots, "Google Drive", home + "\\Google Drive");
            AddIfPresent(roots, "Google Drive", home + "\\My Drive");
            AddIfPresent(roots, "iCloud", home + "\\iCloudDrive");
            AddIfPresent(roots, "Nextcloud", home + "\\Nextcloud");
            AddIfPresent(roots, "Sync", home + "\\Sync");
        }
        return roots;
    }

    PathFacts FactsFor(const std::string& path)
    {
        PathFacts facts;
        if (path.size() < 3 || path[1] != ':')
            return facts;

        const std::wstring root = Widen(path.substr(0, 3));
        switch (::GetDriveTypeW(root.c_str()))
        {
        case DRIVE_REMOTE:
            facts.IsNetworkDrive = true;
            break;
        case DRIVE_REMOVABLE:
            facts.IsRemovableDrive = true;
            break;
        default:
            break;
        }
        return facts;
    }

    std::string SuggestedExportName(std::uint64_t utcSeconds)
    {
        std::string stamp = FormatLocalTimestamp(utcSeconds);
        if (stamp == "unknown")
            return "Bunker-Plaintext.export.csv";

        std::string cleaned;
        cleaned.reserve(stamp.size());
        for (const char c : stamp)
        {
            if (c == '.' || c == ':')
                cleaned.push_back('-');
            else if (c == ' ')
                cleaned.push_back('-');
            else
                cleaned.push_back(c);
        }
        return "Bunker-Plaintext-" + cleaned + ".export.csv";
    }

    bool AskForExportPath(void* parentWindow, const std::string& suggestedName,
                          std::string& chosenPath)
    {
        chosenPath.clear();

        wchar_t buffer[MAX_PATH * 2] = {};
        const std::wstring suggestion = Widen(suggestedName);
        ::wcsncpy_s(buffer, suggestion.c_str(), _TRUNCATE);

        const wchar_t filter[] = L"CSV file (*.csv)\0*.csv\0All files\0*.*\0\0";

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = static_cast<HWND>(parentWindow);
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH * 2;
        ofn.lpstrTitle = L"Export Vault as Plain Text";
        ofn.lpstrDefExt = L"csv";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                    OFN_DONTADDTORECENT | OFN_EXPLORER;

        if (!::GetSaveFileNameW(&ofn))
            return false;

        chosenPath = Narrow(buffer);
        return !chosenPath.empty();
    }

    bool AskForImportPath(void* parentWindow, std::string& chosenPath)
    {
        chosenPath.clear();

        wchar_t buffer[MAX_PATH * 2] = {};
        const wchar_t filter[] = L"CSV file (*.csv)\0*.csv\0All files\0*.*\0\0";

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = static_cast<HWND>(parentWindow);
        ofn.lpstrFilter = filter;
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH * 2;
        ofn.lpstrTitle = L"Import Accounts from CSV";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                    OFN_DONTADDTORECENT | OFN_EXPLORER;

        if (!::GetOpenFileNameW(&ofn))
            return false;

        chosenPath = Narrow(buffer);
        return !chosenPath.empty();
    }

    Status ReadIntoSecret(const std::string& path, SecureBuffer& out,
                          std::size_t& outLength)
    {
        out.Reset();
        outLength = 0;

        HANDLE h = ::CreateFileW(Widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return Error{ ErrorCode::InvalidArgument, "file not readable" };

        LARGE_INTEGER size = {};
        if (!::GetFileSizeEx(h, &size) || size.QuadPart < 0)
        {
            ::CloseHandle(h);
            return Error{ ErrorCode::InvalidArgument, "file size not readable" };
        }
        if (size.QuadPart == 0)
        {
            ::CloseHandle(h);
            return Error{ ErrorCode::InvalidArgument, "the file is empty" };
        }
        if (static_cast<unsigned long long>(size.QuadPart) > kMaxImportBytes)
        {
            ::CloseHandle(h);
            return Error{ ErrorCode::FileTooLarge, "file implausibly large" };
        }

        const auto total = static_cast<std::size_t>(size.QuadPart);
        SecureBuffer buffer(total);
        if (!buffer.IsValid())
        {
            ::CloseHandle(h);
            return Error{ ErrorCode::OutOfMemory, "not enough locked memory" };
        }

        std::size_t done = 0;
        while (done < total)
        {
            const DWORD chunk =
                static_cast<DWORD>((total - done) > (1u << 20) ? (1u << 20)
                                                               : (total - done));
            DWORD got = 0;
            if (!::ReadFile(h, buffer.Data() + done, chunk, &got, nullptr) || got == 0)
            {
                ::CloseHandle(h);
                return Error{ ErrorCode::InvalidArgument, "read failed" };
            }
            done += got;
        }
        ::CloseHandle(h);

        out = std::move(buffer);
        outLength = total;
        return Status{};
    }
}
