#pragma once
#include <string>
#include "Enums.hpp"

namespace mr {
namespace domain {

struct AppConfig {
    std::string configPath;
    std::string outputPath;
    std::string outputFormat = "MP4(H.264)";
    std::string outputResolution = "original";
    std::string outputFps = "original";
    std::string audioBitrate = "192";
    std::string videoBitrate = "auto";
    std::string crfValue = "23";
    std::string preset = "medium";
    bool saveCover = true;
    bool overwriteFiles = false;
    bool keepMetadata = true;
    bool videoCodecAsk = true;
    bool audioCodecAsk = false;
    std::string audioCodec = "copy";
    std::string subtitleAction = "ask";
    std::string deleteOriginal = "ask";
    bool parallelBatch = false;
    Language language = Language::Russian;
    Language currentLang = Language::Russian;
    AccelMode accelMode = AccelMode::CpuOnly;
    AccelMode hybridGpuChoice = AccelMode::CpuOnly;
    std::string customFfmpegArgs;
};

} // namespace domain
} // namespace mr
