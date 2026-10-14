#pragma once
#include <string>
#include <vector>

namespace mr {
namespace ui {

class Menu {
public:
    // Interactive arrow-key navigation menu with hotkeys, hint descriptions, and ANSI repainting.
    static int arrowSelect(const std::string& title,
                           const std::string& description,
                           const std::vector<std::string>& options,
                           int currentIdx = 0,
                           const std::vector<std::string>& hints = {},
                           bool inlineMode = false);
};

// Convenience global helper
inline int arrowSelect(const std::string& title,
                       const std::string& description,
                       const std::vector<std::string>& options,
                       int currentIdx = 0,
                       const std::vector<std::string>& hints = {},
                       bool inlineMode = false) {
    return Menu::arrowSelect(title, description, options, currentIdx, hints, inlineMode);
}

} // namespace ui
} // namespace mr
