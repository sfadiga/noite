#pragma once

#include <QString>

#include <stdexcept>

namespace noite {

enum class ErrorCode {
    AlreadyRunning,
    NewerSchema,
    MigrationFailed,
    NotFound,
    InvalidTransition,
    InvalidTag,
    AttachmentTooLarge,
    InvalidAttachmentId,
    InvalidSetting,
    BackupDirMissing,
    BackupInvalid,
    Database,
    Io,
};

// Every failure in noite-core is thrown as an Error carrying a code the caller can act on.
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const QString& message);
    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

} // namespace noite
