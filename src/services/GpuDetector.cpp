#include "GpuDetector.hpp"
#include "LocalizationService.hpp"
#include "../core/WinLibs.hpp"
#include "../core/StringUtil.hpp"
#include "../core/ProcessRunner.hpp"
#include <intrin.h>
#include <algorithm>

namespace mr {
namespace services {

static std::string detectCPUInternal() {
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = static_cast<unsigned int>(cpuInfo[0]);
    if (nExIds >= 0x80000004) {
        char cpuBrand[65] = { 0 };
        __cpuid(reinterpret_cast<int*>(cpuBrand), 0x80000002);
        __cpuid(reinterpret_cast<int*>(cpuBrand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(cpuBrand + 32), 0x80000004);
        std::string cpu = cpuBrand;
        while (!cpu.empty() && (cpu.front() == ' ' || cpu.front() == '\t')) cpu.erase(cpu.begin());
        while (!cpu.empty() && (cpu.back() == ' ' || cpu.back() == '\t' || cpu.back() == '\r' || cpu.back() == '\n')) cpu.pop_back();
        std::string res;
        bool inSpace = false;
        for (char c : cpu) {
            if (c == ' ' || c == '\t') {
                if (!inSpace) {
                    res += ' ';
                    inSpace = true;
                }
            } else {
                res += c;
                inSpace = false;
            }
        }
        return res;
    }
    return "";
}

HardwareInfo GpuDetector::detect(const std::string& ffprobePath) {
    HardwareInfo hw;
    hw.cpuName = detectCPUInternal();

    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);
    for (int i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); ++i) {
        std::wstring name(dd.DeviceString);
        std::string nameUtf8 = core::StringUtil::wstringToUtf8(name);
        std::string lower = nameUtf8;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.find("nvidia") != std::string::npos || lower.find("geforce") != std::string::npos ||
            lower.find("rtx") != std::string::npos || lower.find("gtx") != std::string::npos) {
            hw.hasNvidia = true;
            if (hw.nvidiaName.empty()) hw.nvidiaName = nameUtf8;
        } else if (lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos) {
            hw.hasAmd = true;
            if (hw.amdName.empty()) hw.amdName = nameUtf8;
        } else if (lower.find("intel") != std::string::npos) {
            hw.hasIntel = true;
            if (hw.intelName.empty()) hw.intelName = nameUtf8;
        }
        dd.cb = sizeof(dd);
    }

    if (!ffprobePath.empty()) {
        std::string output = core::ProcessRunner::runCommand("\"" + ffprobePath + "\" -hide_banner -encoders 2>&1");
        bool hasNvenc = (output.find("h264_nvenc") != std::string::npos) || (output.find("hevc_nvenc") != std::string::npos);
        bool hasAmf = (output.find("h264_amf") != std::string::npos) || (output.find("hevc_amf") != std::string::npos);
        bool hasQsv = (output.find("h264_qsv") != std::string::npos) || (output.find("hevc_qsv") != std::string::npos);

        if (!hasNvenc) hw.hasNvidia = false;
        if (!hasAmf) hw.hasAmd = false;
        if (!hasQsv) hw.hasIntel = false;
    }

    if (hw.hasNvidia && hw.nvidiaName.empty()) hw.nvidiaName = "NVIDIA (NVENC)";
    if (hw.hasIntel && hw.intelName.empty()) hw.intelName = "Intel (QSV)";
    if (hw.hasAmd && hw.amdName.empty()) hw.amdName = "AMD (AMF)";

    return hw;
}

bool GpuDetector::is8KSupportedByGPU(domain::AccelMode gpu, const std::string& codecPart, const HardwareInfo& hw) {
    if (codecPart == "H.264") {
        return false;
    }

    if (gpu == domain::AccelMode::Nvidia) {
        std::string name = hw.nvidiaName;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find("rtx") != std::string::npos ||
            name.find("1660") != std::string::npos ||
            name.find("1650 super") != std::string::npos ||
            name.find("titan rtx") != std::string::npos ||
            name.find("a4000") != std::string::npos ||
            name.find("a5000") != std::string::npos ||
            name.find("a6000") != std::string::npos ||
            name.find("a100") != std::string::npos ||
            name.find("h100") != std::string::npos ||
            name.find("l4") != std::string::npos) {
            return true;
        }
        return false;
    }

    if (gpu == domain::AccelMode::Amd) {
        std::string name = hw.amdName;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find("rx 7") != std::string::npos || name.find("rx 8") != std::string::npos || name.find("rx 9") != std::string::npos ||
            name.find("7900") != std::string::npos || name.find("7800") != std::string::npos || name.find("7700") != std::string::npos || name.find("7600") != std::string::npos) {
            return true;
        }
        return false;
    }

    if (gpu == domain::AccelMode::Intel) {
        std::string name = hw.intelName;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name.find("arc") != std::string::npos || name.find("ultra") != std::string::npos) {
            return true;
        }
        return false;
    }

    return false;
}

domain::AccelMode GpuDetector::getActiveGpuMode(domain::AccelMode mode, domain::AccelMode hybridChoice) {
    if (mode == domain::AccelMode::CpuDecGpuEnc || mode == domain::AccelMode::GpuDecCpuEnc) {
        return hybridChoice;
    }
    return mode;
}

std::string GpuDetector::getAccelerationModeName(domain::AccelMode mode) {
    switch (mode) {
        case domain::AccelMode::CpuOnly:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
        case domain::AccelMode::CpuDecGpuEnc:
            return tr("Hybrid (CPU + GPU)", "Смешанный (CPU + GPU)");
        case domain::AccelMode::Nvidia:
            return tr("Hardware NVIDIA (NVENC)", "Аппаратный NVIDIA (NVENC)");
        case domain::AccelMode::Intel:
            return tr("Hardware INTEL (QSV)", "Аппаратный INTEL (QSV)");
        case domain::AccelMode::Amd:
            return tr("Hardware AMD (AMF)", "Аппаратный AMD (AMF)");
        case domain::AccelMode::GpuDecCpuEnc:
            return tr("Reverse Hybrid (GPU + CPU)", "Обратный смешанный (GPU + CPU)");
        default:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
    }
}

std::string GpuDetector::getAccelerationHardwareInfo(domain::AccelMode mode, domain::AccelMode hybridChoice, const HardwareInfo& hw) {
    std::string cpuName = !hw.cpuName.empty() ? hw.cpuName : "CPU";
    std::string gpuName;
    std::string gpuTag;

    auto resolveGpu = [&]() {
        if (hybridChoice == domain::AccelMode::Nvidia) {
            gpuTag = "NVENC";
            gpuName = !hw.nvidiaName.empty() ? hw.nvidiaName : "NVIDIA";
        } else if (hybridChoice == domain::AccelMode::Intel) {
            gpuTag = "QSV";
            gpuName = !hw.intelName.empty() ? hw.intelName : "Intel";
        } else if (hybridChoice == domain::AccelMode::Amd) {
            gpuTag = "AMF";
            gpuName = !hw.amdName.empty() ? hw.amdName : "AMD";
        }
    };

    switch (mode) {
        case domain::AccelMode::CpuOnly:
            return "[" + cpuName + "]\n " + cpuName;
        case domain::AccelMode::CpuDecGpuEnc:
            resolveGpu();
            return "[CPU+GPU(" + gpuTag + ")]\n " + cpuName + " + " + gpuName;
        case domain::AccelMode::Nvidia:
            gpuName = !hw.nvidiaName.empty() ? hw.nvidiaName : "NVIDIA";
            return "[GPU(NVENC)]\n " + gpuName;
        case domain::AccelMode::Intel:
            gpuName = !hw.intelName.empty() ? hw.intelName : "Intel";
            return "[GPU(QSV)]\n " + gpuName;
        case domain::AccelMode::Amd:
            gpuName = !hw.amdName.empty() ? hw.amdName : "AMD";
            return "[GPU(AMF)]\n " + gpuName;
        case domain::AccelMode::GpuDecCpuEnc:
            resolveGpu();
            return "[GPU(" + gpuTag + ")+CPU]\n " + gpuName + " + " + cpuName;
        default:
            return "[" + cpuName + "]\n " + cpuName;
    }
}

std::string GpuDetector::getHWAccelArg(domain::AccelMode mode, domain::AccelMode hybridChoice, bool pureReencode) {
    if (mode == domain::AccelMode::CpuOnly || mode == domain::AccelMode::CpuDecGpuEnc) {
        return "";
    }
    if (mode == domain::AccelMode::GpuDecCpuEnc) {
        if (pureReencode) {
            domain::AccelMode gpu = getActiveGpuMode(mode, hybridChoice);
            if (gpu == domain::AccelMode::Nvidia) return " -hwaccel cuda";
            if (gpu == domain::AccelMode::Intel) return " -hwaccel qsv";
        }
        return " -hwaccel auto";
    }
    if (pureReencode) {
        if (mode == domain::AccelMode::Nvidia) return " -hwaccel cuda";
        if (mode == domain::AccelMode::Intel) return " -hwaccel qsv";
    }
    return " -hwaccel auto";
}

void HardwareInfo::detectAll(const std::string& ffprobePath) {
    *this = GpuDetector::detect(ffprobePath);
}

std::string HardwareInfo::getAccelerationModeName(domain::AccelMode mode) const {
    return GpuDetector::getAccelerationModeName(mode);
}

std::string HardwareInfo::getAccelerationHardwareInfo(domain::AccelMode mode, domain::AccelMode hybridChoice) const {
    return GpuDetector::getAccelerationHardwareInfo(mode, hybridChoice, *this);
}

domain::AccelMode HardwareInfo::getActiveGpuMode(domain::AccelMode mode, domain::AccelMode hybridChoice) const {
    return GpuDetector::getActiveGpuMode(mode, hybridChoice);
}

} // namespace services
} // namespace mr
