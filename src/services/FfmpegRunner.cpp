#include "FfmpegRunner.hpp"
#include "LocalizationService.hpp"
#include "../core/WinLibs.hpp"
#include "../core/Console.hpp"
#include <conio.h>
#include <iostream>

namespace mr {
namespace services {

bool FfmpegRunner::parseFFmpegProgress(const std::string& line, std::string& timeStr, std::string& speed) {
    timeStr.clear();
    speed.clear();

    size_t tPos = line.find("time=");
    if (tPos == std::string::npos) return false;

    size_t tEnd = line.find(' ', tPos + 5);
    if (tEnd == std::string::npos) tEnd = line.size();
    timeStr = line.substr(tPos + 5, tEnd - (tPos + 5));

    size_t sPos = line.find("speed=");
    if (sPos != std::string::npos) {
        size_t sEnd = line.find(' ', sPos + 6);
        if (sEnd == std::string::npos) sEnd = line.size();
        speed = line.substr(sPos + 6, sEnd - (sPos + 6));
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
        if (percent > 100) percent = 100;
    }
    const int barWidth = 38;
    int pos = static_cast<int>(barWidth * percent / 100.0);
    std::string line = "\r[";
    for (int i = 0; i < barWidth; ++i) {
        line += (i < pos) ? '=' : (i == pos ? '>' : ' ');
    }

    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%3.0f%%", percent);
    line += "] ";
    line += pctBuf;

    if (!speed.empty()) {
        line += "  " + tr("Speed: ", "Скорость: ") + speed;
    }

    int cm = static_cast<int>(currentSec / 60);
    int cs = static_cast<int>(currentSec) % 60;
    int tm = static_cast<int>(totalSec / 60);
    int ts = static_cast<int>(totalSec) % 60;
    char timeBuf[64];
    snprintf(timeBuf, sizeof(timeBuf), "  [%02d:%02d/%02d:%02d]", cm, cs, tm, ts);
    line += timeBuf;
    line += "            ";
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
    bool progressActive = false;
    char buffer[4096];
    DWORD bytesRead = 0;
    ULONGLONG lastRenderTime = 0;

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
                    std::string timeStr, speed;
                    if (parseFFmpegProgress(line, timeStr, speed)) {
                        double cur = timeToSeconds(timeStr);
                        ULONGLONG now = GetTickCount64();
                        if (now - lastRenderTime >= 1000 || cur >= totalDuration) {
                            renderProgressBar(cur, totalDuration, speed);
                            lastRenderTime = now;
                            progressActive = true;
                        }
                    } else if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
                        if (line.find("encoder") == std::string::npos) {
                            if (progressActive) {
                                std::cout << std::endl;
                                progressActive = false;
                            }
                            core::Console::print(tr("[ERROR] ", "[ОШИБКА] ") + line + "\n", core::ConsoleColor::RED);
                        }
                    }
                    line.clear();
                }
            } else {
                line += c;
            }
        }
    }

    if (progressActive) {
        std::cout << std::endl;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

} // namespace services
} // namespace mr
