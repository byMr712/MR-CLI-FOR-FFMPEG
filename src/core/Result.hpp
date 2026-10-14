#pragma once
#include <variant>
#include <string>
#include <optional>

namespace mr {
namespace core {

template <typename T, typename E = std::string>
class Result {
public:
    Result(const T& val) : data_(val) {}
    Result(T&& val) : data_(std::move(val)) {}
    Result(const E& err) : data_(err) {}
    Result(E&& err) : data_(std::move(err)) {}

    [[nodiscard]] bool hasValue() const noexcept {
        return std::holds_alternative<T>(data_);
    }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const T& value() const { return std::get<T>(data_); }
    [[nodiscard]] T& value() { return std::get<T>(data_); }

    [[nodiscard]] const E& error() const { return std::get<E>(data_); }
    [[nodiscard]] E& error() { return std::get<E>(data_); }

    [[nodiscard]] T valueOr(T defaultVal) const {
        return hasValue() ? std::get<T>(data_) : defaultVal;
    }

private:
    std::variant<T, E> data_;
};

template <typename E>
class Result<void, E> {
public:
    Result() : error_(std::nullopt) {}
    Result(const E& err) : error_(err) {}
    Result(E&& err) : error_(std::move(err)) {}

    [[nodiscard]] bool hasValue() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const E& error() const { return error_.value(); }

private:
    std::optional<E> error_;
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
