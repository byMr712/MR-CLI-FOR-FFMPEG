#include "Menu.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../services/LocalizationService.hpp"
#include <conio.h>
#include <iostream>

namespace mr {
namespace ui {

int Menu::arrowSelect(const std::string& title,
                      const std::string& description,
                      const std::vector<std::string>& options,
                      int currentIdx,
                      const std::vector<std::string>& hints,
                      bool inlineMode) {
    int selected = (currentIdx >= 0 && currentIdx < static_cast<int>(options.size())) ? currentIdx : 0;
    int linesRendered = 0;

    while (true) {
        if (!inlineMode) {
            core::Console::clearScreen();
        } else if (linesRendered > 0) {
            std::cout << "\033[" << linesRendered << "A\r\033[J" << std::flush;
        }

        int currentLines = 0;
        auto printLine = [&](const std::string& text, core::ConsoleColor color = core::ConsoleColor::WHITE, bool nl = true) {
            core::Console::setColor(color);
            std::cout << text;
            core::Console::setColor(core::ConsoleColor::WHITE);
            if (nl) {
                std::cout << "\n";
                currentLines++;
            }
        };

        if (inlineMode) {
            std::cout << "\n";
            currentLines++;
        }
        printLine("========================================", core::ConsoleColor::CYAN);
        printLine(" " + title, core::ConsoleColor::CYAN);
        printLine("========================================", core::ConsoleColor::CYAN);

        if (!description.empty()) {
            std::cout << "\n" << description << "\n";
            currentLines += 2;
            for (char c : description) {
                if (c == '\n') currentLines++;
            }
        }

        std::cout << "\n";
        currentLines++;

        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            if (i == selected) {
                core::Console::setColor(core::ConsoleColor::GREEN);
                std::cout << " > " << options[i] << "\n";
                core::Console::setColor(core::ConsoleColor::WHITE);
            } else {
                std::cout << "   " << options[i] << "\n";
            }
            currentLines++;
            for (char c : options[i]) {
                if (c == '\n') currentLines++;
            }
        }

        if (!hints.empty() && selected >= 0 && selected < static_cast<int>(hints.size()) && !hints[selected].empty()) {
            std::cout << "\n";
            currentLines++;
            printLine("----------------------------------------------------------------------", core::ConsoleColor::CYAN);
            core::Console::setColor(core::ConsoleColor::YELLOW);
            std::cout << " [i] " << hints[selected] << "\n";
            core::Console::setColor(core::ConsoleColor::WHITE);
            currentLines++;
            for (char c : hints[selected]) {
                if (c == '\n') currentLines++;
            }
            printLine("----------------------------------------------------------------------", core::ConsoleColor::CYAN);
        }

        std::cout << "\n";
        currentLines++;
        printLine(services::tr("Arrow keys to select, Enter to confirm, ESC or 0 to go back",
                               "Стрелки для выбора, Enter для подтверждения, ESC или 0 для возврата"),
                  core::ConsoleColor::WHITE);

        linesRendered = currentLines;
        std::cout << std::flush;

        wint_t key = _getwch();
        if (key == 27 || key == '0') {
            std::cout << "\n" << std::flush;
            return -1;
        }
        if (key == 13) {
            std::cout << "\n" << std::flush;
            return selected;
        }
        if (key == 0 || key == 0xE0) {
            wint_t scan = _getwch();
            if (scan == 72) { // Up
                selected = (selected > 0) ? selected - 1 : static_cast<int>(options.size()) - 1;
            } else if (scan == 80) { // Down
                selected = (selected < static_cast<int>(options.size()) - 1) ? selected + 1 : 0;
            }
        } else {
            char ch = core::StringUtil::normalizeKeyToEnglish(key);
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                if (options[i].length() >= 2 && options[i][0] == ' ' && options[i][1] == ch) {
                    std::cout << "\n" << std::flush;
                    return i;
                }
                if (options[i].length() >= 1 && options[i][0] == ch) {
                    std::cout << "\n" << std::flush;
                    return i;
                }
            }
        }
    }
}

} // namespace ui
} // namespace mr
