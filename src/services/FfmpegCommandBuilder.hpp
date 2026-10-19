#pragma once
#include <string>
#include <vector>
#include "../domain/Enums.hpp"
#include "CoverService.hpp"
#include "FfprobeService.hpp"

namespace mr {
namespace services {

struct FFmpegTarget {
    std::string targetPath;
    std::string writePath;
    std::string inputPath;
    bool isTemp = false;
    bool coverEmbedded = false;
};

class FfmpegCommandBuilder {
public:
    static FFmpegTarget prepareTarget(const std::string& targetPath, const std::vector<std::string>& inputPaths);
    static bool finalizeTarget(const FFmpegTarget& ft, bool success, const CoverService* coverService = nullptr, bool saveCover = true);

    static std::string getOutputExtension(const std::string& outputFormat);
    static std::string buildOutputPath(const std::string& inputPath,
                                       const std::string& outputBaseDir,
                                       bool deleteOriginal,
                                       const std::string& outputFormat,
                                       const std::string& suffix = "",
                                       const std::string& forceExt = "");

    static std::string getVideoCodecArgs(domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat);
    static std::string getAudioCodecSettingName(const std::string& audioCodec);
    static std::string getAudioCodecArgs(const std::string& audioCodec, const std::string& audioBitrate, const std::string& outputFormat);
    static std::string getVideoQualityArgs(const std::string& crfVal, domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat, bool forceCPU = false);
    static std::string getVideoPresetArgs(const std::string& preset, domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat, bool forceCPU = false);
    static std::string buildVideoFilterSpec(const std::string& filePath,
                                            const FfprobeService& probe,
                                            const std::string& defaultResolution,
                                            bool useAutoAlign = false,
                                            bool useDownscale4K = false,
                                            const std::string& extraFilter = "",
                                            const std::string& overrideResolution = "");
};

} // namespace services
} // namespace mr
