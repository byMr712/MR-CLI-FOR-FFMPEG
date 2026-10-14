#pragma once
#include <string>
#include "../domain/AppConfig.hpp"
#include "../services/ConfigService.hpp"
#include "../services/LocalizationService.hpp"
#include "../services/FfprobeService.hpp"
#include "../services/CoverService.hpp"
#include "../services/ComponentInstaller.hpp"
#include "../services/GpuDetector.hpp"

namespace mr {
namespace app {

class AppContext {
public:
    static AppContext& instance() {
        static AppContext s_instance;
        return s_instance;
    }

    services::ConfigService configService;
    services::FfprobeService ffprobeService;
    services::CoverService coverService;
    services::ComponentInstaller installer;
    services::HardwareInfo hardware;

    domain::AppConfig& config() { return configService.getConfig(); }
    const domain::AppConfig& config() const { return configService.getConfig(); }

    void refreshPaths() {
        ffprobeService.setFfprobePath(installer.getFfprobePath());
        coverService.setPaths(installer.getFfmpegPath(), installer.getFfprobePath());
    }

private:
    AppContext() = default;
};

} // namespace app
} // namespace mr
