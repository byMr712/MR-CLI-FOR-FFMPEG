#pragma once
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include <string>

namespace mr {
namespace core {

class Clipboard {
public:
    [[nodiscard]] static std::string getText() {
        if (!OpenClipboard(nullptr)) return "";

        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) {
            CloseClipboard();
            return "";
        }

        auto* t = static_cast<wchar_t*>(GlobalLock(h));
        if (!t) {
            CloseClipboard();
            return "";
        }

        std::wstring r(t);
        GlobalUnlock(h);
        CloseClipboard();
        return wstringToUtf8(r);
    }
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
