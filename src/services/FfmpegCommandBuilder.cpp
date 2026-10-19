#include "FfmpegCommandBuilder.hpp"
#include "GpuDetector.hpp"
#include "LocalizationService.hpp"
#include "../core/WinLibs.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/Console.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace mr {
namespace services {

FFmpegTarget FfmpegCommandBuilder::prepareTarget(const std::string& targetPath, const std::vector<std::string>& inputPaths) {
    FFmpegTarget ft;
    ft.targetPath = targetPath;
    ft.writePath = targetPath;
    ft.isTemp = false;
    if (!inputPaths.empty()) ft.inputPath = inputPaths[0];

    std::wstring wTarget = core::StringUtil::utf8ToWstring(targetPath);
    if (wTarget.empty()) return ft;

    bool conflictsWithInput = false;
    for (const auto& inp : inputPaths) {
        if (inp.empty()) continue;
        std::wstring wInp = core::StringUtil::utf8ToWstring(inp);
        if (_wcsicmp(wTarget.c_str(), wInp.c_str()) == 0) {
            conflictsWithInput = true;
            break;
        }

        std::error_code ec;
        auto absTarget = fs::weakly_canonical(fs::u8path(targetPath), ec);
        auto absIn = fs::weakly_canonical(fs::u8path(inp), ec);
        if (!ec && absTarget == absIn) {
            conflictsWithInput = true;
            break;
        }
    }

    if (conflictsWithInput) {
        fs::path p = fs::u8path(targetPath);
        std::wstring stemW = p.stem().wstring();
        std::wstring extW = p.extension().wstring();
        fs::path tempP = p.parent_path() / (stemW + L".~mr_tmp" + extW);
        ft.writePath = core::StringUtil::wstringToUtf8(tempP.wstring());
        ft.isTemp = true;
    }

    return ft;
}

bool FfmpegCommandBuilder::finalizeTarget(const FFmpegTarget& ft, bool success, const CoverService* coverService, bool saveCover) {
    if (!success) {
        std::wstring wWrite = core::StringUtil::utf8ToWstring(ft.writePath);
        for (int retry = 0; retry < 20; ++retry) {
            if (!core::PathProvider::fileExists(ft.writePath)) break;
            if (DeleteFileW(wWrite.c_str())) break;
            Sleep(50);
        }
        return false;
    }

    if (!ft.isTemp) {
        if (!ft.coverEmbedded && coverService) {
            coverService->processCover(ft.inputPath, ft.targetPath, saveCover);
        }
        return true;
    }

    std::wstring wTemp = core::StringUtil::utf8ToWstring(ft.writePath);
    std::wstring wTarget = core::StringUtil::utf8ToWstring(ft.targetPath);

    if (success && core::PathProvider::fileExists(ft.writePath)) {
        DWORD targetAttrs = GetFileAttributesW(wTarget.c_str());
        if (targetAttrs != INVALID_FILE_ATTRIBUTES && (targetAttrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wTarget.c_str(), targetAttrs & ~FILE_ATTRIBUTE_READONLY);
        }
        DWORD tempAttrs = GetFileAttributesW(wTemp.c_str());
        if (tempAttrs != INVALID_FILE_ATTRIBUTES && (tempAttrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wTemp.c_str(), tempAttrs & ~FILE_ATTRIBUTE_READONLY);
        }

        bool replaced = false;

        // Stage 1: MoveFileExW with MOVEFILE_REPLACE_EXISTING
        for (int retry = 0; retry < 15; ++retry) {
            if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                replaced = true;
                break;
            }
            Sleep(100);
        }

        // Stage 2: ReplaceFileW
        if (!replaced) {
            for (int retry = 0; retry < 10; ++retry) {
                if (ReplaceFileW(wTarget.c_str(), wTemp.c_str(), NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
                    replaced = true;
                    break;
                }
                Sleep(100);
            }
        }

        // Stage 3: Delete target then MoveFileW
        if (!replaced) {
            DeleteFileW(wTarget.c_str());
            for (int retry = 0; retry < 10; ++retry) {
                if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
                    replaced = true;
                    break;
                }
                Sleep(100);
            }
        }

        // Stage 4: CopyFileW fallback
        if (!replaced) {
            for (int retry = 0; retry < 10; ++retry) {
                if (CopyFileW(wTemp.c_str(), wTarget.c_str(), FALSE)) {
                    DeleteFileW(wTemp.c_str());
                    replaced = true;
                    break;
                }
                Sleep(150);
            }
        }

        if (!replaced) {
            DWORD err = GetLastError();
            core::Console::print("\n[ERROR] Failed to replace target file (Win32 error code: " + std::to_string(err) + ")\n", core::ConsoleColor::RED);
            DeleteFileW(wTemp.c_str());
            return false;
        }

        if (!ft.coverEmbedded && coverService) {
            coverService->processCover(ft.inputPath, ft.targetPath, saveCover);
        }
        return core::PathProvider::fileExists(ft.targetPath);
    } else {
        DeleteFileW(wTemp.c_str());
        return false;
    }
}

std::string FfmpegCommandBuilder::getOutputExtension(const std::string& outputFormat) {
    if (outputFormat.find("M4V") != std::string::npos) return "m4v";
    if (outputFormat.find("MP4") != std::string::npos) return "mp4";
    if (outputFormat.find("MKV") != std::string::npos) return "mkv";
    if (outputFormat.find("WEBM") != std::string::npos) return "webm";
    if (outputFormat.find("AVI") != std::string::npos) return "avi";
    if (outputFormat.find("MOV") != std::string::npos) return "mov";
    if (outputFormat.find("MP3") != std::string::npos) return "mp3";
    if (outputFormat.find("M4A") != std::string::npos) return "m4a";
    if (outputFormat.find("WAV") != std::string::npos) return "wav";
    if (outputFormat.find("FLAC") != std::string::npos) return "flac";
    if (outputFormat.find("OGG") != std::string::npos) return "ogg";
    if (outputFormat.find("GIF") != std::string::npos) return "gif";
    return "mp4";
}

std::string FfmpegCommandBuilder::buildOutputPath(const std::string& inputPath,
                                                 const std::string& outputBaseDir,
                                                 bool deleteOriginal,
                                                 const std::string& outputFormat,
                                                 const std::string& suffix,
                                                 const std::string& forceExt) {
    fs::path inPath = fs::u8path(inputPath);
    std::string stem = inPath.stem().u8string();
    std::string ext = forceExt.empty() ? getOutputExtension(outputFormat) : forceExt;
    std::string usedSuffix = (!deleteOriginal) ? suffix : "";
    std::string outName = stem + usedSuffix + "." + ext;

    std::string dir = outputBaseDir;
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/') {
        dir += "\\";
    }
    std::string outPath = dir + outName;

    if (!deleteOriginal) {
        std::error_code ec;
        auto absOut = fs::weakly_canonical(fs::absolute(fs::u8path(outPath), ec), ec);
        auto absIn = fs::weakly_canonical(fs::absolute(fs::u8path(inputPath), ec), ec);
        if (!ec && absOut == absIn) {
            outName = stem + (suffix.empty() ? "_converted" : suffix) + "." + ext;
            outPath = dir + outName;
        }

        int counter = 1;
        while (core::PathProvider::fileExists(outPath)) {
            outPath = dir + stem + usedSuffix + "_" + std::to_string(counter) + "." + ext;
            counter++;
        }
    }
    return outPath;
}

std::string FfmpegCommandBuilder::getVideoCodecArgs(domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat) {
    domain::AccelMode activeGpu = GpuDetector::getActiveGpuMode(mode, hybridChoice);

    if (mode == domain::AccelMode::GpuDecCpuEnc) {
        if (outputFormat.find("H.264") != std::string::npos) return "-c:v libx264";
        if (outputFormat.find("H.265") != std::string::npos || outputFormat.find("HEVC") != std::string::npos) return "-c:v libx265";
        if (outputFormat.find("AV1") != std::string::npos) return "-c:v libaom-av1";
        if (outputFormat.find("VP9") != std::string::npos) return "-c:v libvpx-vp9";
        if (outputFormat.find("MPEG4") != std::string::npos) return "-c:v mpeg4";
        return "-c:v libx264";
    }

    std::string hwEncoder;
    if (activeGpu == domain::AccelMode::Nvidia) hwEncoder = "nvenc";
    else if (activeGpu == domain::AccelMode::Amd) hwEncoder = "amf";
    else if (activeGpu == domain::AccelMode::Intel) hwEncoder = "qsv";

    if (outputFormat.find("H.264") != std::string::npos) {
        if (hwEncoder == "nvenc") return "-c:v h264_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v h264_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v h264_qsv";
        return "-c:v libx264";
    }
    if (outputFormat.find("H.265") != std::string::npos || outputFormat.find("HEVC") != std::string::npos) {
        if (hwEncoder == "nvenc") return "-c:v hevc_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v hevc_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v hevc_qsv";
        return "-c:v libx265";
    }
    if (outputFormat.find("AV1") != std::string::npos) {
        if (hwEncoder == "nvenc") return "-c:v av1_nvenc -rc:v vbr -cq:v";
        if (hwEncoder == "amf") return "-c:v av1_amf -rc:v vbr -cq:v";
        if (hwEncoder == "qsv") return "-c:v av1_qsv";
        return "-c:v libaom-av1";
    }
    if (outputFormat.find("VP9") != std::string::npos) return "-c:v libvpx-vp9";
    if (outputFormat.find("MPEG4") != std::string::npos) return "-c:v mpeg4";

    if (!hwEncoder.empty()) {
        if (hwEncoder == "nvenc") return "-c:v h264_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v h264_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v h264_qsv";
    }
    return "-c:v libx264";
}

std::string FfmpegCommandBuilder::getAudioCodecSettingName(const std::string& audioCodec) {
    if (audioCodec == "copy" || audioCodec == "original") return tr("Copy original", "Как в оригинале");
    if (audioCodec == "ask") return tr("Always ask", "Всегда спрашивать");
    if (audioCodec == "aac") return "AAC";
    if (audioCodec == "ac3") return "AC3 (Dolby Digital)";
    if (audioCodec == "eac3") return "E-AC3 (Dolby Digital+)";
    if (audioCodec == "mp3") return "MP3";
    if (audioCodec == "opus") return "Opus";
    if (audioCodec == "flac") return "FLAC";
    if (audioCodec == "pcm_s16le" || audioCodec == "pcm" || audioCodec == "wav") return "PCM (WAV)";
    return tr("Copy original", "Как в оригинале");
}

std::string FfmpegCommandBuilder::getAudioCodecArgs(const std::string& audioCodec, const std::string& audioBitrate, const std::string& outputFormat) {
    std::string choice = audioCodec;
    if (choice == "ask") choice = "copy";

    if (choice == "copy" || choice == "original") return "-c:a copy";
    if (choice == "aac") return "-c:a aac -b:a " + audioBitrate + "k";
    if (choice == "ac3") return "-c:a ac3 -b:a " + audioBitrate + "k";
    if (choice == "eac3") return "-c:a eac3 -b:a " + audioBitrate + "k";
    if (choice == "mp3") return "-c:a libmp3lame -ac 2 -b:a " + audioBitrate + "k";
    if (choice == "opus") return "-c:a libopus -b:a " + audioBitrate + "k";
    if (choice == "flac") return "-c:a flac";
    if (choice == "pcm_s16le" || choice == "pcm" || choice == "wav") return "-c:a pcm_s16le";

    if (outputFormat.find("MP3") != std::string::npos) return "-c:a libmp3lame -ac 2 -b:a " + audioBitrate + "k";
    if (outputFormat.find("Opus") != std::string::npos || outputFormat.find("WEBM") != std::string::npos) return "-c:a libopus -b:a " + audioBitrate + "k";
    if (outputFormat.find("FLAC") != std::string::npos) return "-c:a flac";
    if (outputFormat.find("WAV") != std::string::npos) return "-c:a pcm_s16le";
    return "-c:a copy";
}

std::string FfmpegCommandBuilder::getVideoQualityArgs(const std::string& crfVal, domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat, bool forceCPU) {
    if (forceCPU || mode == domain::AccelMode::GpuDecCpuEnc) {
        if (outputFormat.find("VP9") != std::string::npos) return "-crf " + crfVal + " -b:v 0";
        return "-crf " + crfVal;
    }
    domain::AccelMode activeGpu = GpuDetector::getActiveGpuMode(mode, hybridChoice);
    bool isHW = (activeGpu == domain::AccelMode::Nvidia || activeGpu == domain::AccelMode::Amd || activeGpu == domain::AccelMode::Intel);
    if (isHW) {
        if (outputFormat.find("AV1") != std::string::npos) return crfVal;
        return "-qp " + crfVal;
    }
    if (outputFormat.find("VP9") != std::string::npos) return "-crf " + crfVal + " -b:v 0";
    return "-crf " + crfVal;
}

std::string FfmpegCommandBuilder::getVideoPresetArgs(const std::string& preset, domain::AccelMode mode, domain::AccelMode hybridChoice, const std::string& outputFormat, bool forceCPU) {
    const std::string& p = preset;
    if (forceCPU || mode == domain::AccelMode::GpuDecCpuEnc) return "-preset " + p;
    domain::AccelMode activeGpu = GpuDetector::getActiveGpuMode(mode, hybridChoice);

    if (activeGpu == domain::AccelMode::Nvidia) {
        if (p == "ultrafast" || p == "superfast" || p == "veryfast") return "-preset p1";
        if (p == "faster" || p == "fast") return "-preset p4";
        if (p == "medium") return "-preset p5";
        if (p == "slow" || p == "slower" || p == "veryslow") return "-preset p7";
        return "-preset p5";
    }
    if (activeGpu == domain::AccelMode::Amd) {
        if (p == "ultrafast" || p == "superfast" || p == "veryfast" || p == "faster" || p == "fast") return "-preset speed";
        if (p == "medium") return "-preset balanced";
        if (p == "slow" || p == "slower" || p == "veryslow") return "-preset quality";
        return "-preset balanced";
    }
    if (activeGpu == domain::AccelMode::Intel) {
        if (p == "ultrafast" || p == "superfast") return "-preset veryfast";
        if (p == "veryfast" || p == "faster") return "-preset faster";
        if (p == "fast") return "-preset fast";
        if (p == "medium") return "-preset medium";
        if (p == "slow") return "-preset slow";
        if (p == "slower" || p == "veryslow") return "-preset slower";
        return "-preset medium";
    }
    if (outputFormat.find("AV1") != std::string::npos) {
        if (p == "ultrafast" || p == "superfast") return "-cpu-used 8";
        if (p == "veryfast" || p == "faster") return "-cpu-used 7";
        if (p == "fast") return "-cpu-used 6";
        if (p == "medium") return "-cpu-used 5";
        if (p == "slow") return "-cpu-used 4";
        if (p == "slower") return "-cpu-used 3";
        if (p == "veryslow") return "-cpu-used 2";
        return "-cpu-used 5";
    }
    if (outputFormat.find("VP9") != std::string::npos) {
        if (p == "ultrafast" || p == "superfast") return "-cpu-used 5";
        if (p == "veryfast" || p == "faster") return "-cpu-used 4";
        if (p == "fast") return "-cpu-used 3";
        if (p == "medium") return "-cpu-used 2";
        if (p == "slow") return "-cpu-used 1";
        if (p == "slower" || p == "veryslow") return "-cpu-used 0";
        return "-cpu-used 2";
    }
    return "-preset " + p;
}

std::string FfmpegCommandBuilder::buildVideoFilterSpec(const std::string& filePath,
                                                      const FfprobeService& probe,
                                                      const std::string& defaultResolution,
                                                      bool useAutoAlign,
                                                      bool useDownscale4K,
                                                      const std::string& extraFilter,
                                                      const std::string& overrideResolution) {
    std::vector<std::string> filters;
    domain::VideoSourceProperties vp = probe.getVideoProperties(filePath);

    if (vp.isInterlaced) {
        filters.push_back("bwdif");
    }

    if (!extraFilter.empty()) {
        filters.push_back(extraFilter);
    }

    if (useAutoAlign) {
        filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    } else if (useDownscale4K) {
        filters.push_back("scale=-2:2160");
    } else {
        std::string targetRes = overrideResolution.empty() ? defaultResolution : overrideResolution;
        if (targetRes != "original") {
            filters.push_back("scale=-2:" + targetRes);
        }
    }

    if (filters.empty()) return "";
    std::string res = "-vf \"";
    for (size_t i = 0; i < filters.size(); ++i) {
        if (i > 0) res += ",";
        res += filters[i];
    }
    res += "\"";
    return res;
}

} // namespace services
} // namespace mr
