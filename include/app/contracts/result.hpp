#ifndef APP_CONTRACTS_RESULT_HPP
#define APP_CONTRACTS_RESULT_HPP

#include <string>
#include <system_error>
#include <optional>
#include <utility>

namespace app::contracts {

enum class ErrorCode {
    Success = 0,
    InvalidInput,
    Forbidden,
    NotFound,
    Conflict,
    InsufficientStock,
    StorageFailure,
    BackupFailure,
    ShiftClosed,
    InvalidState
};

inline const char* to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::InvalidInput: return "InvalidInput";
        case ErrorCode::Forbidden: return "Forbidden";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::Conflict: return "Conflict";
        case ErrorCode::InsufficientStock: return "InsufficientStock";
        case ErrorCode::StorageFailure: return "StorageFailure";
        case ErrorCode::BackupFailure: return "BackupFailure";
        case ErrorCode::ShiftClosed: return "ShiftClosed";
        case ErrorCode::InvalidState: return "InvalidState";
        default: return "Unknown";
    }
}

class OperationResult {
public:
    OperationResult() : code_(ErrorCode::Success), message_("") {}

    static OperationResult ok() {
        return OperationResult(ErrorCode::Success, "");
    }

    static OperationResult fail(ErrorCode code, std::string message) {
        return OperationResult(code, std::move(message));
    }

    bool is_success() const { return code_ == ErrorCode::Success; }
    ErrorCode error_code() const { return code_; }
    const std::string& error_message() const { return message_; }

private:
    OperationResult(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ErrorCode code_;
    std::string message_;
};

template <typename T>
class Result {
public:
    static Result<T> ok(T value) {
        return Result<T>(std::move(value));
    }

    static Result<T> fail(ErrorCode code, std::string message) {
        return Result<T>(code, std::move(message));
    }

    bool is_success() const { return code_ == ErrorCode::Success; }
    ErrorCode error_code() const { return code_; }
    const std::string& error_message() const { return message_; }

    const T& value() const {
        return *value_;
    }

    T& value() {
        return *value_;
    }

    const T& value_or(const T& fallback) const {
        return value_ ? *value_ : fallback;
    }

private:
    explicit Result(T value)
        : code_(ErrorCode::Success), message_(""), value_(std::move(value)) {}

    Result(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)), value_(std::nullopt) {}

    ErrorCode code_;
    std::string message_;
    std::optional<T> value_;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_RESULT_HPP
