#pragma once
#include <string>
#include "Enums.hpp"

namespace mr {
namespace domain {

struct EncodingProblem {
    bool hasProblem = false;
    bool isOddDimensions = false;
    bool isUltraHighRes = false;
    int evenWidth = 0;
    int evenHeight = 0;
    std::string filePath;
    std::string description;
    std::string detailedReason;
};

struct SubtitleDialogResult {
    SubtitleIncompatAction action = SubtitleIncompatAction::SkipCurrent;
    bool applyToAll = false;
};

} // namespace domain
} // namespace mr
