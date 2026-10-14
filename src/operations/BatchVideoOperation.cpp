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

        // Check if we can do double processing with the next file
        bool paired = false;
        PairPrep prep;
        if (cfg.parallelBatch && (i + 1 < files.size())) {
            string nextPath = files[i + 1];
            bool videoLocked = batchVideoPref.hasPreference;
            bool audioLocked = batchAudioPref.hasPreference;
            bool subsLocked = (forceSubAction != -1) || (cfg.subtitleAction != "ask");
            bool encLocked = (forceMode != -1);

            paired = PairProcessingService::tryPrepare(inPath, nextPath, videoLocked, audioLocked, subsLocked, encLocked,
                                                       batchVideoPref, batchAudioPref, "", cfg, ctx.ffprobeService, ctx.hardware, prep);
        }

        bool hasVerdict = PairProcessingService::printVerdict(cfg.parallelBatch, paired, i + 1 < files.size());
        if (!hasVerdict) {
            cout << "\n";
        }
        Console::printColor("----------------------------------------", Console::Color::CYAN);
        Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 1) + "/" + to_string(files.size()) + ": " + fileName, Console::Color::CYAN);
        if (paired) {
            string nextFileName = fs::u8path(prep.nextPath).filename().u8string();
            Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 2) + "/" + to_string(files.size()) + ": " + nextFileName, Console::Color::CYAN);
        }
        Console::printColor("----------------------------------------", Console::Color::CYAN);

        if (paired) {
            string nextPath = prep.nextPath;
            double durA = ctx.ffprobeService.getMediaDuration(inPath);
            double durB = ctx.ffprobeService.getMediaDuration(nextPath);

            string outPathA = FfmpegCommandBuilder::buildOutputPath(inPath, cfg.outputPath, cfg.overwriteFiles, cfg.outputFormat, "_compressed");
            string outPathB = FfmpegCommandBuilder::buildOutputPath(nextPath, cfg.outputPath, cfg.overwriteFiles, cfg.outputFormat, "_compressed");

            auto targetA = FfmpegCommandBuilder::prepareTarget(outPathA, {inPath});
            auto targetB = FfmpegCommandBuilder::prepareTarget(outPathB, {nextPath});

            string videoMapArgA, audioMapArgA;
            if (batchVideoPref.hasPreference) {
                auto vTracksA = ctx.ffprobeService.getVideoTracks(inPath);
                int matchIdx = ctx.ffprobeService.matchVideoPreference(vTracksA, batchVideoPref);
                if (matchIdx == 9999) videoMapArgA = " -map 0:v";
                else if (matchIdx >= 0) videoMapArgA = " -map 0:v:" + to_string(matchIdx);
                else videoMapArgA = " -map 0:v:0";
            }
            if (batchAudioPref.hasPreference) {
                auto aTracksA = ctx.ffprobeService.getAudioTracks(inPath);
                int matchIdx = ctx.ffprobeService.matchAudioPreference(aTracksA, batchAudioPref);
                if (matchIdx == 9999) audioMapArgA = " -map 0:a";
                else if (matchIdx >= 0) audioMapArgA = " -map 0:a:" + to_string(matchIdx);
                else audioMapArgA = " -map 0:a:0";
            }
            string streamMapArgA = FfprobeService::buildStreamMapArgs(videoMapArgA, audioMapArgA);

            wstring cmdA = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
            cmdA += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inPath)) + L"\"";
            if (!streamMapArgA.empty()) cmdA += StringUtil::utf8ToWstring(streamMapArgA);
            cmdA += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdA += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdA += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdA += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec, cfg.audioBitrate, cfg.outputFormat));
            if (!cfg.keepMetadata) cmdA += L" -map_metadata -1";
            if (cfg.overwriteFiles) cmdA += L" -y";
            cmdA += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(targetA.writePath)) + L"\"";

            wstring cmdB = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
            cmdB += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(nextPath)) + L"\"";
            if (!prep.nextStreamMapArg.empty()) cmdB += StringUtil::utf8ToWstring(prep.nextStreamMapArg);
            cmdB += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdB += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdB += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, cfg.accelMode, cfg.hybridGpuChoice, cfg.outputFormat));
            cmdB += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec, cfg.audioBitrate, cfg.outputFormat));
            if (!cfg.keepMetadata) cmdB += L" -map_metadata -1";
            if (cfg.overwriteFiles) cmdB += L" -y";
            cmdB += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(targetB.writePath)) + L"\"";

            PairJob jobA, jobB;
            bool spawnedA = PairProcessingService::spawnJob(jobA, cmdA, fileName, durA);
            bool spawnedB = PairProcessingService::spawnJob(jobB, cmdB, fs::u8path(nextPath).filename().u8string(), durB);

            if (spawnedA && spawnedB) {
                bool escaped = false;
                bool pairOk = PairProcessingService::executePair(jobA, jobB, escaped);
                if (pairOk) {
                    success += 2;
                    FfmpegCommandBuilder::finalizeTarget(targetA, true, &ctx.coverService, cfg.saveCover);
                    FfmpegCommandBuilder::finalizeTarget(targetB, true, &ctx.coverService, cfg.saveCover);
                    Dialogs::handleOriginalDeletion(inPath, targetA.targetPath, targetA.isTemp, deleteOrig);
                    Dialogs::handleOriginalDeletion(nextPath, targetB.targetPath, targetB.isTemp, deleteOrig);
                    fs::path dirA = fs::u8path(targetA.targetPath).parent_path().make_preferred();
                    if (dirA.empty()) {
                        std::error_code ec;
                        dirA = fs::current_path(ec).make_preferred();
                    }
                    fs::path dirB = fs::u8path(targetB.targetPath).parent_path().make_preferred();
                    if (dirB.empty()) {
                        std::error_code ec;
                        dirB = fs::current_path(ec).make_preferred();
                    }
                    Console::printColor(TR("[OK] Videos processed and saved to:", "[OK] Видео обработаны и сохранены по пути:"), Console::Color::GREEN);
                    Console::printColor("\"" + dirA.u8string() + "\"", Console::Color::WHITE);
                    if (dirA != dirB) {
                        Console::printColor("\"" + dirB.u8string() + "\"", Console::Color::WHITE);
                    }
                    i += 2;
                    continue;
                }
                if (escaped) {
                    FfmpegCommandBuilder::finalizeTarget(targetA, false);
                    FfmpegCommandBuilder::finalizeTarget(targetB, false);
                    PairProcessingService::releaseJob(jobA);
                    PairProcessingService::releaseJob(jobB);
                    string pairLabel = fileName + " + " + fs::u8path(nextPath).filename().u8string();
                    int pauseAct = Dialogs::askBatchPauseAction(pairLabel);
                    if (pauseAct == 2) {
                        Console::printColor(TR("[INFO] Batch cancelled by user.", "[ИНФО] Пакетная обработка отменена пользователем."), Console::Color::YELLOW);
                        fail += 2;
                        break;
                    } else if (pauseAct == 3) {
                        continue;
                    } else {
                        skippedFiles.push_back(inPath);
                        skippedFiles.push_back(nextPath);
                        fail += 2;
                        i += 2;
                        continue;
                    }
                }
            }
            FfmpegCommandBuilder::finalizeTarget(targetA, false);
            FfmpegCommandBuilder::finalizeTarget(targetB, false);
            PairProcessingService::releaseJob(jobA);
            PairProcessingService::releaseJob(jobB);
            Console::printColor(TR("[INFO] Parallel processing failed, falling back to single file mode.",
                                   "[ИНФО] Параллельная обработка не удалась, переключение на одиночный режим."), Console::Color::YELLOW);
            cout << "\n";
            Console::printColor("----------------------------------------", Console::Color::CYAN);
            Console::printColor(TR("Processing video ", "Обработка видео ") + to_string(i + 1) + "/" + to_string(files.size()) + ": " + fileName, Console::Color::CYAN);
            Console::printColor("----------------------------------------", Console::Color::CYAN);
        }

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
        string outPath = FfmpegCommandBuilder::buildOutputPath(inPath, cfg.outputPath, cfg.overwriteFiles, currentFmt, "_compressed");
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

        wstring coverAttachArgs;
        string outExt = FfmpegCommandBuilder::getOutputExtension(currentFmt);
        if (cfg.saveCover && !outExt.empty()) {
            if (ctx.coverService.buildCoverAttachArgs(outExt, inPath, coverAttachArgs)) {
                cmd += coverAttachArgs;
                ft.coverEmbedded = true;
            }
        }

        if (!videoMapArg.empty()) cmd += StringUtil::utf8ToWstring(videoMapArg);
        if (!audioMapArg.empty()) cmd += StringUtil::utf8ToWstring(audioMapArg);

        string vf = FfmpegCommandBuilder::buildVideoFilterSpec(inPath, ctx.ffprobeService, cfg.outputResolution, useAutoAlign, useDownscale4K);
        if (!vf.empty()) cmd += L" " + StringUtil::utf8ToWstring(vf);

        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(effectiveMode, cfg.hybridGpuChoice, currentFmt));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
        cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec, cfg.audioBitrate, currentFmt));

        if (!cfg.customFfmpegArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(cfg.customFfmpegArgs);
        if (!cfg.keepMetadata) cmd += L" -map_metadata -1";
        if (cfg.overwriteFiles) cmd += L" -y";
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
