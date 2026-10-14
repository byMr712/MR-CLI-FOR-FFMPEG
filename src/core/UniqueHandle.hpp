#pragma once
#include "WinLibs.hpp"
#include <utility>

namespace mr {
namespace core {

class UniqueHandle {
public:
    constexpr UniqueHandle() noexcept : handle_(INVALID_HANDLE_VALUE) {}
    explicit UniqueHandle(HANDLE h) noexcept : handle_(h) {}
    ~UniqueHandle() noexcept { close(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept {
        if (handle_ != h) {
            close();
            handle_ = h;
        }
    }

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool isValid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }
    explicit operator bool() const noexcept { return isValid(); }

    HANDLE release() noexcept {
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return h;
    }

    HANDLE* put() noexcept {
        close();
        return &handle_;
    }

private:
    void close() noexcept {
        if (isValid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE handle_;
};

class ScopedCoInit {
public:
    explicit ScopedCoInit(DWORD dwCoInit = COINIT_APARTMENTTHREADED) noexcept
        : hr_(CoInitializeEx(nullptr, dwCoInit)) {}

    ~ScopedCoInit() noexcept {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool succeeded() const noexcept { return SUCCEEDED(hr_); }
    [[nodiscard]] HRESULT result() const noexcept { return hr_; }

    ScopedCoInit(const ScopedCoInit&) = delete;
    ScopedCoInit& operator=(const ScopedCoInit&) = delete;

private:
    HRESULT hr_;
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
