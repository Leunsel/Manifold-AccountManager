#include "App/Application.h"
#include "App/ProcessHardening.h"
#include "Core/Language.h"
#include "App/VaultSession.h"
#include "Core/Crypto.h"
#include "Core/Result.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <wchar.h>

int APIENTRY wWinMain(_In_ HINSTANCE instance,
                      _In_opt_ HINSTANCE,
                      _In_ LPWSTR commandLine,
                      _In_ int showCommand)
{
    ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 |
                               LOAD_LIBRARY_SEARCH_USER_DIRS |
                               LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

    if (const Bunker::Status st = Bunker::Crypto::Initialise(); !st.IsOk())
    {
        wchar_t german[256] = L"";
        wchar_t detail[256] = L"";
        ::MultiByteToWideChar(CP_UTF8, 0, Bunker::ToUserMessage(st.Code()), -1,
                              german, ARRAYSIZE(german));
        ::MultiByteToWideChar(CP_UTF8, 0,
                              st.GetError().Detail ? st.GetError().Detail : "-", -1,
                              detail, ARRAYSIZE(detail));

        wchar_t message[512];
        ::swprintf_s(message,
                     L"%ls\n\nDetail: %ls\n\nThe program will now exit.",
                     german, detail);
        ::MessageBoxW(nullptr, message, L"Crypto self-test failed",
                      MB_ICONSTOP | MB_OK);
        return 2;
    }

    Bunker::ApplyProcessHardening();

    {
        char wanted[16] = {};
        if (const wchar_t* arg = commandLine ? ::wcsstr(commandLine, L"--lang=") : nullptr)
        {
            arg += 7;
            std::size_t at = 0;
            while (at + 1 < sizeof(wanted) && *arg != L'\0' && *arg != L' ' &&
                   *arg < 128)
            {
                wanted[at++] = static_cast<char>(*arg++);
            }
            wanted[at] = '\0';
        }
        Bunker::SetLanguage(wanted[0] != '\0' ? wanted : "en");
    }

    Bunker::Application app;
    app.SetAllowScreenshots(commandLine && ::wcsstr(commandLine, L"--allow-screenshots") != nullptr);

    if (const wchar_t* arg = commandLine ? ::wcsstr(commandLine, L"--scale=") : nullptr)
    {
        wchar_t number[16] = {};
        for (std::size_t i = 0; i + 1 < 16 && arg[8 + i] != L'\0' &&
                                arg[8 + i] != L' '; ++i)
        {
            number[i] = (arg[8 + i] == L',') ? L'.' : arg[8 + i];
        }
        const double value = ::_wtof(number);
        if (value >= 0.5 && value <= 4.0)
            app.SetScaleOverride(static_cast<float>(value));
    }

    if (commandLine)
    {
        if (const wchar_t* arg = ::wcsstr(commandLine, L"--vault="))
        {
            const wchar_t* value = arg + 8;
            std::wstring path;
            if (*value == L'"')
            {
                ++value;
                while (*value && *value != L'"')
                    path.push_back(*value++);
            }
            else
            {
                while (*value && *value != L' ')
                    path.push_back(*value++);
            }
            if (!path.empty())
            {
                const int need = ::WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1,
                                                       nullptr, 0, nullptr, nullptr);
                if (need > 0)
                {
                    std::string utf8(static_cast<std::size_t>(need - 1), 0);
                    ::WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(),
                                          need, nullptr, nullptr);
                    app.Session().SetPath(std::move(utf8));
                }
            }
        }
    }
    if (!app.Initialise(instance, showCommand))
    {
        ::MessageBoxW(nullptr, app.LastError(),
                      L"Manifold Vault could not be started",
                      MB_ICONERROR | MB_OK);
        return 1;
    }
    return app.Run();
}
