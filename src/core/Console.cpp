#include "Console.hpp"
#include "Clipboard.hpp"
#include "StringUtil.hpp"
#include <iostream>
#include <conio.h>

namespace mr {
namespace core {

void Console::initUTF8() noexcept {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
}

void Console::setColor(ConsoleColor c) noexcept {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), static_cast<WORD>(c));
}

void Console::print(std::string_view text, ConsoleColor c, bool newline) {
    if (std::cout.fail() || std::cout.bad()) {
        std::cout.clear();
    }
    setColor(c);
    std::cout << text;
    setColor(ConsoleColor::White);
    if (newline) {
        std::cout << '\n';
    }
}

void Console::clearScreen() {
    if (std::cout.fail() || std::cout.bad()) {
        std::cout.clear();
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        std::cout << "\033[2J\033[H" << std::flush;
        return;
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
        std::cout << "\033[2J\033[H" << std::flush;
        return;
    }
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD count = 0;
    COORD homeCoords = { 0, 0 };
    FillConsoleOutputCharacterW(hOut, L' ', cellCount, homeCoords, &count);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hOut, homeCoords);
}

void Console::waitForKey() noexcept {
    _getwch();
}

wint_t Console::getWChar() noexcept {
    return _getwch();
}

char Console::getMenuChoice() noexcept {
    wint_t wc = _getwch();
    if (wc == 27) return 27; // ESC
    char c = StringUtil::normalizeKeyToEnglish(wc);
    if (c >= 'A' && c <= 'Z') {
        c += 32;
    }
    return c;
}

bool Console::inputLineWithEscape(std::string& result, const std::string& prompt) {
    if (!prompt.empty()) std::cout << prompt << std::flush;
    result.clear();

    std::wstring buffer;

    while (true) {
        wint_t wc = _getwch();

        if (wc == 27) { // ESC key
            std::cout << "\n";
            return false;
        }

        if (wc == 13) { // Enter (\r)
            std::cout << "\n";
            break;
        }

        if (wc == 8) { // Backspace
            if (!buffer.empty()) {
                buffer.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            continue;
        }

        if (wc == 22) { // Ctrl+V (Paste)
            std::string clipText = Clipboard::getText();
            if (!clipText.empty()) {
                std::wstring wclip = StringUtil::utf8ToWstring(clipText);
                buffer += wclip;
                std::cout << clipText << std::flush;
            }
            continue;
        }

        if (wc >= 32) {
            buffer.push_back(static_cast<wchar_t>(wc));
            std::string utf8Char = StringUtil::wstringToUtf8(std::wstring(1, static_cast<wchar_t>(wc)));
            std::cout << utf8Char << std::flush;
        }
    }

    result = StringUtil::wstringToUtf8(buffer);
    return true;
}

} // namespace core
} // namespace mr
