#pragma once
#include "WinLibs.hpp"
#include <string>
#include <string_view>

namespace mr {
namespace core {

enum class ConsoleColor : WORD {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 11,
    Red = 4,
    Yellow = 6,
    White = 7,
    LightGreen = 10,
    LightRed = 12,
    LightBlue = 9,
    LightMagenta = 13,
    BLACK = 0,
    BLUE = 1,
    GREEN = 2,
    CYAN = 11,
    RED = 4,
    YELLOW = 6,
    WHITE = 7
};

class Console {
public:
    using Color = ConsoleColor;
    static inline const ConsoleColor BLACK = ConsoleColor::Black;
    static inline const ConsoleColor BLUE = ConsoleColor::Blue;
    static inline const ConsoleColor GREEN = ConsoleColor::Green;
    static inline const ConsoleColor CYAN = ConsoleColor::Cyan;
    static inline const ConsoleColor RED = ConsoleColor::Red;
    static inline const ConsoleColor YELLOW = ConsoleColor::Yellow;
    static inline const ConsoleColor WHITE = ConsoleColor::White;

    static void initUTF8() noexcept;
    static void initConsole() noexcept { initUTF8(); }
    static void cleanupConsole() noexcept {}
    static void setConsoleTitle(const std::wstring& title) noexcept { SetConsoleTitleW(title.c_str()); }
    static void setColor(ConsoleColor c) noexcept;
    static void print(std::string_view text, ConsoleColor c = ConsoleColor::White, bool newline = true);
    static void printColor(std::string_view text, ConsoleColor c = ConsoleColor::White, bool newline = true) {
        print(text, c, newline);
    }
    static void clearScreen();
    static void flushInput() noexcept;
    static void waitForKey() noexcept;
    static char getMenuChoice() noexcept;
    static wint_t getWChar() noexcept;
    static bool inputLineWithEscape(std::string& result, const std::string& prompt = "");
    static bool isEscapePressed() noexcept;
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
