// PKG Studio - explicit error propagation.
//
// The code base never throws for expected failures (malformed input, missing
// files, unsupported formats). Everything that can fail returns a Result<T>
// or a Status so that a caller cannot accidentally ignore an error.
#pragma once

#include <QString>
#include <optional>
#include <utility>

namespace pkg {

struct Error {
    QString message;
    QString detail;

    Error() = default;
    explicit Error(QString msg, QString det = {})
        : message(std::move(msg)), detail(std::move(det)) {}
};

inline Error makeError(QString message, QString detail = {})
{
    return Error{std::move(message), std::move(detail)};
}

/// A value or an error. `operator bool` reports success.
template <typename T>
class Result {
public:
    Result(T value)  // NOLINT(google-explicit-constructor) - mirrors std::expected
        : m_value(std::move(value))
    {
    }
    Result(Error error)  // NOLINT(google-explicit-constructor)
        : m_error(std::move(error))
    {
    }

    [[nodiscard]] bool ok() const { return m_error.message.isEmpty() && m_value.has_value(); }
    explicit operator bool() const { return ok(); }

    [[nodiscard]] T& value() { return *m_value; }
    [[nodiscard]] const T& value() const { return *m_value; }
    [[nodiscard]] T valueOr(T fallback) const
    {
        return m_value.has_value() ? *m_value : std::move(fallback);
    }
    [[nodiscard]] const Error& error() const { return m_error; }

private:
    std::optional<T> m_value;
    Error m_error;
};

/// Error-or-nothing.
class Status {
public:
    Status() = default;
    Status(Error error)  // NOLINT(google-explicit-constructor)
        : m_error(std::move(error))
    {
    }

    [[nodiscard]] bool ok() const { return m_error.message.isEmpty(); }
    explicit operator bool() const { return ok(); }
    [[nodiscard]] const Error& error() const { return m_error; }

private:
    Error m_error;
};

inline Status okStatus()
{
    return Status{};
}

}  // namespace pkg
