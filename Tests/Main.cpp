#include "TestRunner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include <crtdbg.h>
#include <windows.h>

namespace
{
    void __cdecl ExitOnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*,
                                        unsigned int, uintptr_t)
    {
        std::fprintf(stderr, "\n[ ABORT ] invalid parameter passed to the CRT\n");
        std::fflush(stderr);
        std::_Exit(3);
    }

    void GoHeadless()
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler(ExitOnInvalidParameter);
#ifdef _DEBUG
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }
}

int main(int argc, char** argv)
{
    ::SetConsoleOutputCP(CP_UTF8);

    const char* filter = "";
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--headless") == 0)
            GoHeadless();
        else
            filter = argv[i];
    }

    std::printf("Manifold Vault - Kerntests\n");
    if (*filter)
        std::printf("Filter: '%s'\n", filter);
    std::printf("\n");
    return Test::RunAll(filter);
}
