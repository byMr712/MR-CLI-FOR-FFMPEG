#pragma once
#include "WinLibs.hpp"
#include <string>
#include <string_view>

namespace mr {
namespace core {

[[nodiscard]] inline std::wstring utf8ToWstring(std::string_view str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

[[nodiscard]] inline std::string wstringToUtf8(std::wstring_view wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

[[nodiscard]] inline size_t utf8Length(std::string_view str) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if ((c & 0x80) == 0) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        count++;
    }
    return count;
}

[[nodiscard]] inline std::string truncateUtf8(std::string_view str, size_t maxChars) {
    if (maxChars == 0) return "";
    size_t chars = 0;
    size_t targetChars = (maxChars > 3) ? (maxChars - 3) : maxChars;
    size_t targetByte = 0;

    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        size_t len = 1;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;

        if (i + len > str.size()) break;

        chars++;
        if (chars == targetChars) {
            targetByte = i + len;
        }
        if (chars > maxChars) {
            break;
        }
        i += len;
    }

    if (chars <= maxChars) {
        return std::string(str);
    }

    if (chars > maxChars && targetByte > 0) {
        return std::string(str.substr(0, targetByte)) + "...";
    }

    return std::string(str.substr(0, (std::min)(str.size(), maxChars)));
}

[[nodiscard]] inline char normalizeKeyToEnglish(wint_t wc) noexcept {
    switch (wc) {
        case L'й': case L'Й': return 'q';
        case L'ц': case L'Ц': return 'w';
        case L'у': case L'У': return 'e';
        case L'к': case L'К': return 'r';
        case L'е': case L'Е': return 't';
        case L'н': case L'Н': return 'y';
        case L'г': case L'Г': return 'u';
        case L'ш': case L'Ш': return 'i';
        case L'щ': case L'Щ': return 'o';
        case L'з': case L'З': return 'p';
        case L'ф': case L'Ф': return 'a';
        case L'ы': case L'Ы': return 's';
        case L'в': case L'В': return 'd';
        case L'а': case L'А': return 'f';
        case L'п': case L'П': return 'g';
        case L'р': case L'Р': return 'h';
        case L'о': case L'О': return 'j';
        case L'л': case L'Л': return 'k';
        case L'д': case L'Д': return 'l';
        case L'я': case L'Я': return 'z';
        case L'ч': case L'Ч': return 'x';
        case L'с': case L'С': return 'c';
        case L'м': case L'М': return 'v';
        case L'и': case L'И': return 'b';
        case L'т': case L'Т': return 'n';
        case L'ь': case L'Ь': return 'm';
        default:
            if (wc >= L'A' && wc <= L'Z') return static_cast<char>(wc + 32);
            if (wc < 128) return static_cast<char>(wc);
            return 0;
    }
}

class StringUtil {
public:
    static std::wstring utf8ToWstring(std::string_view str) { return mr::core::utf8ToWstring(str); }
    static std::string wstringToUtf8(std::wstring_view wstr) { return mr::core::wstringToUtf8(wstr); }
    static size_t utf8Length(std::string_view str) noexcept { return mr::core::utf8Length(str); }
    static std::string truncateUtf8(std::string_view str, size_t maxChars) { return mr::core::truncateUtf8(str, maxChars); }
    static char normalizeKeyToEnglish(wint_t wc) noexcept { return mr::core::normalizeKeyToEnglish(wc); }
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
