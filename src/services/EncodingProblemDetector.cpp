#include "EncodingProblemDetector.hpp"
#include "LocalizationService.hpp"
#include <algorithm>

namespace mr {
namespace services {

domain::EncodingProblem EncodingProblemDetector::detect(const std::string& filePath,
                                                        domain::AccelMode mode,
                                                        domain::AccelMode hybridChoice,
                                                        const HardwareInfo& hw,
                                                        const std::string& outputFormat,
                                                        const FfprobeService& ffprobe) {
    domain::EncodingProblem ep;
    ep.filePath = filePath;
    if (mode == domain::AccelMode::CpuOnly || mode == domain::AccelMode::GpuDecCpuEnc) {
        return ep;
    }

    domain::VideoSourceProperties vp = ffprobe.getVideoProperties(filePath);
    if (vp.pixFmt.empty() && vp.codecName.empty()) {
        return ep;
    }

    domain::AccelMode activeGpu = GpuDetector::getActiveGpuMode(mode, hybridChoice);
    std::string encoderName;
    if (activeGpu == domain::AccelMode::Nvidia) encoderName = "NVENC";
    else if (activeGpu == domain::AccelMode::Amd) encoderName = "AMF";
    else if (activeGpu == domain::AccelMode::Intel) encoderName = "QSV";
    else return ep;

    std::string codecPart;
    if (outputFormat.find("H.264") != std::string::npos) codecPart = "H.264";
    else if (outputFormat.find("H.265") != std::string::npos || outputFormat.find("HEVC") != std::string::npos) codecPart = "H.265/HEVC";
    else if (outputFormat.find("AV1") != std::string::npos) codecPart = "AV1";
    else codecPart = "H.264";

    std::string fullEncoder = codecPart + " (" + encoderName + ")";

    if (mode == domain::AccelMode::Nvidia || mode == domain::AccelMode::Amd || mode == domain::AccelMode::Intel) {
        std::string lowerCodec = vp.codecName;
        std::transform(lowerCodec.begin(), lowerCodec.end(), lowerCodec.begin(), ::tolower);
        if (lowerCodec == "mpeg4" || lowerCodec == "msmpeg4" || lowerCodec == "msmpeg4v1" ||
            lowerCodec == "msmpeg4v2" || lowerCodec == "msmpeg4v3" || lowerCodec == "flv1" ||
            lowerCodec == "vp6" || lowerCodec == "vp6f" || lowerCodec == "wmv1" || lowerCodec == "wmv2" ||
            lowerCodec == "rv10" || lowerCodec == "rv20" || lowerCodec == "rv30" || lowerCodec == "rv40") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "Input video codec (" + vp.codecName + ") cannot be decoded by GPU hardware decoder (" + encoderName + ").\n"
                "Hybrid mode (CPU decode + GPU encode) or Software CPU mode is recommended.",
                "Входной видеокодек (" + vp.codecName + ") не поддерживается аппаратным декодером вашей видеокарты (" + encoderName + ").\n"
                "Рекомендуется использовать Гибридный режим (CPU декод + GPU энкод) или программный режим CPU.");
            return ep;
        }
    }

    if ((vp.width > 0 && vp.width % 2 != 0) || (vp.height > 0 && vp.height % 2 != 0)) {
        ep.hasProblem = true;
        ep.isOddDimensions = true;
        ep.evenWidth = (vp.width % 2 != 0) ? (vp.width + 1) : vp.width;
        ep.evenHeight = (vp.height % 2 != 0) ? (vp.height + 1) : vp.height;
        ep.description = fullEncoder;
        std::string dimStr = std::to_string(vp.width) + "x" + std::to_string(vp.height);
        std::string evenDimStr = std::to_string(ep.evenWidth) + "x" + std::to_string(ep.evenHeight);
        ep.detailedReason = tr(
            "Odd video resolution (" + dimStr + ").\n"
            "Hardware encoders of your graphics card (" + encoderName + ") require width and height to be divisible by 2.\n"
            "Recommended: Automatically align resolution to even (" + evenDimStr + ") or encode via CPU.",
            "Нечётное разрешение видео (" + dimStr + ").\n"
            "Аппаратные кодировщики вашей видеокарты (" + encoderName + ") требуют, чтобы ширина и высота делились на 2.\n"
            "Рекомендуется: Автоматически выровнять разрешение до чётного (" + evenDimStr + ") или кодировать через CPU.");
        return ep;
    }

    if (vp.width > 4096 || vp.height > 4096) {
        if (!GpuDetector::is8KSupportedByGPU(activeGpu, codecPart, hw)) {
            ep.hasProblem = true;
            ep.isUltraHighRes = true;
            ep.description = fullEncoder;
            std::string dimStr = std::to_string(vp.width) + "x" + std::to_string(vp.height);
            std::string reasonDetail;
            if (codecPart == "H.264") {
                reasonDetail = tr(
                    "Standard H.264 profile does not support resolutions higher than 4096x4096 on any graphics card.\n"
                    "Recommended: Switch to H.265 (HEVC) / AV1 or use software CPU encoder.",
                    "Стандартный профиль H.264 не поддерживает разрешение выше 4096x4096 ни на одной видеокарте.\n"
                    "Рекомендуется: Переключить на H.265 (HEVC) / AV1 или программный кодировщик CPU.");
            } else {
                reasonDetail = tr(
                    "Hardware encoder of your graphics card (" + encoderName + ") does not support resolution higher than 4096x4096.\n"
                    "Recommended: Use software CPU encoder (libx264 / libx265) or reduce resolution to 4K.",
                    "Аппаратный энкодер вашей видеокарты (" + encoderName + ") не поддерживает разрешение выше 4096x4096.\n"
                    "Рекомендуется: Использовать программный кодировщик CPU (libx264 / libx265) или уменьшить разрешение до 4K.");
            }
            ep.detailedReason = tr("Ultra-high video resolution (" + dimStr + ").\n", "Сверхвысокое разрешение видео (" + dimStr + ").\n") + reasonDetail;
            return ep;
        }
    }

    if (vp.is10bit) {
        if (codecPart == "H.264") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "10-bit pixel format (" + vp.pixFmt + ") cannot be encoded to hardware H.264 without loss.\n"
                "Hardware encoder of your graphics card (" + encoderName + ") does not support 10-bit input.\n"
                "Recommended: Choose H.265 (HEVC) / AV1 codec or use software CPU encoder.",
                "10-битный формат пикселей (" + vp.pixFmt + ") не может быть закодирован в аппаратный H.264 без потерь.\n"
                "Аппаратный энкодер вашей видеокарты (" + encoderName + ") не поддерживает 10-битный вход.\n"
                "Рекомендуется: Выбрать кодек H.265 (HEVC) / AV1 или программный кодировщик CPU.");
            return ep;
        }
    }

    if (vp.isSubsampled422 || vp.isSubsampled444) {
        if (codecPart == "H.264" || codecPart == "H.265/HEVC") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "Chroma subsampling " + std::string(vp.isSubsampled444 ? "4:4:4" : "4:2:2") + " (" + vp.pixFmt + ") is not supported by " + codecPart + " encoder (" + encoderName + ").\n"
                "Recommended: Use CPU software encoder (libx264) or choose another format.",
                "Субдискретизация цветности " + std::string(vp.isSubsampled444 ? "4:4:4" : "4:2:2") + " (" + vp.pixFmt + ") не поддерживается кодеком " + codecPart + " (" + encoderName + ").\n"
                "Рекомендуется: Использовать программный кодировщик CPU (libx264) или выбрать другой формат.");
            return ep;
        }
    }

    return ep;
}

} // namespace services
} // namespace mr
