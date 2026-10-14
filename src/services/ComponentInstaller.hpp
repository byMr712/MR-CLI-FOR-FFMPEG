#pragma once
#include <string>

namespace mr {
namespace services {

class ComponentInstaller {
public:
    explicit ComponentInstaller(const std::string& configDir = "");

    void setConfigDir(const std::string& dir) { m_configDir = dir; }
    void setBaseDir(const std::string& dir) { setConfigDir(dir); }
    const std::string& getConfigDir() const { return m_configDir; }

    const std::string& getFfmpegPath() const { return m_ffmpegPath; }
    const std::string& getFfprobePath() const { return m_ffprobePath; }
    bool isFfmpegFound() const { return m_ffmpegFound; }
    bool isFfprobeFound() const { return m_ffprobeFound; }

    bool checkDependencies();
    bool checkOrInstallComponents() { return checkDependencies(); }
    bool updateComponents();
    bool checkAndCopyFromPeer();

    static void organizeExtractedTool(const std::string& targetExe, const std::string& targetDir);

private:
    bool downloadAndInstall();

    std::string m_configDir;
    std::string m_ffmpegPath;
    std::string m_ffprobePath;
    bool m_ffmpegFound = false;
    bool m_ffprobeFound = false;
};

} // namespace services
} // namespace mr
