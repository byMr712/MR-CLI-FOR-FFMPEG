#include "FfmpegRunner.hpp"
#include "LocalizationService.hpp"
#include "../core/WinLibs.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include <conio.h>
#include <iostream>

namespace mr {
namespace services {

bool FfmpegRunner::parseFFmpegProgress(const std::string& line, std::string& timeStr, std::string& speed) {
    timeStr.clear();
    speed.clear();

    size_t tPos = line.find("time=");
    if (tPos == std::string::npos) return false;

    size_t valStart = tPos + 5;
    while (valStart < line.size() && line[valStart] == ' ') valStart++;
    size_t tEnd = line.find(' ', valStart);
    if (tEnd == std::string::npos) tEnd = line.size();
    timeStr = line.substr(valStart, tEnd - valStart);

    size_t sPos = line.find("speed=");
    if (sPos != std::string::npos) {
        size_t sValStart = sPos + 6;
        while (sValStart < line.size() && line[sValStart] == ' ') sValStart++;
        size_t sEnd = line.find(' ', sValStart);
        if (sEnd == std::string::npos) sEnd = line.size();
        speed = line.substr(sValStart, sEnd - sValStart);
    }

    return !timeStr.empty();
}

double FfmpegRunner::timeToSeconds(const std::string& timeStr) {
    int h = 0, m = 0;
    double s = 0;
    if (sscanf_s(timeStr.c_str(), "%d:%d:%lf", &h, &m, &s) >= 3) {
        return h * 3600.0 + m * 60.0 + s;
    }
    return 0;
}

void FfmpegRunner::renderProgressBar(double currentSec, double totalSec, const std::string& speed) {
    double percent = 0;
    if (totalSec > 0) {
        percent = (currentSec / totalSec) * 100.0;
        if (percent > 100.0) percent = 100.0;
        if (percent < 0.0) percent = 0.0;
    }

    int conWidth = core::Console::getConsoleWidth();
    if (conWidth < 40) conWidth = 80;
    int maxCols = conWidth - 2;
    if (maxCols > 78) maxCols = 78;

    auto formatDuration = [](double sec, bool showHours) -> std::string {
        if (sec < 0) sec = 0;
        int total = static_cast<int>(sec + 0.5);
        int h = total / 3600;
        int m = (total % 3600) / 60;
        int s = total % 60;
        char buf[32];
        if (showHours) {
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
        }
        return buf;
    };

    bool showHours = (totalSec >= 3600.0 || currentSec >= 3600.0);
    std::string timePart = "  [" + formatDuration(currentSec, showHours) + "/" + formatDuration(totalSec, showHours) + "]";

    std::string speedPart;
    if (!speed.empty() && speed != "N/A") {
        speedPart = "  " + tr("Speed: ", "Скорость: ") + speed;
    }

    char pctBuf[16];
    snprintf(pctBuf, sizeof(pctBuf), "%3.0f%%", percent);
    std::string pctPart = "] " + std::string(pctBuf);

    size_t nonBarWidth = 1 + core::StringUtil::utf8Length(pctPart)
                           + core::StringUtil::utf8Length(speedPart)
                           + core::StringUtil::utf8Length(timePart);

    int barWidth = static_cast<int>(maxCols) - static_cast<int>(nonBarWidth);
    if (barWidth < 10) barWidth = 10;
    if (barWidth > 26) barWidth = 26;

    int pos = static_cast<int>(barWidth * percent / 100.0);
    if (pos > barWidth) pos = barWidth;

    std::string line = "\r[";
    for (int i = 0; i < barWidth; ++i) {
        line += (i < pos) ? '=' : (i == pos ? '>' : ' ');
    }
    line += pctPart;
    line += speedPart;
    line += timePart;
    line += "\033[K";

    std::cout << line << std::flush;
}

bool FfmpegRunner::executeWithProgress(const std::wstring& cmdLine, double totalDuration, bool& outEscaped) {
    if (std::cout.fail() || std::cout.bad()) {
        std::cout.clear();
    }
    outEscaped = false;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        core::Console::print(tr("[ERROR] Failed to create process pipe!", "[ОШИБКА] Не удалось создать канал процесса!"), core::ConsoleColor::RED);
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    HANDLE hNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    si.hStdInput = (hNul != INVALID_HANDLE_VALUE) ? hNul : NULL;

    PROCESS_INFORMATION pi = {};
    std::wstring mutableCmd = cmdLine;
    if (mutableCmd.find(L"-nostdin") == std::wstring::npos) {
        size_t firstSpace = mutableCmd.find(L' ');
        if (firstSpace != std::wstring::npos) {
            mutableCmd.insert(firstSpace, L" -nostdin");
        }
    }

    BOOL success = CreateProcessW(
        NULL, &mutableCmd[0], NULL, NULL,
        TRUE, 0, NULL, NULL, &si, &pi
    );
    CloseHandle(hWritePipe);
    if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);

    if (!success) {
        CloseHandle(hReadPipe);
        core::Console::print(tr("[ERROR] Failed to launch FFmpeg!", "[ОШИБКА] Не удалось запустить FFmpeg!"), core::ConsoleColor::RED);
        return false;
    }

    std::string line;
    std::string lastGoodSpeed;
    bool progressActive = false;
    char buffer[4096];
    DWORD bytesRead = 0;
    ULONGLONG lastRenderTime = 0;

    auto processLine = [&](const std::string& l) {
        std::string timeStr, speed;
        if (parseFFmpegProgress(l, timeStr, speed)) {
            double cur = timeToSeconds(timeStr);
            if (!speed.empty() && speed != "N/A") {
                lastGoodSpeed = speed;
            }
            ULONGLONG now = GetTickCount64();
            if (now - lastRenderTime >= 500 || cur >= totalDuration) {
                renderProgressBar(cur, totalDuration, lastGoodSpeed);
                lastRenderTime = now;
                progressActive = true;
            }
        } else if (l.find("Error") != std::string::npos || l.find("error") != std::string::npos) {
            if (l.find("encoder") == std::string::npos &&
                l.find("past duration") == std::string::npos) {
                if (progressActive) {
                    std::cout << std::endl;
                    progressActive = false;
                }
                core::Console::print(tr("[ERROR] ", "[ОШИБКА] ") + l + "\n", core::ConsoleColor::RED);
            }
        }
    };

    while (true) {
        if (core::Console::isEscapePressed()) {
            if (progressActive) {
                std::cout << std::endl;
                progressActive = false;
            }
            outEscaped = true;
            TerminateProcess(pi.hProcess, 1);
            break;
        }

        DWORD avail = 0;
        if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) || avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 25) == WAIT_OBJECT_0) {
                PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL);
                if (avail == 0) break;
            }
            continue;
        }

        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        for (DWORD i = 0; i < bytesRead; ++i) {
            char c = buffer[i];
            if (c == '\r' || c == '\n') {
                if (!line.empty()) {
                    processLine(line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
    }

    if (!line.empty()) {
        processLine(line);
        line.clear();
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    if (exitCode == 0 && totalDuration > 0) {
        renderProgressBar(totalDuration, totalDuration, lastGoodSpeed);
        progressActive = true;
    }

    if (progressActive) {
        std::cout << std::endl;
    }

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

} // namespace services
} // namespace mr
