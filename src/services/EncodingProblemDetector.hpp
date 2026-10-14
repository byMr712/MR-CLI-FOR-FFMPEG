#pragma once
#include <string>
#include "../domain/Enums.hpp"
#include "../domain/EncodingProblem.hpp"
#include "GpuDetector.hpp"
#include "FfprobeService.hpp"

namespace mr {
namespace services {

class EncodingProblemDetector {
public:
    static domain::EncodingProblem detect(const std::string& filePath,
                                          domain::AccelMode mode,
                                          domain::AccelMode hybridChoice,
                                          const HardwareInfo& hw,
                                          const std::string& outputFormat,
                                          const FfprobeService& ffprobe);

    static domain::EncodingProblem detectEncodingProblem(const std::string& filePath,
                                                         const FfprobeService& ffprobe,
                                                         domain::AccelMode mode = domain::AccelMode::Nvidia,
                                                         domain::AccelMode hybridChoice = domain::AccelMode::Nvidia,
                                                         const std::string& outputFormat = "MP4") {
        HardwareInfo dummyHw;
        return detect(filePath, mode, hybridChoice, dummyHw, outputFormat, ffprobe);
    }
};

} // namespace services
} // namespace mr
