#include "PathProvider.hpp"
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include <shlobj.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace mr {
namespace core {

std::string PathProvider::getDocumentsPath() {
    wchar_t docPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
        return StringUtil::wstringToUtf8(docPath);
    }
    return "";
}

std::string PathProvider::getDefaultAppBaseDir() {
    std::string docs = getDocumentsPath();
    if (!docs.empty()) {
        if (docs.back() != '\\' && docs.back() != '/') {
            docs += "\\";
        }
        return docs + "MR-CLI-FOR-FFMPEG\\";
    }
    return "C:\\MR-CLI-FOR-FFMPEG\\";
}

std::string PathProvider::getDefaultConfigDir() {
    return getDefaultAppBaseDir() + "configs\\";
}

std::string PathProvider::getDefaultOutputDir() {
    return getDefaultAppBaseDir() + "output\\";
}

std::string PathProvider::getPeerAppConfigDir() {
    std::string docs = getDocumentsPath();
    std::string peerDir;
    if (!docs.empty()) {
        if (docs.back() != '\\' && docs.back() != '/') {
            docs += "\\";
        }
        peerDir = docs + "MR-CLI-FOR-YT-DLP\\configs\\";
    }
    if (peerDir.empty() || !fileExists(peerDir + "ffmpeg.exe")) {
        std::string fallback = "C:\\MR-CLI-FOR-YT-DLP\\configs\\";
        if (fileExists(fallback + "ffmpeg.exe")) {
            return fallback;
        }
    }
    return peerDir;
}

bool PathProvider::fileExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec);
}

bool PathProvider::dirExists(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_directory(fs::u8path(path), ec);
}

bool PathProvider::ensureDirExists(const std::string& path) {
    if (path.empty()) return false;
    if (dirExists(path)) return true;
    std::error_code ec;
    return fs::create_directories(fs::u8path(path), ec) || dirExists(path);
}

std::wstring PathProvider::getShortPathName(const std::wstring& longPath) {
    if (longPath.empty()) return longPath;
    DWORD size = GetShortPathNameW(longPath.c_str(), NULL, 0);
    if (size == 0) return longPath;
    std::wstring shortPath(size, 0);
    DWORD result = GetShortPathNameW(longPath.c_str(), &shortPath[0], size);
    if (result == 0 || result >= size) return longPath;
    shortPath.resize(result);
    return shortPath;
}

std::string PathProvider::getSafeFFmpegPath(const std::string& utf8Path) {
    std::wstring wPath = StringUtil::utf8ToWstring(utf8Path);
    std::wstring shortPath = getShortPathName(wPath);
    return StringUtil::wstringToUtf8(shortPath);
}

} // namespace core
} // namespace mr
