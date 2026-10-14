#pragma once
#include <string>
#include <filesystem>
#include "../domain/AppConfig.hpp"

namespace mr {
namespace services {

class ConfigService {
public:
    explicit ConfigService(const std::string& configDir = "");

    void setConfigDir(const std::string& configDir) { m_configDir = configDir; }
    const std::string& getConfigDir() const { return m_configDir; }

    domain::AppConfig& getConfig() { return m_config; }
    const domain::AppConfig& getConfig() const { return m_config; }
    void setConfig(const domain::AppConfig& cfg) { m_config = cfg; }

    bool load();
    bool save();

    bool loadConfig(domain::AppConfig& cfg, const std::string& path = "") {
        if (!path.empty()) {
            std::filesystem::path p = std::filesystem::u8path(path);
            m_configDir = p.parent_path().u8string() + "\\";
        }
        bool ok = load();
        cfg = m_config;
        return ok;
    }

    bool saveConfig(const domain::AppConfig& cfg, const std::string& path = "") {
        m_config = cfg;
        if (!path.empty()) {
            std::filesystem::path p = std::filesystem::u8path(path);
            m_configDir = p.parent_path().u8string() + "\\";
        }
        return save();
    }

    bool isLoaded() const { return m_isLoaded; }

private:
    std::string m_configDir;
    domain::AppConfig m_config;
    bool m_isLoaded = false;
};

} // namespace services
} // namespace mr
