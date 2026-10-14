#pragma once
#include <string>
#include "../domain/Enums.hpp"

namespace mr {
namespace services {

struct HardwareInfo {
    std::string cpuName;
    std::string nvidiaName;
    std::string intelName;
    std::string amdName;
    bool hasNvidia = false;
    bool hasIntel = false;
    bool hasAmd = false;

    void detectAll(const std::string& ffprobePath = "");
    std::string getAccelerationModeName(domain::AccelMode mode) const;
    std::string getAccelerationHardwareInfo(domain::AccelMode mode, domain::AccelMode hybridChoice) const;
    domain::AccelMode getActiveGpuMode(domain::AccelMode mode, domain::AccelMode hybridChoice) const;
};

class GpuDetector {
public:
    static HardwareInfo detect(const std::string& ffprobePath = "");
    static bool is8KSupportedByGPU(domain::AccelMode gpu, const std::string& codecPart, const HardwareInfo& hw);
    static domain::AccelMode getActiveGpuMode(domain::AccelMode mode, domain::AccelMode hybridChoice);
    static std::string getAccelerationModeName(domain::AccelMode mode);
    static std::string getAccelerationHardwareInfo(domain::AccelMode mode, domain::AccelMode hybridChoice, const HardwareInfo& hw);
    static std::string getHWAccelArg(domain::AccelMode mode, domain::AccelMode hybridChoice, bool pureReencode = false);
};

} // namespace services
} // namespace mr
