#pragma once
#include <string>

namespace mr {
namespace services {

class FfmpegRunner {
public:
    static bool parseFFmpegProgress(const std::string& line, std::string& timeStr, std::string& speed);
    static double timeToSeconds(const std::string& timeStr);
    static void renderProgressBar(double currentSec, double totalSec, const std::string& speed);

    // Executes FFmpeg process safely using redirected pipes without CREATE_NO_WINDOW.
    // Handles live progress parsing and ESC cancellation.
    static bool executeWithProgress(const std::wstring& cmdLine, double totalDuration, bool& outEscaped);
};

} // namespace services
} // namespace mr
