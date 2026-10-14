#pragma once
#include <string>
#include <functional>

namespace mr {
namespace core {

class BitsDownloader {
public:
    using ProgressCallback = std::function<void(double percent, const std::string& extraInfo)>;

    // Downloads file using native Windows BITS COM service.
    // AGENTS.md 4.5: WinINet and curl.exe are strictly forbidden to prevent AV heuristics.
    static bool download(const std::string& url,
                         const std::string& destFile,
                         const std::string& label = "",
                         ProgressCallback onProgress = nullptr);

    static void renderDefaultProgress(const std::string& label, double percent, const std::string& extraInfo);
};

} // namespace core
} // namespace mr
