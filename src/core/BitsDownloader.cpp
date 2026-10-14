#include "BitsDownloader.hpp"
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include "Console.hpp"
#include <bits.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace mr {
namespace core {

void BitsDownloader::renderDefaultProgress(const std::string& label, double percent, const std::string& extraInfo) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int barWidth = 25;
    int pos = static_cast<int>(barWidth * percent / 100.0);

    std::string line = "\r";
    if (!label.empty()) {
        line += "[" + label + "] ";
    }
    line += "[";
    for (int i = 0; i < barWidth; ++i) {
        line += (i < pos) ? '=' : (i == pos ? '>' : ' ');
    }
    line += "] ";

    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%3.0f%%", percent);
    line += pctBuf;

    if (!extraInfo.empty()) {
        line += " " + extraInfo;
    }
    line += "        ";
    std::cout << line << std::flush;
}

bool BitsDownloader::download(const std::string& url,
                             const std::string& destFile,
                             const std::string& label,
                             ProgressCallback onProgress) {
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(initHr);

    IBackgroundCopyManager* mgr = nullptr;
    IBackgroundCopyJob* job = nullptr;

    auto cleanup = [&]() {
        if (job) { job->Release(); job = nullptr; }
        if (mgr) { mgr->Release(); mgr = nullptr; }
        if (needUninit) CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(BackgroundCopyManager), nullptr, CLSCTX_ALL,
                                  __uuidof(IBackgroundCopyManager), reinterpret_cast<void**>(&mgr));
    if (FAILED(hr) || !mgr) {
        Console::print("[ERROR] Windows transfer service (BITS) is unavailable on this system!\n"
                       "[ОШИБКА] Служба передачи данных Windows (BITS) недоступна на этой системе!\n",
                       ConsoleColor::RED);
        Console::print("[INFO] Enable it in Services, or download FFmpeg manually.\n"
                       "[ИНФО] Включите её в службах или скачайте FFmpeg вручную.\n",
                       ConsoleColor::YELLOW);
        cleanup();
        return false;
    }

    GUID jobId = {};
    hr = mgr->CreateJob(L"MR CLI for FFmpeg component", BG_JOB_TYPE_DOWNLOAD, &jobId, &job);
    if (FAILED(hr) || !job) {
        cleanup();
        return false;
    }

    const std::wstring wTmp = StringUtil::utf8ToWstring(destFile + ".tmp");
    hr = job->AddFile(StringUtil::utf8ToWstring(url).c_str(), wTmp.c_str());
    if (FAILED(hr)) {
        job->Cancel();
        cleanup();
        return false;
    }

    job->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
    job->SetNotifyFlags(BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR);
    job->SetMinimumRetryDelay(3);
    job->SetNoProgressTimeout(60);

    hr = job->Resume();
    if (FAILED(hr)) {
        job->Cancel();
        cleanup();
        return false;
    }

    const ULONGLONG startTick = GetTickCount64();
    const ULONGLONG maxWaitMs = 30ULL * 60ULL * 1000ULL;
    bool transferred = false;

    while (GetTickCount64() - startTick < maxWaitMs) {
        Sleep(400);

        BG_JOB_STATE state = BG_JOB_STATE_QUEUED;
        if (FAILED(job->GetState(&state))) break;

        if (state == BG_JOB_STATE_TRANSFERRED) {
            transferred = true;
            break;
        }
        if (state == BG_JOB_STATE_ERROR || state == BG_JOB_STATE_CANCELLED) {
            break;
        }

        BG_JOB_PROGRESS prog = {};
        if (SUCCEEDED(job->GetProgress(&prog)) && prog.BytesTotal > 0) {
            double pct = static_cast<double>(prog.BytesTransferred) * 100.0 / static_cast<double>(prog.BytesTotal);
            char info[64];
            snprintf(info, sizeof(info), "(%.1f / %.1f MB)",
                     static_cast<double>(prog.BytesTransferred) / 1048576.0,
                     static_cast<double>(prog.BytesTotal) / 1048576.0);
            if (onProgress) {
                onProgress(pct, info);
            } else {
                renderDefaultProgress(label, pct, info);
            }
        }
    }

    if (transferred) {
        job->Complete();
        const std::wstring wDst = StringUtil::utf8ToWstring(destFile);
        MoveFileExW(wTmp.c_str(), wDst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    } else {
        job->Cancel();
    }

    cleanup();
    std::cout << "\n";

    if (!transferred) return false;

    std::error_code ec;
    const uintmax_t sz = fs::file_size(fs::u8path(destFile), ec);
    return !ec && sz > 1000;
}

} // namespace core
} // namespace mr
