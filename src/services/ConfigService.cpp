#include "ConfigService.hpp"
#include "LocalizationService.hpp"
#include "../core/PathProvider.hpp"
#include <fstream>

namespace mr {
namespace services {

ConfigService::ConfigService(const std::string& configDir)
    : m_configDir(configDir) {
    if (m_configDir.empty()) {
        m_configDir = core::PathProvider::getDefaultConfigDir();
    }
}

bool ConfigService::load() {
    std::string configPath = m_configDir;
    if (!configPath.empty() && configPath.back() != '\\' && configPath.back() != '/') {
        configPath += "\\";
    }
    configPath += "mr-config.txt";

    if (!core::PathProvider::fileExists(configPath)) {
        return false;
    }

    std::ifstream f(configPath);
    if (!f.is_open()) {
        return false;
    }

    m_isLoaded = true;
    std::string l;
    while (std::getline(f, l)) {
        if (l.length() >= 3 &&
            static_cast<unsigned char>(l[0]) == 0xEF &&
            static_cast<unsigned char>(l[1]) == 0xBB &&
            static_cast<unsigned char>(l[2]) == 0xBF) {
            l = l.substr(3);
        }
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) {
            l.pop_back();
        }

        if (l.find("OUTPUT_PATH=") == 0) m_config.outputPath = l.substr(12);
        else if (l.find("OUTPUT_FORMAT=") == 0) m_config.outputFormat = l.substr(14);
        else if (l.find("OUTPUT_RESOLUTION=") == 0) m_config.outputResolution = l.substr(18);
        else if (l.find("OUTPUT_FPS=") == 0) m_config.outputFps = l.substr(11);
        else if (l.find("AUDIO_BITRATE=") == 0) m_config.audioBitrate = l.substr(14);
        else if (l.find("VIDEO_BITRATE=") == 0) m_config.videoBitrate = l.substr(14);
        else if (l.find("CRF_VALUE=") == 0) m_config.crfValue = l.substr(10);
        else if (l.find("PRESET=") == 0) m_config.preset = l.substr(7);
        else if (l.find("OVERWRITE_FILES=") == 0) m_config.overwriteFiles = (l.substr(16) == "true");
        else if (l.find("KEEP_METADATA=") == 0) m_config.keepMetadata = (l.substr(14) == "true");
        else if (l.find("VIDEO_CODEC_ASK=") == 0) m_config.videoCodecAsk = (l.substr(16) == "true");
        else if (l.find("AUDIO_CODEC=") == 0) m_config.audioCodec = l.substr(12);
        else if (l.find("AUDIO_CODEC_ASK=") == 0) {
            if (l.substr(16) == "true") m_config.audioCodec = "ask";
            else m_config.audioCodec = "copy";
        }
        else if (l.find("SUBTITLE_ACTION=") == 0) m_config.subtitleAction = l.substr(16);
        else if (l.find("DELETE_ORIGINAL=") == 0) m_config.deleteOriginal = l.substr(16);
        else if (l.find("LANGUAGE=") == 0) {
            m_config.language = (l.substr(9) == "ru") ? domain::Language::Russian : domain::Language::English;
            LocalizationService::instance().setLanguage(m_config.language);
        }
        else if (l.find("SAVE_COVER=") == 0) m_config.saveCover = (l.substr(11) == "true");
        else if (l.find("PARALLEL_BATCH=") == 0) m_config.parallelBatch = (l.substr(15) == "true");
        else if (l.find("ACCELERATION_MODE=") == 0) {
            try {
                m_config.accelMode = static_cast<domain::AccelMode>(std::stoi(l.substr(18)));
            } catch (...) {
                m_config.accelMode = domain::AccelMode::CpuOnly;
            }
        }
        else if (l.find("HYBRID_GPU_CHOICE=") == 0) {
            try {
                m_config.hybridGpuChoice = static_cast<domain::AccelMode>(std::stoi(l.substr(18)));
            } catch (...) {
                m_config.hybridGpuChoice = domain::AccelMode::CpuOnly;
            }
        }
    }
    f.close();
    return true;
}

bool ConfigService::save() {
    std::string configPath = m_configDir;
    if (!configPath.empty() && configPath.back() != '\\' && configPath.back() != '/') {
        configPath += "\\";
    }
    core::PathProvider::ensureDirExists(configPath);
    configPath += "mr-config.txt";

    std::ofstream f(configPath, std::ios::out | std::ios::binary);
    if (!f.is_open()) return false;

    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    f.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    f << "OUTPUT_PATH=" << m_config.outputPath << "\n"
      << "OUTPUT_FORMAT=" << m_config.outputFormat << "\n"
      << "OUTPUT_RESOLUTION=" << m_config.outputResolution << "\n"
      << "OUTPUT_FPS=" << m_config.outputFps << "\n"
      << "AUDIO_BITRATE=" << m_config.audioBitrate << "\n"
      << "VIDEO_BITRATE=" << m_config.videoBitrate << "\n"
      << "CRF_VALUE=" << m_config.crfValue << "\n"
      << "PRESET=" << m_config.preset << "\n"
      << "OVERWRITE_FILES=" << (m_config.overwriteFiles ? "true" : "false") << "\n"
      << "KEEP_METADATA=" << (m_config.keepMetadata ? "true" : "false") << "\n"
      << "VIDEO_CODEC_ASK=" << (m_config.videoCodecAsk ? "true" : "false") << "\n"
      << "AUDIO_CODEC=" << m_config.audioCodec << "\n"
      << "SUBTITLE_ACTION=" << m_config.subtitleAction << "\n"
      << "DELETE_ORIGINAL=" << m_config.deleteOriginal << "\n"
      << "PARALLEL_BATCH=" << (m_config.parallelBatch ? "true" : "false") << "\n"
      << "LANGUAGE=" << (m_config.language == domain::Language::Russian ? "ru" : "en") << "\n"
      << "SAVE_COVER=" << (m_config.saveCover ? "true" : "false") << "\n"
      << "ACCELERATION_MODE=" << static_cast<int>(m_config.accelMode) << "\n"
      << "HYBRID_GPU_CHOICE=" << static_cast<int>(m_config.hybridGpuChoice) << "\n";
    f.close();
    return true;
}

} // namespace services
} // namespace mr
