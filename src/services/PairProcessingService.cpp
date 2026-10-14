#include "PairProcessingService.hpp"
#include "FfmpegRunner.hpp"
#include "EncodingProblemDetector.hpp"
#include "FfmpegCommandBuilder.hpp"
#include "LocalizationService.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include <algorithm>
#include <conio.h>
#include <iostream>

namespace mr {
namespace services {

static int s_pairRenderedLines = 0;

bool PairProcessingService::spawnJob(domain::PairJob& job, const std::wstring& cmdLine, const std::string& fileName, double duration) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hWrite = NULL;
    if (!CreatePipe(&job.hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(job.hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    std::wstring mutableCmd = cmdLine;
    if (!CreateProcessW(NULL, &mutableCmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &job.pi)) {
        CloseHandle(job.hRead);
        job.hRead = INVALID_HANDLE_VALUE;
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hWrite);
    job.fileName = fileName;
    job.duration = duration;
    job.running = true;
    return true;
}

void PairProcessingService::consumeOutput(domain::PairJob& job) {
    if (!job.running || job.hRead == INVALID_HANDLE_VALUE) return;

    char buffer[4096];
    DWORD bytesRead = 0;
    DWORD avail = 0;
    if (PeekNamedPipe(job.hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        if (ReadFile(job.hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            for (DWORD i = 0; i < bytesRead; ++i) {
                char c = buffer[i];
                if (c == '\r' || c == '\n') {
                    if (!job.pending.empty()) {
                        std::string timeStr, spd;
                        if (FfmpegRunner::parseFFmpegProgress(job.pending, timeStr, spd)) {
                            job.currentSec = FfmpegRunner::timeToSeconds(timeStr);
                            if (!spd.empty()) job.speed = spd;
                        } else if (job.pending.find("rror") != std::string::npos &&
                                   job.pending.find("encoder") == std::string::npos) {
                            job.errorSeen = true;
                            if (job.lastError.empty()) job.lastError = job.pending;
                        }
                        job.pending.clear();
                    }
                } else {
                    job.pending += c;
                }
            }
        }
    }
}

bool PairProcessingService::tryFinishJob(domain::PairJob& job) {
    if (!job.running) return true;
    if (WaitForSingleObject(job.pi.hProcess, 0) != WAIT_OBJECT_0) return false;

    consumeOutput(job);
    DWORD left = 0;
    if (PeekNamedPipe(job.hRead, NULL, 0, NULL, &left, NULL) && left > 0) return false;

    GetExitCodeProcess(job.pi.hProcess, &job.exitCode);
    job.running = false;
    return true;
}

void PairProcessingService::releaseJob(domain::PairJob& job) {
    if (job.hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(job.hRead);
        job.hRead = INVALID_HANDLE_VALUE;
    }
    if (job.pi.hProcess) {
        CloseHandle(job.pi.hProcess);
        job.pi.hProcess = NULL;
    }
    if (job.pi.hThread) {
        CloseHandle(job.pi.hThread);
        job.pi.hThread = NULL;
    }
    job.running = false;
}

std::string PairProcessingService::timeLabel(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds + 0.5);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[24];
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string PairProcessingService::statusLine(const domain::PairJob& job, int slot) {
    int percent = 0;
    if (job.duration > 0) {
        percent = static_cast<int>(job.currentSec * 100.0 / job.duration + 0.5);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
    } else if (!job.running) {
        percent = (job.exitCode == 0) ? 100 : percent;
    }

    const int barWidth = 22;
    int filled = static_cast<int>(static_cast<long long>(barWidth) * percent / 100);
    if (filled < 0) filled = 0;
    if (filled > barWidth) filled = barWidth;

    std::string name = job.fileName;
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    const size_t maxName = 40;
    name = core::StringUtil::truncateUtf8(name, maxName);

    char head[16];
    snprintf(head, sizeof(head), "[%d] ", slot);

    std::string out = head + name + " [";
    for (int b = 0; b < barWidth; ++b) out += (b < filled) ? '=' : ' ';
    out += "] ";

    char pct[16];
    snprintf(pct, sizeof(pct), "%3d%%", percent);
    out += pct;
    if (!job.speed.empty()) out += "  " + job.speed;
    out += "  " + timeLabel(job.currentSec) + "/" + timeLabel(job.duration);

    if (!job.running) {
        if (job.exitCode == 0) {
            out += tr("  [OK]", "  [OK]");
        } else {
            out += tr("  [FAILED]", "  [ОШИБКА]");
        }
    }
    return out;
}

void PairProcessingService::renderStatus(const domain::PairJob& a, const domain::PairJob& b) {
    if (std::cout.fail() || std::cout.bad()) {
        std::cout.clear();
    }
    if (s_pairRenderedLines > 0) {
        std::cout << "\033[" << s_pairRenderedLines << "A\r\033[J";
    }
    std::cout << statusLine(a, 1) << "\n";
    std::cout << statusLine(b, 2) << "\n";
    std::cout << std::flush;
    s_pairRenderedLines = 2;
}

bool PairProcessingService::executePair(domain::PairJob& a, domain::PairJob& b, bool& outEscaped) {
    if (std::cout.fail() || std::cout.bad()) {
        std::cout.clear();
    }
    outEscaped = false;
    s_pairRenderedLines = 0;

    ULONGLONG lastRenderTime = 0;
    renderStatus(a, b);
    lastRenderTime = GetTickCount64();

    while (a.running || b.running) {
        while (_kbhit()) {
            int key = _getch();
            if (key == 0 || key == 0xE0) {
                if (_kbhit()) _getch();
            } else if (key == 27) { // ESC
                outEscaped = true;
                if (a.running) TerminateProcess(a.pi.hProcess, 1);
                if (b.running) TerminateProcess(b.pi.hProcess, 1);
                break;
            }
        }
        if (outEscaped) break;

        consumeOutput(a);
        consumeOutput(b);
        bool doneA = tryFinishJob(a);
        bool doneB = tryFinishJob(b);

        if (!doneA || !doneB) {
            ULONGLONG now = GetTickCount64();
            if (now - lastRenderTime >= 1000) {
                renderStatus(a, b);
                lastRenderTime = now;
            }
            Sleep(25);
        } else {
            break;
        }
    }

    if (!outEscaped) {
        renderStatus(a, b);
    }
    std::cout << std::endl;

    bool okA = (a.exitCode == 0);
    bool okB = (b.exitCode == 0);
    return (okA && okB);
}

domain::PairFingerprint PairProcessingService::buildFingerprint(const std::string& filePath,
                                                                bool videoPrefStored,
                                                                bool audioPrefStored,
                                                                bool subtitlesLocked,
                                                                bool encodingLocked,
                                                                const domain::AppConfig& cfg,
                                                                const FfprobeService& probe,
                                                                const HardwareInfo& hw) {
    domain::PairFingerprint fp;

    std::vector<domain::VideoTrack> vTracks = probe.getVideoTracks(filePath);
    for (size_t vi = 0; vi < vTracks.size(); ++vi) {
        if (!vTracks[vi].isCoverOrAttachedPic()) fp.realVideoStreams++;
    }
    fp.audioTracks = static_cast<int>(probe.getAudioTracks(filePath).size());

    bool isMp4Family = (cfg.outputFormat.find("MP4") != std::string::npos ||
                        cfg.outputFormat.find("MOV") != std::string::npos ||
                        cfg.outputFormat.find("M4V") != std::string::npos);
    if (isMp4Family) {
        std::vector<domain::SubtitleTrack> subTracks = probe.getSubtitleTracks(filePath);
        if (!subTracks.empty()) {
            for (const auto& st : subTracks) {
                std::string lc = st.codec;
                std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                if (lc != "mov_text") {
                    fp.complexSubs = true;
                    break;
                }
            }
        }
    }

    domain::EncodingProblem ep = EncodingProblemDetector::detect(filePath, cfg.accelMode, cfg.hybridGpuChoice, hw, cfg.outputFormat, probe);
    fp.encodingProblem = ep.hasProblem;
    fp.oddDimensions = ep.isOddDimensions;
    fp.ultraHighRes = ep.isUltraHighRes;

    domain::AccelMode activeGpu = GpuDetector::getActiveGpuMode(cfg.accelMode, cfg.hybridGpuChoice);
    bool isHW = (activeGpu == domain::AccelMode::Nvidia || activeGpu == domain::AccelMode::Amd || activeGpu == domain::AccelMode::Intel);

    if (fp.complexSubs && !subtitlesLocked) fp.needsQuestion = true;
    if (isHW && fp.encodingProblem && !encodingLocked) fp.needsQuestion = true;
    if (fp.realVideoStreams > 1 && !videoPrefStored) fp.needsQuestion = true;
    if (fp.audioTracks > 1 && !audioPrefStored) fp.needsQuestion = true;

    return fp;
}

bool PairProcessingService::tryPrepare(const std::string& curPath,
                                       const std::string& nextPath,
                                       bool videoPrefStored,
                                       bool audioPrefStored,
                                       bool subsLocked,
                                       bool encodingLocked,
                                       const domain::VideoTrackPreference& vPref,
                                       const domain::AudioTrackPreference& aPref,
                                       const std::string& nextForcedAudioMap,
                                       const domain::AppConfig& cfg,
                                       const FfprobeService& probe,
                                       const HardwareInfo& hw,
                                       domain::PairPrep& outPrep) {
    domain::PairFingerprint fpCur = buildFingerprint(curPath, videoPrefStored, audioPrefStored, subsLocked, encodingLocked, cfg, probe, hw);
    if (fpCur.needsQuestion) return false;

    domain::PairFingerprint fpNext = buildFingerprint(nextPath, videoPrefStored, audioPrefStored, subsLocked, encodingLocked, cfg, probe, hw);
    if (fpNext.needsQuestion) return false;
    if (!(fpCur == fpNext)) return false;

    bool nextResolvable = true;
    std::string nvMap;
    std::string naMap = nextForcedAudioMap;

    std::vector<domain::VideoTrack> nvT = probe.getVideoTracks(nextPath);
    std::vector<int> nvReal;
    for (size_t k = 0; k < nvT.size(); ++k) {
        if (!nvT[k].isCoverOrAttachedPic()) nvReal.push_back(static_cast<int>(k));
    }
    if (nvReal.size() <= 1 && nvT.size() > 1) {
        nvMap = " -map 0:v:" + std::to_string(nvT[nvReal.empty() ? 0 : nvReal[0]].videoIndex);
    } else if (nvReal.size() > 1) {
        int m = FfprobeService::matchVideoPreference(nvT, nvReal, vPref);
        if (m == -1) nextResolvable = false;
        else nvMap = vPref.keepAll ? " -map 0:v?" : " -map 0:v:" + std::to_string(nvT[m].videoIndex);
    }

    if (naMap.empty()) {
        std::vector<domain::AudioTrack> naT = probe.getAudioTracks(nextPath);
        if (naT.size() > 1) {
            int m = FfprobeService::matchAudioPreference(naT, aPref);
            if (m == -1) nextResolvable = false;
            else naMap = aPref.keepAll ? " -map 0:a?" : " -map 0:a:" + std::to_string(naT[m].audioIndex);
        }
    }

    if (FfmpegCommandBuilder::buildOutputPath(nextPath, cfg.outputPath, cfg.overwriteFiles, cfg.outputFormat, "_compressed") ==
        FfmpegCommandBuilder::buildOutputPath(curPath, cfg.outputPath, cfg.overwriteFiles, cfg.outputFormat, "_compressed")) {
        nextResolvable = false;
    }

    if (!nextResolvable) return false;

    outPrep.nextPath = nextPath;
    outPrep.nextStreamMapArg = FfprobeService::buildStreamMapArgs(nvMap, naMap);
    return true;
}

bool PairProcessingService::printVerdict(bool enabled, bool paired, bool hasNext) {
    if (!enabled || !hasNext) return false;
    std::cout << "\n";
    if (paired) {
        core::Console::print(tr("Next video qualifies for double processing, two videos will be processed at once:",
                                "Следующее видео подходит для двойной обработки, обрабатываются сразу два видео:"),
                             core::ConsoleColor::CYAN);
    } else {
        core::Console::print(tr("Next video does NOT qualify for double processing, only the current video is being processed, please wait.",
                                "Следующее видео НЕ подходит для двойной обработки, обрабатывается только текущее видео, ожидайте."),
                             core::ConsoleColor::YELLOW);
    }
    return true;
}

} // namespace services
} // namespace mr
