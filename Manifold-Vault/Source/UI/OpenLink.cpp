#include "UI/OpenLink.h"

#include "Core/LinkSafety.h"

#include <sodium.h>

#include <string>

#include <windows.h>
#include <shellapi.h>

namespace Bunker::UI
{
    bool OpenWebLink(std::string_view value)
    {
        const std::string_view link = LinkToOpen(value);
        if (link.empty())
            return false;

        const int n = ::MultiByteToWideChar(CP_UTF8, 0, link.data(),
                                            static_cast<int>(link.size()), nullptr, 0);
        if (n <= 0)
            return false;
        std::wstring wide(static_cast<std::size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, link.data(), static_cast<int>(link.size()),
                              wide.data(), n);

        const HINSTANCE result = ::ShellExecuteW(nullptr, L"open", wide.c_str(),
                                                 nullptr, nullptr, SW_SHOWNORMAL);

        sodium_memzero(wide.data(), wide.size() * sizeof(wchar_t));

        return reinterpret_cast<INT_PTR>(result) > 32;
    }
}
