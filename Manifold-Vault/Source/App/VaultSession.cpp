#include "App/VaultSession.h"

#include "Core/FileSystem.h"
#include "App/ProcessHardening.h"
#include "Core/Secret.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <sodium.h>

#include <windows.h>

namespace Bunker
{
    namespace
    {
        double LockoutSecondsFor(int failedAttempts)
        {
            if (failedAttempts <= 2)
                return 0.0;
            const int step = std::min(failedAttempts - 2, 6);
            return std::min(30.0, std::pow(2.0, static_cast<double>(step - 1)));
        }
    }

    VaultSession::VaultSession()
    {
        m_path = DefaultVaultPath();
        m_state = RealFileSystem().Exists(m_path) ? State::Locked : State::NoVaultYet;
    }

    VaultSession::~VaultSession()
    {
        JoinWorker();
        JoinChangeWorker();
    }

    void VaultSession::JoinChangeWorker()
    {
        if (m_changeWorker.joinable())
            m_changeWorker.join();
    }

    std::string VaultSession::DefaultVaultPath()
    {
        wchar_t exePath[MAX_PATH * 2] = {};
        const DWORD n = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
        if (n == 0 || n >= MAX_PATH * 2)
            return "Bunker.vault";

        for (DWORD i = n; i > 0; --i)
        {
            if (exePath[i - 1] == L'\\' || exePath[i - 1] == L'/')
            {
                exePath[i] = 0;
                break;
            }
        }

        char narrow[MAX_PATH * 4] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, exePath, -1, narrow, sizeof(narrow), nullptr, nullptr);
        return std::string(narrow) + "Bunker.vault";
    }

    void VaultSession::SetPath(std::string path)
    {
        if (m_state == State::Working)
            return;
        Lock();
        m_path = std::move(path);
        m_state = RealFileSystem().Exists(m_path) ? State::Locked : State::NoVaultYet;
        m_failedAttempts = 0;
        m_lockoutUntil = {};
        m_lastError = ErrorCode::Ok;
        m_lastMessage = "";
    }

    double VaultSession::LockoutRemaining() const
    {
        if (m_lockoutUntil.time_since_epoch().count() == 0)
            return 0.0;
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_lockoutUntil)
            return 0.0;
        return std::chrono::duration<double>(m_lockoutUntil - now).count();
    }

    void VaultSession::StartWorker(Job job, const char* password, std::size_t len,
                                   const Crypto::KdfParams& params)
    {
        JoinWorker();

        auto secret = std::make_shared<SecureBuffer>(len + 1);
        if (!secret->IsValid())
        {
            m_lastError = ErrorCode::OutOfMemory;
            m_lastMessage = "Not enough locked memory.";
            return;
        }
        std::memcpy(secret->Data(), password, len);
        secret->Data()[len] = 0;

        m_job = job;
        m_state = State::Working;
        m_workerDone.store(false, std::memory_order_release);
        m_workerResult = Ok();
        m_workerVault.Lock();

        const std::string path = m_path;
        m_worker = std::thread([this, job, secret, path, params, len]() {
            const auto started = std::chrono::steady_clock::now();
            Status st = Ok();
            if (job == Job::Unlock)
            {
                st = Vault::VaultFile::Unlock(RealFileSystem(), path,
                                       reinterpret_cast<const char*>(secret->Data()), len,
                                       m_workerVault);
            }
            else
            {
                st = Vault::VaultFile::Create(RealFileSystem(), path,
                                       reinterpret_cast<const char*>(secret->Data()), len,
                                       params, m_workerVault);
            }
            secret->Wipe();
            m_workerSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            m_workerResult = st;
            m_workerDone.store(true, std::memory_order_release);
        });
    }

    void VaultSession::JoinWorker()
    {
        if (m_worker.joinable())
            m_worker.join();
    }

    Status VaultSession::TakeChangeResult()
    {
        m_changeReady = false;
        const Status result = m_changeResult;
        m_changeResult = Ok();
        return result;
    }

    void VaultSession::BeginChangePassword(const char* oldPassword, std::size_t oldLen,
                                           const char* newPassword, std::size_t newLen)
    {
        if (m_state != State::Unlocked || m_changeBusy)
            return;
        if (!oldPassword || oldLen == 0 || !newPassword || newLen == 0)
        {
            m_changeResult = Error{ ErrorCode::InvalidArgument, "empty master password" };
            m_changeReady = true;
            return;
        }

        JoinChangeWorker();

        auto oldSecret = std::make_shared<SecureBuffer>(oldLen + 1);
        auto newSecret = std::make_shared<SecureBuffer>(newLen + 1);
        if (!oldSecret->IsValid() || !newSecret->IsValid())
        {
            m_changeResult = Error{ ErrorCode::OutOfMemory, "not enough locked memory" };
            m_changeReady = true;
            return;
        }
        std::memcpy(oldSecret->Data(), oldPassword, oldLen);
        oldSecret->Data()[oldLen] = 0;
        std::memcpy(newSecret->Data(), newPassword, newLen);
        newSecret->Data()[newLen] = 0;

        unsigned char salt[Crypto::kSaltBytes];
        std::memcpy(salt, m_vault.Salt(), sizeof(salt));
        m_changeParams = m_vault.Params();

        m_changeBusy = true;
        m_changeReady = false;
        m_changeDone.store(false, std::memory_order_release);
        m_changeDerive = Ok();
        m_changeKekOld.Reset();
        m_changeKekNew.Reset();

        const Crypto::KdfParams params = m_changeParams;
        std::array<unsigned char, Crypto::kSaltBytes> oldSalt{};
        std::memcpy(oldSalt.data(), salt, sizeof(salt));
        sodium_memzero(salt, sizeof(salt));

        m_changeWorker = std::thread([this, oldSecret, newSecret, oldSalt, params,
                                      oldLen, newLen]() {
            Status st = Crypto::DeriveKeyFromPassword(
                m_changeKekOld, reinterpret_cast<const char*>(oldSecret->Data()), oldLen,
                oldSalt.data(), params);

            if (st.IsOk())
            {
                Crypto::RandomBytes(m_changeSalt, sizeof(m_changeSalt));
                st = Crypto::DeriveKeyFromPassword(
                    m_changeKekNew, reinterpret_cast<const char*>(newSecret->Data()),
                    newLen, m_changeSalt, params);
            }

            m_changeDerive = st;
            m_changeDone.store(true, std::memory_order_release);
        });
    }

    void VaultSession::BeginUnlock(const char* password, std::size_t len)
    {
        if (m_state == State::Working || IsLockedOut() || !password || len == 0)
            return;
        StartWorker(Job::Unlock, password, len, Crypto::KdfParams{});
    }

    void VaultSession::BeginCreate(const char* password, std::size_t len,
                                   const Crypto::KdfParams& params)
    {
        if (m_state == State::Working || !password || len == 0)
            return;
        StartWorker(Job::Create, password, len, params);
    }

    void VaultSession::Poll()
    {
        if (m_changeBusy && m_changeDone.load(std::memory_order_acquire))
        {
            JoinChangeWorker();
            m_changeBusy = false;
            m_changeReady = true;

            if (!m_changeDerive.IsOk())
            {
                m_changeResult = m_changeDerive;
            }
            else if (m_state != State::Unlocked)
            {
                m_changeResult = Error{ ErrorCode::NotInitialised, "vault is locked" };
            }
            else if (!m_vault.KekMatches(m_changeKekOld))
            {
                m_changeResult = Error{ ErrorCode::WrongPassword,
                                        "current master password does not match" };
            }
            else
            {
                m_changeResult = m_vault.ChangePasswordWithKek(
                    RealFileSystem(), m_path, std::move(m_changeKekNew),
                    m_changeSalt, m_changeParams);
            }

            m_changeKekOld.Reset();
            m_changeKekNew.Reset();
            sodium_memzero(m_changeSalt, sizeof(m_changeSalt));
        }

        if (m_state != State::Working)
            return;
        if (!m_workerDone.load(std::memory_order_acquire))
            return;

        JoinWorker();
        m_lastDerivationSeconds = m_workerSeconds;

        if (m_workerResult.IsOk())
        {
            m_vault = std::move(m_workerVault);
            m_state = State::Unlocked;

            if (ShouldRestrictAccess(true, kHardenedBuild))
                (void)RestrictProcessAccess();
            m_failedAttempts = 0;
            m_lockoutUntil = {};
            m_lastError = ErrorCode::Ok;
            m_lastMessage = "";
        }
        else
        {
            m_workerVault.Lock();
            m_lastError = m_workerResult.Code();
            m_lastMessage = ToUserMessage(m_lastError);

            if (m_lastError == ErrorCode::WrongPassword)
            {
                ++m_failedAttempts;
                const double wait = LockoutSecondsFor(m_failedAttempts);
                if (wait > 0.0)
                    m_lockoutUntil = std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(static_cast<long long>(wait * 1000.0));
            }

            m_state = RealFileSystem().Exists(m_path) ? State::Locked : State::NoVaultYet;
        }
        m_job = Job::None;
    }

    void VaultSession::Lock()
    {
        if (m_state == State::Working)
            return;
        m_vault.Lock();

        RestoreProcessAccess();

        m_state = RealFileSystem().Exists(m_path) ? State::Locked : State::NoVaultYet;
    }
}
