#pragma once

#include "Core/Crypto.h"
#include "Core/Result.h"
#include "Core/VaultFile.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace Bunker
{
    class VaultSession
    {
    public:
        enum class State
        {
            NoVaultYet,
            Locked,
            Working,
            Unlocked,
        };

        VaultSession();
        ~VaultSession();

        VaultSession(const VaultSession&) = delete;
        VaultSession& operator=(const VaultSession&) = delete;

        static std::string DefaultVaultPath();

        void SetPath(std::string path);
        const std::string& Path() const { return m_path; }

        State GetState() const { return m_state; }
        bool IsBusy() const { return m_state == State::Working; }

        void BeginUnlock(const char* password, std::size_t len);
        void BeginCreate(const char* password, std::size_t len,
                         const Crypto::KdfParams& params);

        void BeginChangePassword(const char* oldPassword, std::size_t oldLen,
                                 const char* newPassword, std::size_t newLen);

        bool IsChangingPassword() const { return m_changeBusy; }

        bool ChangeFinished() const { return m_changeReady; }
        Status TakeChangeResult();

        void Poll();

        void Lock();

        Vault::VaultFile& Vault() { return m_vault; }
        const Vault::VaultFile& Vault() const { return m_vault; }

        ErrorCode LastErrorCode() const { return m_lastError; }
        const char* LastMessage() const { return m_lastMessage; }

        int FailedAttempts() const { return m_failedAttempts; }

        double LockoutRemaining() const;
        bool IsLockedOut() const { return LockoutRemaining() > 0.0; }

        double LastDerivationSeconds() const { return m_lastDerivationSeconds; }

    private:
        enum class Job { None, Unlock, Create };

        void JoinChangeWorker();

        void StartWorker(Job job, const char* password, std::size_t len,
                         const Crypto::KdfParams& params);
        void JoinWorker();

        std::string m_path;
        Vault::VaultFile m_vault;
        State m_state = State::NoVaultYet;

        std::thread m_worker;
        std::atomic<bool> m_workerDone{ false };
        Job m_job = Job::None;

        Status m_workerResult;
        Vault::VaultFile m_workerVault;
        double m_workerSeconds = 0.0;

        std::thread m_changeWorker;
        std::atomic<bool> m_changeDone{ false };
        bool m_changeBusy = false;
        bool m_changeReady = false;
        Status m_changeResult;
        Status m_changeDerive;
        SecureBuffer m_changeKekOld;
        SecureBuffer m_changeKekNew;
        unsigned char m_changeSalt[Crypto::kSaltBytes] = {};
        Crypto::KdfParams m_changeParams;

        ErrorCode m_lastError = ErrorCode::Ok;
        const char* m_lastMessage = "";
        int m_failedAttempts = 0;
        std::chrono::steady_clock::time_point m_lockoutUntil{};
        double m_lastDerivationSeconds = 0.0;
    };
}
