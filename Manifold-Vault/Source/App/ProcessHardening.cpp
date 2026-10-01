#include "App/ProcessHardening.h"

#include "Core/Secret.h"

#include <sodium.h>

#include <csignal>
#include <cstdlib>
#include <exception>

#include <atomic>
#include <vector>

#include <windows.h>

#define PSAPI_VERSION 2
#include <psapi.h>

#include <aclapi.h>

namespace Bunker
{
    namespace
    {
        HardeningReport g_report;

        enum ExitCode : UINT
        {
            ExitUnhandledException = 3,
            ExitTerminate = 4,
            ExitAbort = 5,
            ExitInvalidParameter = 6,
            ExitSodiumMisuse = 7,
        };

        [[noreturn]] void DieWiped(UINT code)
        {
            EmergencyWipe();
            ::TerminateProcess(::GetCurrentProcess(), code);
            for (;;) {}
        }

        bool IsFatal(DWORD code)
        {
            return (code & 0xC0000000u) == 0xC0000000u;
        }

        LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info)
        {
            const DWORD code = (info && info->ExceptionRecord)
                                   ? info->ExceptionRecord->ExceptionCode
                                   : 0u;
            if (!IsFatal(code))
                return EXCEPTION_CONTINUE_SEARCH;

            EmergencyWipe();
            ::TerminateProcess(::GetCurrentProcess(), ExitUnhandledException);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void OnTerminate() { DieWiped(ExitTerminate); }

        void OnSigAbort(int) { DieWiped(ExitAbort); }

        void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*,
                                unsigned int, uintptr_t)
        {
            DieWiped(ExitInvalidParameter);
        }

        void OnSodiumMisuse() { DieWiped(ExitSodiumMisuse); }

        void ApplyMitigationPolicies()
        {
            PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dyn = {};
            dyn.ProhibitDynamicCode = 1;
            g_report.DynamicCodeDisabled =
                ::SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &dyn, sizeof(dyn)) != FALSE;

            PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY ext = {};
            ext.DisableExtensionPoints = 1;
            g_report.ExtensionPointsDisabled =
                ::SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy,
                                             &ext, sizeof(ext)) != FALSE;

            PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY handles = {};
            handles.RaiseExceptionOnInvalidHandleReference = 1;
            handles.HandleExceptionsPermanentlyEnabled = 1;
            g_report.StrictHandleChecks =
                ::SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy,
                                             &handles, sizeof(handles)) != FALSE;
        }
    }

    namespace
    {
        using RegisterFn = HRESULT(WINAPI*)(const void*, DWORD);
        using UnregisterFn = HRESULT(WINAPI*)(const void*);

        struct WerExclusion
        {
            RegisterFn Register = nullptr;
            UnregisterFn Unregister = nullptr;
            bool Available() const { return Register && Unregister; }
        };

        const WerExclusion& Wer()
        {
            static const WerExclusion resolved = []() -> WerExclusion {
                WerExclusion w;
                HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
                if (!k32)
                    return w;
                w.Register = reinterpret_cast<RegisterFn>(reinterpret_cast<void*>(
                    ::GetProcAddress(k32, "WerRegisterExcludedMemoryBlock")));
                w.Unregister = reinterpret_cast<UnregisterFn>(reinterpret_cast<void*>(
                    ::GetProcAddress(k32, "WerUnregisterExcludedMemoryBlock")));
                if (!w.Available())
                    w = WerExclusion{};
                return w;
            }();
            return resolved;
        }

        std::atomic<std::size_t> g_excluded{ 0 };
        std::atomic<std::size_t> g_refused{ 0 };

        void OnSecretRegion(const void* address, std::size_t size, bool live) noexcept
        {
            if (live)
                ExcludeFromCrashDumps(address, size);
            else
                IncludeInCrashDumpsAgain(address);
        }
    }

    void ExcludeFromCrashDumps(const void* address, std::size_t size)
    {
        if (!address || size == 0 || !Wer().Available())
            return;
        if (size > 0xFFFFFFFFull)
        {
            g_refused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (SUCCEEDED(Wer().Register(address, static_cast<DWORD>(size))))
            g_excluded.fetch_add(1, std::memory_order_relaxed);
        else
            g_refused.fetch_add(1, std::memory_order_relaxed);
    }

    void IncludeInCrashDumpsAgain(const void* address)
    {
        if (!address || !Wer().Available())
            return;
        if (SUCCEEDED(Wer().Unregister(address)))
        {
            std::size_t now = g_excluded.load(std::memory_order_relaxed);
            while (now > 0 &&
                   !g_excluded.compare_exchange_weak(now, now - 1,
                                                     std::memory_order_relaxed))
            {
            }
        }
    }

    namespace
    {
        PSECURITY_DESCRIPTOR g_originalSd = nullptr;
        bool g_restricted = false;

        PSID CurrentUserSid(std::vector<unsigned char>& buf)
        {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
                return nullptr;
            DWORD len = 0;
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            if (len == 0)
            {
                ::CloseHandle(token);
                return nullptr;
            }
            buf.assign(len, 0);
            const BOOL ok = ::GetTokenInformation(token, TokenUser, buf.data(),
                                                  len, &len);
            ::CloseHandle(token);
            if (!ok)
                return nullptr;
            return reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;
        }
    }

    bool RestrictProcessAccess()
    {
        if (g_restricted)
            return true;

        PACL existing = nullptr;
        if (::GetSecurityInfo(::GetCurrentProcess(), SE_KERNEL_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &existing, nullptr, &g_originalSd) != ERROR_SUCCESS)
        {
            g_originalSd = nullptr;
            return false;
        }

        std::vector<unsigned char> sidBuf;
        PSID sid = CurrentUserSid(sidBuf);
        if (!sid)
        {
            ::LocalFree(g_originalSd);
            g_originalSd = nullptr;
            return false;
        }

        EXPLICIT_ACCESSW ea{};
        ea.grfAccessPermissions =
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE;
        ea.grfAccessMode = SET_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
        ea.Trustee.ptstrName = static_cast<LPWCH>(sid);

        PACL dacl = nullptr;
        if (::SetEntriesInAclW(1, &ea, nullptr, &dacl) != ERROR_SUCCESS)
        {
            ::LocalFree(g_originalSd);
            g_originalSd = nullptr;
            return false;
        }

        const DWORD rc = ::SetSecurityInfo(
            ::GetCurrentProcess(), SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, dacl, nullptr);
        ::LocalFree(dacl);

        if (rc != ERROR_SUCCESS)
        {
            ::LocalFree(g_originalSd);
            g_originalSd = nullptr;
            return false;
        }
        g_restricted = true;
        return true;
    }

    void RestoreProcessAccess()
    {
        if (!g_restricted)
            return;

        PACL dacl = nullptr;
        BOOL present = FALSE, defaulted = FALSE;
        if (g_originalSd &&
            ::GetSecurityDescriptorDacl(g_originalSd, &present, &dacl, &defaulted))
        {
            ::SetSecurityInfo(::GetCurrentProcess(), SE_KERNEL_OBJECT,
                              DACL_SECURITY_INFORMATION |
                              UNPROTECTED_DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, present ? dacl : nullptr, nullptr);
        }
        if (g_originalSd)
        {
            ::LocalFree(g_originalSd);
            g_originalSd = nullptr;
        }
        g_restricted = false;
    }

    bool ProcessAccessIsRestricted()
    {
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (::GetSecurityInfo(::GetCurrentProcess(), SE_KERNEL_OBJECT,
                              DACL_SECURITY_INFORMATION, nullptr, nullptr,
                              &dacl, nullptr, &sd) != ERROR_SUCCESS)
            return false;

        bool grantsRead = false;
        if (dacl)
        {
            for (DWORD i = 0; i < dacl->AceCount; ++i)
            {
                void* raw = nullptr;
                if (!::GetAce(dacl, i, &raw))
                    continue;
                auto* header = static_cast<ACE_HEADER*>(raw);
                if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
                    continue;
                const ACCESS_MASK mask =
                    static_cast<ACCESS_ALLOWED_ACE*>(raw)->Mask;
                if ((mask & PROCESS_VM_READ) != 0 ||
                    (mask & (GENERIC_ALL | GENERIC_READ)) != 0)
                {
                    grantsRead = true;
                    break;
                }
            }
        }
        else
        {
            grantsRead = true;
        }
        if (sd)
            ::LocalFree(sd);
        return !grantsRead;
    }

    bool RegionIsLocked(const void* address, std::size_t size)
    {
        if (!address || size == 0)
            return false;

        SYSTEM_INFO si{};
        ::GetSystemInfo(&si);
        const std::size_t page = si.dwPageSize ? si.dwPageSize : 4096;

        const std::uintptr_t begin =
            reinterpret_cast<std::uintptr_t>(address) & ~(std::uintptr_t{ page } - 1);
        const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(address) + size;
        const std::size_t pages =
            static_cast<std::size_t>((end - begin + page - 1) / page);

        constexpr std::size_t kChunk = 512;
        PSAPI_WORKING_SET_EX_INFORMATION info[kChunk]{};

        for (std::size_t done = 0; done < pages; done += kChunk)
        {
            const std::size_t n = (pages - done) < kChunk ? (pages - done) : kChunk;
            for (std::size_t i = 0; i < n; ++i)
                info[i].VirtualAddress =
                    reinterpret_cast<void*>(begin + (done + i) * page);

            if (!::QueryWorkingSetEx(::GetCurrentProcess(), info,
                                     static_cast<DWORD>(n * sizeof(info[0]))))
                return false;

            for (std::size_t i = 0; i < n; ++i)
                if (!info[i].VirtualAttributes.Valid ||
                    !info[i].VirtualAttributes.Locked)
                    return false;
        }
        return true;
    }

    namespace
    {
        void RaiseLockedMemoryQuota(HardeningReport& report)
        {
            report.LockedMemoryBudget = kLockedVaultBytes;

            SIZE_T currentMin = 0, currentMax = 0;
            if (!::GetProcessWorkingSetSize(::GetCurrentProcess(),
                                            &currentMin, &currentMax))
                return;

            constexpr std::size_t kHeadroom = 8u * 1024 * 1024;
            const std::size_t wanted = kLockedVaultBytes + kHeadroom;
            const std::size_t newMin =
                RaisedWorkingSetMinimum(static_cast<std::size_t>(currentMin), wanted);

            const SIZE_T newMax = currentMax > newMin ? currentMax
                                                      : static_cast<SIZE_T>(newMin * 4);
            report.LockedMemoryRaised =
                ::SetProcessWorkingSetSizeEx(::GetCurrentProcess(),
                                             static_cast<SIZE_T>(newMin), newMax,
                                             QUOTA_LIMITS_HARDWS_MIN_ENABLE |
                                             QUOTA_LIMITS_HARDWS_MAX_DISABLE) != 0;

            SIZE_T afterMin = 0, afterMax = 0;
            if (::GetProcessWorkingSetSize(::GetCurrentProcess(), &afterMin, &afterMax))
                report.WorkingSetMinBytes = static_cast<std::size_t>(afterMin);

            void* probe = ::VirtualAlloc(nullptr, kLockedVaultBytes,
                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!probe)
                return;
            if (::VirtualLock(probe, kLockedVaultBytes))
            {
                report.LockedMemoryVerified = RegionIsLocked(probe, kLockedVaultBytes);
                ::VirtualUnlock(probe, kLockedVaultBytes);
            }
            ::VirtualFree(probe, 0, MEM_RELEASE);
        }
    }

    HardeningReport ApplyProcessHardening()
    {
        g_report = HardeningReport{};

        RaiseLockedMemoryQuota(g_report);

        ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        ::SetUnhandledExceptionFilter(&UnhandledFilter);

        std::set_terminate(&OnTerminate);
        std::signal(SIGABRT, &OnSigAbort);
        _set_invalid_parameter_handler(&OnInvalidParameter);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

        g_report.SodiumMisuseHooked = (sodium_set_misuse_handler(&OnSodiumMisuse) == 0);

        g_report.HandlersInstalled = true;

        SetSecretRegionHook(Wer().Available() ? &OnSecretRegion : nullptr);
        g_report.DumpsExcluded = Wer().Available();

        ApplyMitigationPolicies();
        return g_report;
    }

    const HardeningReport& LastHardeningReport()
    {
        g_report.ExcludedRegions = g_excluded.load(std::memory_order_relaxed);
        g_report.ExclusionsRefused = g_refused.load(std::memory_order_relaxed);
        g_report.AccessRestricted = ProcessAccessIsRestricted();
        return g_report;
    }
}
