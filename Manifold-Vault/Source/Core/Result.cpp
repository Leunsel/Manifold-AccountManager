#include "Core/Language.h"
#include "Core/Result.h"

namespace Bunker
{
    const char* ToString(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::Ok:                       return "Ok";
        case ErrorCode::OutOfMemory:              return "OutOfMemory";
        case ErrorCode::InvalidArgument:          return "InvalidArgument";
        case ErrorCode::NotInitialised:           return "NotInitialised";
        case ErrorCode::CryptoSelfTestFailed:     return "CryptoSelfTestFailed";
        case ErrorCode::KdfFailed:                return "KdfFailed";
        case ErrorCode::EncryptFailed:            return "EncryptFailed";
        case ErrorCode::DecryptFailed:            return "DecryptFailed";
        case ErrorCode::WrongPassword:            return "WrongPassword";
        case ErrorCode::FileTamperedOrCorrupt:    return "FileTamperedOrCorrupt";
        case ErrorCode::UnsupportedFormatVersion: return "UnsupportedFormatVersion";
        case ErrorCode::FileTooLarge:             return "FileTooLarge";
        case ErrorCode::FileWriteFailed:          return "FileWriteFailed";
        case ErrorCode::VaultChangedOnDisk:       return "VaultChangedOnDisk";
        case ErrorCode::OtpSecretInvalid:         return "OtpSecretInvalid";
        case ErrorCode::OtpUriUnsupported:        return "OtpUriUnsupported";
        }
        return "Unknown";
    }

    const char* ToUserMessage(ErrorCode code)
    {
        switch (code)
        {
        case ErrorCode::Ok:
            return T(Str::Error_Ok);

        case ErrorCode::WrongPassword:
            return T(Str::Error_WrongPassword);
        case ErrorCode::FileTamperedOrCorrupt:
            return T(Str::Error_FileCorrupt);
        case ErrorCode::UnsupportedFormatVersion:
            return T(Str::Error_NewerVersion);
        case ErrorCode::FileTooLarge:
            return T(Str::Error_FileTooLarge);
        case ErrorCode::FileWriteFailed:
            return T(Str::Error_WriteFailed);
        case ErrorCode::VaultChangedOnDisk:
            return T(Str::Error_ChangedOnDisk);

        case ErrorCode::KdfFailed:
            return T(Str::Error_KdfFailed);

        case ErrorCode::OtpSecretInvalid:
            return T(Str::Error_OtpSecretInvalid);

        case ErrorCode::OtpUriUnsupported:
            return T(Str::Error_OtpUriUnsupported);
        case ErrorCode::OutOfMemory:
            return T(Str::Error_OutOfMemory);

        case ErrorCode::CryptoSelfTestFailed:
            return T(Str::Error_SelfTestFailed);

        case ErrorCode::InvalidArgument:
        case ErrorCode::NotInitialised:
        case ErrorCode::EncryptFailed:
        case ErrorCode::DecryptFailed:
            return T(Str::Error_Internal);
        }
        return T(Str::Error_Unknown);
    }
}
