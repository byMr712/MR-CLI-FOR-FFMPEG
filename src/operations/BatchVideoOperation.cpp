#include "BatchVideoOperation.hpp"
#include "../app/AppContext.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/ShellDialogs.hpp"
#include "../services/FfmpegCommandBuilder.hpp"
#include "../services/FfmpegRunner.hpp"
#include "../services/PairProcessingService.hpp"
#include "../services/EncodingProblemDetector.hpp"
#include "../services/LocalizationService.hpp"
#include "../ui/Dialogs.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <conio.h>

namespace fs = std::filesystem;
using namespace std;
using namespace mr::core;
using namespace mr::domain;
using namespace mr::services;
using namespace mr::ui;
using namespace mr::app;

namespace mr {
namespace operations {

static inline string TR(const string& en, const string& ru) {
    return LocalizationService::tr(en, ru);
}

string BatchVideoOperation::getName() const {
    return TR("Batch convert video", "Пакетное конвертирование видео");
}

void BatchVideoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH VIDEO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Select folder with video files...", "Выберите папку с видеофайлами...") << "\n";
    Console::flushInput();
    string folder = ShellDialogs::openFolderDialog(StringUtil::utf8ToWstring(TR("Select folder with video files", "Выберите папку с видеофайлами")).c_str());
    if (folder.empty()) {
        cout << TR("Or enter/drag-and-drop folder path manually (Enter to cancel): ",
                   "Или введите/перетащите путь к папке вручную (Enter для отмены): ");
        folder = "";
        if (!Console::inputLineWithEscape(folder) || folder.empty()) {
            Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
            Console::waitForKey();
            return;
        }
        while (!folder.empty() && (folder.front() == '\"' || folder.front() == '\'')) folder.erase(folder.begin());
        while (!folder.empty() && (folder.back() == '\"' || folder.back() == '\'')) folder.pop_back();
        if (folder.empty()) {
            Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
            Console::waitForKey();
            return;
        }
        if (!PathProvider::dirExists(folder)) {
            Console::printColor(TR("[ERROR] Directory does not exist!", "[ОШИБКА] Папка не существует!"), Console::Color::RED);
            Console::waitForKey();
            return;
        }
        if (folder.back() != '\\' && folder.back() != '/') {
            folder += '\\';
        }
    }

    vector<string> videoExts = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".ts", ".m4v"};
    vector<string> files;
    error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        string ext = entry.path().extension().u8string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& ve : videoExts) {
            if (ext == ve) { files.push_back(entry.path().u8string()); break; }
        }
    }

    if (files.empty()) {
        Console::printColor(TR("[ERROR] No video files found in the selected folder!", "[ОШИБКА] Видеофайлы в выбранной папке не найдены!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    string batchAudioCodec;
    if (!Dialogs::promptAudioCodecSettings(cfg, batchAudioCodec)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH VIDEO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ ВИДЕО"), Console::Color::CYAN);
    Console::printColor(" \"" + folder + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << TR("Settings:", "Настройки:") << "\n"
         << "  " << TR("Format: ", "Формат: ") << cfg.outputFormat << "\n"
         << "  CRF: " << cfg.crfValue << "\n"
         << "  " << TR("Preset: ", "Пресет: ") << cfg.preset << "\n"
         << "  " << TR("Program acceleration: ", "Программное ускорение: ") << ctx.hardware.getAccelerationModeName(cfg.accelMode) << "\n"
         << "  " << TR("Audio codec: ", "Аудиокодек: ") << FfmpegCommandBuilder::getAudioCodecSettingName(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec) << "\n";

    cout << "\n" << TR("Would you like to change any settings before proceeding? [Y/N]: ",
                       "Хотите изменить настройки перед началом? [Y/N]: ");
    char ch = Console::getMenuChoice();
    if (ch == 27) return;
    if (ch == 'y') {
        cout << "Y\n";
        Console::printColor(TR("[INFO] Please change settings in the Settings menu and restart batch operation.",
                               "[ИНФО] Измените настройки в меню Настроек и перезапустите пакетную операцию."), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }
    cout << "N\n";

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(true, cfg.deleteOriginal, deleteOrig)) return;

    int success = 0, fail = 0;
    vector<string> skippedFiles;
    int forceMode = -1;
    string forcedFormat = "";
    int forceSubAction = -1;

    AudioTrackPreference batchAudioPref;
    VideoTrackPreference batchVideoPref;

    AccelMode activeGpu = ctx.hardware.getActiveGpuMode(cfg.accelMode, cfg.hybridGpuChoice);
    bool isHWEncoder = (activeGpu == AccelMode::Nvidia || activeGpu == AccelMode::Amd || activeGpu == AccelMode::Intel);

    size_t i = 0;
    while (i < files.size()) {
        string inPath = files[i];
        string fileName = fs::u8path(inPath).filename().u8string();

        // Check if we can run continuous parallel conveyor
        auto canAutoProcess = [&](const string& filePath) -> bool {
            bool videoLocked = batchVideoPref.hasPreference;
            bool audioLocked = batchAudioPref.hasPreference;
            bool subsLocked = (forceSubAction != -1) || (cfg.subtitleAction != "ask");
            bool encLocked = (forceMode != -1);
            auto fp = PairProcessingService::buildFingerprint(filePath, videoLocked, audioLocked, subsLocked, encLocked, cfg, ctx.ffprobeService, ctx.hardware);
            return !fp.needsQuestion;
        };

        auto buildSlotCmd = [&](const string& filePath, const string& writePath) -> wstring {
            string videoMapArg, audioMapArg;
            if (batchVideoPref.hasPreference) {
                auto vTracks = ctx.ffprobeService.getVideoTracks(filePath);
                int matchIdx = ctx.ffprobeService.matchVideoPreference(vTracks, batchVideoPref);
                if (matchIdx == 9999) videoMapArg = " -map 0:v";
                else if (matchIdx >= 0) videoMapArg = " -map 0:v:" + to_string(matchIdx);
                else videoMapArg = " -map 0:v:0";
            }
            if (batchAudioPref.hasPreference) {
                auto aTracks = ctx.ffprobeService.getAudioTracks(filePath);
                int matchIdx = ctx.ffprobeService.matchAudioPreference(aTracks, batchAudioPref);
                if (matchIdx == 9999) audioMapArg = " -map 0:a";
                else if (matchIdx >= 0) audioMapArg = " -map 0:a:" + to_string(matchIdx);
                else audioMapArg = " -map 0:a:0";
            }
            string streamMapArg = FfprobeService::buildStreamMapArgs(videoMapArg, audioMapArg);

            wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
            cmd += L" -nostdin";
            cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(filePath)) + L"\"";
            if (!streamMapArg.empty()) cmd += StringUtil::utf8ToWstring(streamMapArg);
            cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec, cfg.audioBitrate, cfg.outputFormat));
            if (!cfg.customFfmpegArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(cfg.customFfmpegArgs);
            if (!cfg.keepMetadata) cmd += L" -map_metadata -1";
            cmd += L" -y";
            cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(writePath)) + L"\"";
            return cmd;
        };

        struct PipelineSlot {
            PairJob job;
            FFmpegTarget target;
            string inPath;
            size_t fileIdx = 0;
            bool active = false;
        };

        bool canConvey = cfg.parallelBatch && (i + 1 < files.size()) && canAutoProcess(inPath) && canAutoProcess(files[i + 1]);

        if (canConvey) {
            PairProcessingService::printVerdict(true, true, true);

            PipelineSlot slot1, slot2;
            string outPath1 = FfmpegCommandBuilder::buildOutputPath(inPath, cfg.outputPath, deleteOrig, cfg.outputFormat, "_compressed");
            slot1.target = FfmpegCommandBuilder::prepareTarget(outPath1, {inPath});
            slot1.inPath = inPath;
            slot1.fileIdx = i;
            double dur1 = ctx.ffprobeService.getMediaDuration(inPath);
            wstring cmd1 = buildSlotCmd(inPath, slot1.target.writePath);
            slot1.active = PairProcessingService::spawnJob(slot1.job, cmd1, fileName, dur1);

            string path2 = files[i + 1];
            string name2 = fs::u8path(path2).filename().u8string();
            string outPath2 = FfmpegCommandBuilder::buildOutputPath(path2, cfg.outputPath, deleteOrig, cfg.outputFormat, "_compressed");
            slot2.target = FfmpegCommandBuilder::prepareTarget(outPath2, {path2});
            slot2.inPath = path2;
            slot2.fileIdx = i + 1;
            double dur2 = ctx.ffprobeService.getMediaDuration(path2);
            wstring cmd2 = buildSlotCmd(path2, slot2.target.writePath);
            slot2.active = PairProcessingService::spawnJob(slot2.job, cmd2, name2, dur2);

            if (slot1.active && slot2.active) {
                Console::printColor("----------------------------------------", Console::Color::CYAN);
                Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 1) + "/" + to_string(files.size()) + ": " + fileName, Console::Color::CYAN);
                Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 2) + "/" + to_string(files.size()) + ": " + name2, Console::Color::CYAN);
                Console::printColor("----------------------------------------", Console::Color::CYAN);

                size_t nextFileIdx = i + 2;
                ULONGLONG lastRenderTime = 0;
                PairProcessingService::renderActiveSlots(&slot1.job, &slot2.job);
                lastRenderTime = GetTickCount64();

                bool conveyorEscaped = false;

                while (slot1.active || slot2.active) {
                    if (Console::isEscapePressed()) {
                        conveyorEscaped = true;
                        if (slot1.active && slot1.job.running) TerminateProcess(slot1.job.pi.hProcess, 1);
                        if (slot2.active && slot2.job.running) TerminateProcess(slot2.job.pi.hProcess, 1);
                        break;
                    }

                    if (slot1.active) PairProcessingService::consumeOutput(slot1.job);
                    if (slot2.active) PairProcessingService::consumeOutput(slot2.job);

                    // Check slot 1 completion
                    if (slot1.active && PairProcessingService::tryFinishJob(slot1.job)) {
                        bool ok1 = (slot1.job.exitCode == 0);
                        FfmpegCommandBuilder::finalizeTarget(slot1.target, ok1, &ctx.coverService, cfg.saveCover);
                        PairProcessingService::releaseJob(slot1.job);
                        slot1.active = false;

                        PairProcessingService::clearRenderedLines();
                        if (ok1) {
                            success++;
                            Dialogs::handleOriginalDeletion(slot1.inPath, slot1.target.targetPath, slot1.target.isTemp, deleteOrig);
                            Console::printColor(TR("[OK] Processed: ", "[OK] Обработан: ") + fs::u8path(slot1.inPath).filename().u8string(), Console::Color::GREEN);
                        } else {
                            fail++;
                            Console::printColor(TR("[ERROR] Failed: ", "[ОШИБКА] Ошибка: ") + fs::u8path(slot1.inPath).filename().u8string(), Console::Color::RED);
                        }

                        if (nextFileIdx < files.size() && canAutoProcess(files[nextFileIdx])) {
                            size_t curIdx = nextFileIdx++;
                            string curPath = files[curIdx];
                            string curName = fs::u8path(curPath).filename().u8string();
                            double curDur = ctx.ffprobeService.getMediaDuration(curPath);
                            string curOut = FfmpegCommandBuilder::buildOutputPath(curPath, cfg.outputPath, deleteOrig, cfg.outputFormat, "_compressed");
                            slot1.target = FfmpegCommandBuilder::prepareTarget(curOut, {curPath});
                            slot1.inPath = curPath;
                            slot1.fileIdx = curIdx;
                            wstring curCmd = buildSlotCmd(curPath, slot1.target.writePath);
                            if (PairProcessingService::spawnJob(slot1.job, curCmd, curName, curDur)) {
                                slot1.active = true;
                                Console::printColor("----------------------------------------", Console::Color::CYAN);
                                Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(curIdx + 1) + "/" + to_string(files.size()) + ": " + curName, Console::Color::CYAN);
                                Console::printColor("----------------------------------------", Console::Color::CYAN);
                            } else {
                                FfmpegCommandBuilder::finalizeTarget(slot1.target, false);
                                fail++;
                            }
                        }
                    }

                    // Check slot 2 completion
                    if (slot2.active && PairProcessingService::tryFinishJob(slot2.job)) {
                        bool ok2 = (slot2.job.exitCode == 0);
                        FfmpegCommandBuilder::finalizeTarget(slot2.target, ok2, &ctx.coverService, cfg.saveCover);
                        PairProcessingService::releaseJob(slot2.job);
                        slot2.active = false;

                        PairProcessingService::clearRenderedLines();
                        if (ok2) {
                            success++;
                            Dialogs::handleOriginalDeletion(slot2.inPath, slot2.target.targetPath, slot2.target.isTemp, deleteOrig);
                            Console::printColor(TR("[OK] Processed: ", "[OK] Обработан: ") + fs::u8path(slot2.inPath).filename().u8string(), Console::Color::GREEN);
                        } else {
                            fail++;
                            Console::printColor(TR("[ERROR] Failed: ", "[ОШИБКА] Ошибка: ") + fs::u8path(slot2.inPath).filename().u8string(), Console::Color::RED);
                        }

                        if (nextFileIdx < files.size() && canAutoProcess(files[nextFileIdx])) {
                            size_t curIdx = nextFileIdx++;
                            string curPath = files[curIdx];
                            string curName = fs::u8path(curPath).filename().u8string();
                            double curDur = ctx.ffprobeService.getMediaDuration(curPath);
                            string curOut = FfmpegCommandBuilder::buildOutputPath(curPath, cfg.outputPath, deleteOrig, cfg.outputFormat, "_compressed");
                            slot2.target = FfmpegCommandBuilder::prepareTarget(curOut, {curPath});
                            slot2.inPath = curPath;
                            slot2.fileIdx = curIdx;
                            wstring curCmd = buildSlotCmd(curPath, slot2.target.writePath);
                            if (PairProcessingService::spawnJob(slot2.job, curCmd, curName, curDur)) {
                                slot2.active = true;
                                Console::printColor("----------------------------------------", Console::Color::CYAN);
                                Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(curIdx + 1) + "/" + to_string(files.size()) + ": " + curName, Console::Color::CYAN);
                                Console::printColor("----------------------------------------", Console::Color::CYAN);
                            } else {
                                FfmpegCommandBuilder::finalizeTarget(slot2.target, false);
                                fail++;
                            }
                        }
                    }

                    // Render active slots at 1 Hz
                    ULONGLONG now = GetTickCount64();
                    if (now - lastRenderTime >= 1000) {
                        PairProcessingService::renderActiveSlots(slot1.active ? &slot1.job : nullptr,
                                                                 slot2.active ? &slot2.job : nullptr);
                        lastRenderTime = now;
                    }
                    Sleep(25);
                }

                if (conveyorEscaped) {
                    PairProcessingService::clearRenderedLines();
                    if (slot1.active) {
                        FfmpegCommandBuilder::finalizeTarget(slot1.target, false);
                        PairProcessingService::releaseJob(slot1.job);
                        slot1.active = false;
                    }
                    if (slot2.active) {
                        FfmpegCommandBuilder::finalizeTarget(slot2.target, false);
                        PairProcessingService::releaseJob(slot2.job);
                        slot2.active = false;
                    }
                    string pairLabel;
                    if (!slot1.inPath.empty() && !slot2.inPath.empty()) {
                        pairLabel = fs::u8path(slot1.inPath).filename().u8string() + " + " + fs::u8path(slot2.inPath).filename().u8string();
                    } else if (!slot1.inPath.empty()) {
                        pairLabel = fs::u8path(slot1.inPath).filename().u8string();
                    } else {
                        pairLabel = fs::u8path(slot2.inPath).filename().u8string();
                    }
                    int pauseAct = Dialogs::askBatchPauseAction(pairLabel);
                    if (pauseAct == 2) {
                        Console::printColor(TR("[INFO] Batch cancelled by user.", "[ИНФО] Пакетная обработка отменена пользователем."), Console::Color::YELLOW);
                        fail += static_cast<int>(files.size() - i);
                        break;
                    } else if (pauseAct == 3) {
                        size_t restartIdx = nextFileIdx;
                        if (!slot1.inPath.empty()) restartIdx = min(restartIdx, slot1.fileIdx);
                        if (!slot2.inPath.empty()) restartIdx = min(restartIdx, slot2.fileIdx);
                        i = restartIdx;
                        continue;
                    } else {
                        if (!slot1.inPath.empty()) { skippedFiles.push_back(slot1.inPath); fail++; }
                        if (!slot2.inPath.empty()) { skippedFiles.push_back(slot2.inPath); fail++; }
                        i = nextFileIdx;
                        continue;
                    }
                }

                PairProcessingService::clearRenderedLines();
                i = nextFileIdx;
                continue;
            }

            if (slot1.active) {
                FfmpegCommandBuilder::finalizeTarget(slot1.target, false);
                PairProcessingService::releaseJob(slot1.job);
            }
            if (slot2.active) {
                FfmpegCommandBuilder::finalizeTarget(slot2.target, false);
                PairProcessingService::releaseJob(slot2.job);
            }
        }

        // Single file mode (or fallback)
        bool hasVerdict = false;
        if (cfg.parallelBatch && (i + 1 < files.size())) {
            hasVerdict = PairProcessingService::printVerdict(true, false, true);
        }
        if (!hasVerdict) {
            cout << "\n";
        }
        Console::printColor("----------------------------------------", Console::Color::CYAN);
        Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 1) + "/" + to_string(files.size()) + ": " + fileName, Console::Color::CYAN);
        Console::printColor("----------------------------------------", Console::Color::CYAN);

        // Fallback or single file processing
        bool useCPU = (forceMode == 0);
        bool useHybrid = (forceMode == 2);
        bool useReverseHybrid = (forceMode == 3);
        bool useAutoAlign = (forceMode == 10);
        bool useDownscale4K = (forceMode == 11);
        string chosenFmt = (forceMode == 1) ? forcedFormat : "";

        if (isHWEncoder && forceMode == -1) {
            EncodingProblem ep = EncodingProblemDetector::detectEncodingProblem(inPath, ctx.ffprobeService, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat);
            if (ep.hasProblem) {
                EncodingDialogResult dr = Dialogs::dialogEncodingProblem(ep, true);
                if (dr.action == 0) { useCPU = true; if (dr.applyToAll) forceMode = 0; }
                else if (dr.action == 1) { if (!dr.chosenFormat.empty()) { chosenFmt = dr.chosenFormat; if (dr.applyToAll) { forceMode = 1; forcedFormat = dr.chosenFormat; } } }
                else if (dr.action == 2) { useHybrid = true; if (dr.applyToAll) forceMode = 2; }
                else if (dr.action == 3) { useReverseHybrid = true; if (dr.applyToAll) forceMode = 3; }
                else if (dr.action == 10) { useAutoAlign = true; if (dr.applyToAll) forceMode = 10; }
                else if (dr.action == 11) { useDownscale4K = true; if (dr.applyToAll) forceMode = 11; }
                else if (dr.action == 5 || dr.action == -1) {
                    skippedFiles.push_back(inPath);
                    fail++;
                    i++;
                    continue;
                }
            }
        }

        string currentFmt = chosenFmt.empty() ? cfg.outputFormat : chosenFmt;
        double duration = ctx.ffprobeService.getMediaDuration(inPath);
        string outPath = FfmpegCommandBuilder::buildOutputPath(inPath, cfg.outputPath, deleteOrig, currentFmt, "_compressed");
        auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inPath});

        string videoMapArg;
        bool applyVideoToAll = false;
        int selectedVideoIdx = -1;
        if (!batchVideoPref.hasPreference) {
            if (!Dialogs::selectVideoTrackForFile(inPath, ctx.ffprobeService, videoMapArg, true, true, &applyVideoToAll, &selectedVideoIdx)) {
                skippedFiles.push_back(inPath);
                fail++;
                i++;
                continue;
            }
            if (applyVideoToAll) {
                auto vTracks = ctx.ffprobeService.getVideoTracks(inPath);
                ctx.ffprobeService.recordVideoPreference(batchVideoPref, selectedVideoIdx, vTracks);
            }
        } else {
            auto vTracks = ctx.ffprobeService.getVideoTracks(inPath);
            int matchIdx = ctx.ffprobeService.matchVideoPreference(vTracks, batchVideoPref);
            if (matchIdx == 9999) {
                videoMapArg = " -map 0:v";
            } else if (matchIdx >= 0) {
                videoMapArg = " -map 0:v:" + to_string(matchIdx);
            } else {
                videoMapArg = " -map 0:v:0";
            }
        }

        string audioMapArg;
        bool applyAudioToAll = false;
        int selectedAudioIdx = -1;
        if (!batchAudioPref.hasPreference) {
            if (!Dialogs::selectAudioTrackForFile(inPath, ctx.ffprobeService, audioMapArg, true, true, nullptr, &selectedAudioIdx, &applyAudioToAll)) {
                skippedFiles.push_back(inPath);
                fail++;
                i++;
                continue;
            }
            if (applyAudioToAll) {
                auto aTracks = ctx.ffprobeService.getAudioTracks(inPath);
                ctx.ffprobeService.recordAudioPreference(batchAudioPref, selectedAudioIdx, false, aTracks);
            }
        } else {
            auto aTracks = ctx.ffprobeService.getAudioTracks(inPath);
            int matchIdx = ctx.ffprobeService.matchAudioPreference(aTracks, batchAudioPref);
            if (matchIdx == 9999) {
                audioMapArg = " -map 0:a";
            } else if (matchIdx >= 0) {
                audioMapArg = " -map 0:a:" + to_string(matchIdx);
            } else {
                audioMapArg = " -map 0:a:0";
            }
        }

        AccelMode effectiveMode = cfg.accelMode;
        if (useCPU) effectiveMode = AccelMode::CpuOnly;
        else if (useHybrid) effectiveMode = AccelMode::CpuDecGpuEnc;
        else if (useReverseHybrid) effectiveMode = AccelMode::GpuDecCpuEnc;

        wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
        cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inPath)) + L"\"";

        string streamMapArg = FfprobeService::buildStreamMapArgs(videoMapArg, audioMapArg);
        if (!streamMapArg.empty()) cmd += StringUtil::utf8ToWstring(streamMapArg);

        string vf = FfmpegCommandBuilder::buildVideoFilterSpec(inPath, ctx.ffprobeService, cfg.outputResolution, useAutoAlign, useDownscale4K);
        if (!vf.empty()) cmd += L" " + StringUtil::utf8ToWstring(vf);

        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(effectiveMode, cfg.hybridGpuChoice, currentFmt));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec, cfg.audioBitrate, currentFmt));

        if (!cfg.customFfmpegArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(cfg.customFfmpegArgs);
        if (!cfg.keepMetadata) cmd += L" -map_metadata -1";
        cmd += L" -y";
        cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

        bool escaped = false;
        bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);

        if (escaped) {
            int pauseAct = Dialogs::askBatchPauseAction(fileName);
            if (pauseAct == 2) {
                FfmpegCommandBuilder::finalizeTarget(ft, false);
                Console::printColor(TR("[INFO] Batch cancelled by user.", "[ИНФО] Пакетная обработка отменена пользователем."), Console::Color::YELLOW);
                break;
            } else if (pauseAct == 3) {
                continue;
            } else {
                skippedFiles.push_back(inPath);
                fail++;
                FfmpegCommandBuilder::finalizeTarget(ft, false);
                i++;
                continue;
            }
        }

        ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);
        if (ok) {
            success++;
            Dialogs::handleOriginalDeletion(inPath, outPath, ft.isTemp, deleteOrig);
            Console::printColor(TR("[OK] Processed: ", "[OK] Обработан: ") + fileName, Console::Color::GREEN);
        } else {
            fail++;
            Console::printColor(TR("[ERROR] Failed: ", "[ОШИБКА] Ошибка: ") + fileName, Console::Color::RED);
        }

        i++;
    }

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH VIDEO PROCESSING COMPLETE", " ПАКЕТНАЯ ОБРАБОТКА ВИДЕО ЗАВЕРШЕНА"), Console::Color::CYAN);
    Console::printColor(TR(" Successfully: ", " Успешно: ") + to_string(success), Console::Color::GREEN);
    if (fail > 0) Console::printColor(TR(" Failed/Skipped: ", " С ошибками/Пропущено: ") + to_string(fail), Console::Color::RED);
    Console::printColor("========================================", Console::Color::CYAN);
    Console::waitForKey();
}

} // namespace operations
} // namespace mr
