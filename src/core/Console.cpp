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

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = 0;
    if (GetConsoleMode(hIn, &inMode)) {
        inMode &= ~ENABLE_QUICK_EDIT_MODE;
        inMode |= ENABLE_EXTENDED_FLAGS;
        SetConsoleMode(hIn, inMode);
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

void Console::flushInput() noexcept {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE && hIn != NULL) {
        FlushConsoleInputBuffer(hIn);
    }
    while (_kbhit()) {
        _getch();
    }
}

void Console::waitForKey() noexcept {
    flushInput();
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

bool Console::isEscapePressed() noexcept {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE && hIn != NULL) {
        DWORD numEvents = 0;
        if (GetNumberOfConsoleInputEvents(hIn, &numEvents) && numEvents > 0) {
            INPUT_RECORD records[32];
            DWORD eventsRead = 0;
            if (PeekConsoleInputW(hIn, records, 32, &eventsRead) && eventsRead > 0) {
                for (DWORD i = 0; i < eventsRead; ++i) {
                    if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown) {
                        if (records[i].Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE) {
                            flushInput();
                            return true;
                        }
                    }
                }
            }
        }
    }

    while (_kbhit()) {
        int key = _getch();
        if (key == 0 || key == 0xE0) {
            if (_kbhit()) _getch();
        } else if (key == 27) {
            flushInput();
            return true;
        }
    }

    return false;
}

int Console::getConsoleWidth() noexcept {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            if (w > 0) return w;
            if (csbi.dwSize.X > 0) return csbi.dwSize.X;
        }
    }
    return 80;
}

} // namespace core
} // namespace mr
