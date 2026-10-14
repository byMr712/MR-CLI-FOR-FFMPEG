#pragma once
#include <string>

namespace mr {
namespace core {

class PathProvider {
public:
    static std::string getDocumentsPath();
    static std::string getDefaultAppBaseDir();
    static std::string getDefaultConfigDir();
    static std::string getDefaultOutputDir();
    static std::string getPeerAppConfigDir();

    static bool fileExists(const std::string& path);
    static bool dirExists(const std::string& path);
    static bool ensureDirExists(const std::string& path);
    static bool createDirRecursive(const std::string& path) { return ensureDirExists(path); }

    static std::wstring getShortPathName(const std::wstring& longPath);
    static std::string getSafeFFmpegPath(const std::string& utf8Path);
};

} // namespace core
} // namespace mr
