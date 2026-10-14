#pragma once
#include "IOperation.hpp"
#include <string>

namespace mr {
namespace operations {

class SettingsOperation : public IOperation {
public:
    std::string getId() const override { return "s"; }
    std::string getName() const override;
    void execute() override;

    // Sub-settings dialogs
    static void selectOutputFormat();
    static void selectResolution();
    static void selectFPS();
    static void selectPreset();
    static void selectCRF();
    static void selectAudioBitrate();
    static void selectCodecSettings();
};

} // namespace operations
} // namespace mr
