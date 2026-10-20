#include "SingleOperations.hpp"
#include "../app/AppContext.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../core/NumberFormat.hpp"
#include "../core/ShellDialogs.hpp"
#include "../core/PathProvider.hpp"
#include "../services/FfmpegCommandBuilder.hpp"
#include "../services/FfmpegRunner.hpp"
#include "../services/ExactTrimService.hpp"
#include "../services/EncodingProblemDetector.hpp"
#include "../services/LocalizationService.hpp"
#include "../ui/Dialogs.hpp"
#include "../ui/Menu.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;
using namespace std;
using namespace mr::core;
using namespace mr::domain;
using namespace mr::services;
using namespace mr::ui;
using namespace mr::app;

namespace mr {
namespace operations {

// Helper shorthand for tr()
static inline string TR(const string& en, const string& ru) {
    return LocalizationService::tr(en, ru);
}

// ----------------------------------------------------------------------------
// 1. ConvertVideoOperation
// ----------------------------------------------------------------------------
string ConvertVideoOperation::getName() const {
    return TR("Convert video", "Конвертировать видео");
}

void ConvertVideoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    string opAudioCodec;
    if (!Dialogs::promptAudioCodecSettings(cfg, opAudioCodec)) return;
    if (!Dialogs::promptVideoCodecSettings(cfg)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CONVERT VIDEO/AUDIO FORMAT", " КОНВЕРТАЦИЯ ФОРМАТА"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file (or press ESC to cancel)...", "Выберите файл (или нажмите ESC для отмены)...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл для конвертации" : L"Select file to convert");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);

    MediaProperties mp = ctx.ffprobeService.parseMediaProperties(inputFile);
    if (mp.durationSec > 0 || !mp.videoTracks.empty() || !mp.audioTracks.empty() || !mp.format.empty()) {
        Console::printColor("\n" + TR("--- File Info ---", "--- Информация о файле ---"), Console::Color::CYAN);
        cout << ctx.ffprobeService.formatMediaPropertiesDisplay(mp);
        Console::printColor("-----------------", Console::Color::CYAN);
    }

    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    bool useCPU = false;
    bool useHybrid = false;
    bool useReverseHybrid = false;
    bool useAutoAlign = false;
    bool useDownscale4K = false;
    string chosenFormat = "";
    AccelMode activeGpuForPfmt = ctx.hardware.getActiveGpuMode(cfg.accelMode, cfg.hybridGpuChoice);
    bool isHWEncoder = (activeGpuForPfmt == AccelMode::NVIDIA || activeGpuForPfmt == AccelMode::AMD || activeGpuForPfmt == AccelMode::INTEL);

    if (isHWEncoder) {
        EncodingProblem ep = EncodingProblemDetector::detectEncodingProblem(inputFile, ctx.ffprobeService);
        if (ep.hasProblem) {
            EncodingDialogResult dr = Dialogs::dialogEncodingProblem(ep, false);
            if (dr.action == 0) useCPU = true;
            else if (dr.action == 1) { if (!dr.chosenFormat.empty()) chosenFormat = dr.chosenFormat; }
            else if (dr.action == 2) useHybrid = true;
            else if (dr.action == 3) useReverseHybrid = true;
            else if (dr.action == 10) useAutoAlign = true;
            else if (dr.action == 11) useDownscale4K = true;
            else if (dr.action == 5 || dr.action == -1) {
                Console::waitForKey();
                return;
            }
        }
    }

    string subExtraArgs = "";
    string subHardsubFilter = "";
    string currentFmt = chosenFormat.empty() ? cfg.outputFormat : chosenFormat;
    bool isMp4Output = (currentFmt.find("MP4") != string::npos || currentFmt.find("MOV") != string::npos || currentFmt.find("M4V") != string::npos);

    if (isMp4Output && !mp.subtitleTracks.empty()) {
        bool hasComplexSubs = false;
        string detectedTypes = "";
        for (const auto& sub : mp.subtitleTracks) {
            string codec = sub.codec;
            transform(codec.begin(), codec.end(), codec.begin(), ::tolower);
            if (codec == "subrip") codec = "srt";
            if (codec == "ass" || codec == "ssa" || codec == "hdmv_pgs_subtitle" || codec == "dvd_subtitle") {
                hasComplexSubs = true;
                if (!detectedTypes.empty()) detectedTypes += ", ";
                detectedTypes += sub.codec;
            }
        }

        if (hasComplexSubs) {
            SubtitleDialogResult subResult = Dialogs::dialogSubtitleIncompatibility(inputFile, detectedTypes, false);
            if (subResult.action == SubtitleIncompatAction::CANCEL) {
                Console::waitForKey();
                return;
            }
            if (subResult.action == SubtitleIncompatAction::CONVERT_SRT) {
                subExtraArgs = " -c:s mov_text";
            } else if (subResult.action == SubtitleIncompatAction::HARDSUB) {
                string safeSubInput = PathProvider::getSafeFFmpegPath(inputFile);
                replace(safeSubInput.begin(), safeSubInput.end(), '\\', '/');
                size_t colonPos = safeSubInput.find(':');
                if (colonPos != string::npos && colonPos == 1) {
                    safeSubInput.insert(1, "\\");
                }
                subHardsubFilter = "subtitles='" + safeSubInput + "'";
                subExtraArgs = " -sn";
            } else if (subResult.action == SubtitleIncompatAction::DROP) {
                subExtraArgs = " -sn";
            }
        }
    }

    string videoMapArg;
    int selectedVideoIdx = -1;
    if (!Dialogs::selectVideoTrackForFile(inputFile, ctx.ffprobeService, videoMapArg, true, false, nullptr, &selectedVideoIdx)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    string audioMapArg;
    int selectedAudioIdx = -1;
    if (!Dialogs::selectAudioTrackForFile(inputFile, ctx.ffprobeService, audioMapArg, true, false, nullptr, &selectedAudioIdx, nullptr)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, currentFmt, "_converted");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    AccelMode effectiveMode = cfg.accelMode;
    if (useCPU) effectiveMode = AccelMode::CPU_ONLY;
    else if (useHybrid) effectiveMode = AccelMode::CPU_DEC_GPU_ENC;
    else if (useReverseHybrid) effectiveMode = AccelMode::GPU_DEC_CPU_ENC;

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";

    string streamMapArg = FfprobeService::buildStreamMapArgs(videoMapArg, audioMapArg);
    if (!streamMapArg.empty()) cmd += StringUtil::utf8ToWstring(streamMapArg);

    string vf = FfmpegCommandBuilder::buildVideoFilterSpec(inputFile, ctx.ffprobeService, cfg.outputResolution, useAutoAlign, useDownscale4K, subHardsubFilter);
    if (!vf.empty()) cmd += L" " + StringUtil::utf8ToWstring(vf);

    cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoCodecArgs(effectiveMode, cfg.hybridGpuChoice, currentFmt));
    cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoPresetArgs(cfg.preset, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
    cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getVideoQualityArgs(cfg.crfValue, effectiveMode, cfg.hybridGpuChoice, currentFmt, useCPU));
    cmd += L" " + StringUtil::utf8ToWstring(FfmpegCommandBuilder::getAudioCodecArgs(opAudioCodec.empty() ? cfg.audioCodec : opAudioCodec, cfg.audioBitrate, currentFmt));

    if (!cfg.customFfmpegArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(cfg.customFfmpegArgs);
    if (!subExtraArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(subExtraArgs);
    if (!cfg.keepMetadata) cmd += L" -map_metadata -1";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" CONVERTING...", " КОНВЕРТАЦИЯ..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Converted successfully!", "[OK] Конвертация успешно завершена!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Conversion failed!", "[ОШИБКА] Ошибка конвертации!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 2. TrimVideoOperation
// ----------------------------------------------------------------------------
string TrimVideoOperation::getName() const {
    return TR("Trim / Cut video", "Обрезать видео");
}

void TrimVideoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    if (!Dialogs::promptVideoCodecSettings(cfg)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" TRIM / CUT VIDEO", " ОБРЕЗКА ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file...", "Выберите файл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл для обрезки" : L"Select file to trim");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);
    if (duration > 0) {
        int m = (int)(duration / 60), s = (int)duration % 60;
        char buf[64];
        const char* fmt = (cfg.currentLang == Language::RU) ? "Длительность: %02d:%02d (%.1f сек)" : "Duration: %02d:%02d (%.1f sec)";
        snprintf(buf, sizeof(buf), fmt, m, s, duration);
        Console::printColor(string(buf), Console::Color::CYAN);
    }

    string startTime, endTime;
    cout << "\n" << TR("Start time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n", "Время начала (ЧЧ:ММ:СС или ММ:СС или секунды, ESC для отмены):\n");
    if (!Console::inputLineWithEscape(startTime, "> ")) return;
    cout << TR("End time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n", "Время окончания (ЧЧ:ММ:СС или ММ:СС или секунды, ESC для отмены):\n");
    if (!Console::inputLineWithEscape(endTime, "> ")) return;

    if (startTime.empty() || endTime.empty()) {
        Console::printColor(TR("[ERROR] Both start and end times are required!", "[ОШИБКА] Требуется указать время начала и окончания!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    double startSec = NumberFormat::parseTimeStringToSeconds(startTime);
    double endSec = NumberFormat::parseTimeStringToSeconds(endTime);
    double trimDuration = endSec - startSec;
    if (trimDuration <= 0 && duration > startSec) trimDuration = duration - startSec;
    if (trimDuration <= 0) {
        Console::printColor(TR("[ERROR] The end time must be later than the start time!", "[ОШИБКА] Время окончания должно быть позже времени начала!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    string videoMapArg;
    int selectedVideoIndex = -1;
    if (!Dialogs::selectVideoTrackForFile(inputFile, ctx.ffprobeService, videoMapArg, true, false, nullptr, &selectedVideoIndex)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    double preRoll = ExactTrimService::getTrimPreRoll(inputFile, videoMapArg, startSec, ctx.ffprobeService, ctx.installer.getFfmpegPath());
    string preRollTxt = NumberFormat::formatDot(preRoll, 3);

    string fastCopyTitle = TR("1. Fast (stream copy, no quality loss)", "1. Быстро (копирование потока, без потери качества)");
    string exactTitle    = TR("2. Exact (re-encode cut head, frame-accurate)", "2. Точно (перекодирование, точность до кадра)");

    vector<string> trimModes = {
        fastCopyTitle + (preRoll > 0.05 ? "  [+" + preRollTxt + " s pre-roll]" : ""),
        exactTitle + "  [" + NumberFormat::formatDot(trimDuration, 3) + " s]"
    };
    vector<string> trimHints = {
        TR("Copies frames as-is. Starts from preceding keyframe; may add extra time at head.",
           "Копирует кадры без перекодирования. Начинается с предшествующего ключевого кадра."),
        TR("Re-encodes video so the cut starts at the exact frame.",
           "Перекодирует видео, гарантируя начало с указанного кадра.")
    };

    int modeChoice = Menu::arrowSelect(TR("TRIM MODE", "РЕЖИМ ОБРЕЗКИ"), "", trimModes, 0, trimHints, true);
    if (modeChoice < 0) return;
    bool isExactMode = (modeChoice == 1);

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_trimmed");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -ss " + StringUtil::utf8ToWstring(startTime);
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -t " + StringUtil::utf8ToWstring(NumberFormat::formatDot(trimDuration, 3));
    string streamMapArg = FfprobeService::buildStreamMapArgs(videoMapArg, "");
    if (!streamMapArg.empty()) cmd += StringUtil::utf8ToWstring(streamMapArg);

    if (isExactMode) {
        ExactTrimCodecs etc = ExactTrimService::buildExactTrimCodecs(inputFile, selectedVideoIndex, cfg.audioCodec, ctx.ffprobeService);
        cmd += L" " + StringUtil::utf8ToWstring(etc.videoCodecArgs);
        if (!etc.pixelFormatArgs.empty()) cmd += L" " + StringUtil::utf8ToWstring(etc.pixelFormatArgs);
        cmd += L" " + StringUtil::utf8ToWstring(etc.audioCodecArgs);
    } else {
        cmd += L" -c copy";
    }

    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" TRIMMING VIDEO...", " ОБРЕЗКА ВИДЕО..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, trimDuration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        double actualDur = ctx.ffprobeService.getMediaDuration(outPath);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Video trimmed successfully!", "[OK] Видео успешно обрезано!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
        ExactTrimService::printTrimLengthReport(trimDuration, actualDur, startTime, isExactMode);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Trimming failed!", "[ОШИБКА] Ошибка обрезки!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 3. ChangeResolutionOperation
// ----------------------------------------------------------------------------
string ChangeResolutionOperation::getName() const {
    return TR("Change resolution", "Изменить разрешение");
}

void ChangeResolutionOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    if (!Dialogs::promptVideoCodecSettings(cfg)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CHANGE VIDEO RESOLUTION", " ИЗМЕНЕНИЕ РАЗРЕШЕНИЯ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file...", "Выберите файл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл" : L"Select file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    string videoMapArg;
    if (!Dialogs::selectVideoTrackForFile(inputFile, ctx.ffprobeService, videoMapArg)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    vector<string> resOpts = {
        "1. 4K UHD (3840x2160)",
        "2. 2K QHD (2560x1440)",
        "3. 1080p Full HD (1920x1080)",
        "4. 720p HD (1280x720)",
        "5. 480p SD (854x480)",
        "6. 360p (640x360)",
        "7. " + TR("Custom resolution...", "Своё разрешение...")
    };
    int sel = Menu::arrowSelect(TR("SELECT RESOLUTION", "ВЫБЕРИТЕ РАЗРЕШЕНИЕ"), "", resOpts, 2, {}, true);
    if (sel < 0) return;

    string targetScale;
    switch (sel) {
        case 0: targetScale = "3840:2160"; break;
        case 1: targetScale = "2560:1440"; break;
        case 2: targetScale = "1920:1080"; break;
        case 3: targetScale = "1280:720"; break;
        case 4: targetScale = "854:480"; break;
        case 5: targetScale = "640:360"; break;
        case 6: {
            cout << TR("Enter resolution as WIDTH:HEIGHT (e.g. 1280:720): ", "Введите разрешение как ШИРИНА:ВЫСОТА (напр. 1280:720): ");
            if (!Console::inputLineWithEscape(targetScale, "")) return;
            break;
        }
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_res");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    string streamMapArg = FfprobeService::buildStreamMapArgs(videoMapArg, "");
    if (!streamMapArg.empty()) cmd += StringUtil::utf8ToWstring(streamMapArg);
    cmd += L" -vf \"scale=" + StringUtil::utf8ToWstring(targetScale) + L":force_original_aspect_ratio=decrease,pad=" + StringUtil::utf8ToWstring(targetScale) + L":(ow-iw)/2:(oh-ih)/2\"";
    cmd += L" -c:a copy";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" RESCALING VIDEO...", " ИЗМЕНЕНИЕ РАЗРЕШЕНИЯ..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Resolution changed successfully!", "[OK] Разрешение успешно изменено!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Rescaling failed!", "[ОШИБКА] Ошибка изменения разрешения!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 4. ChangeSpeedOperation
// ----------------------------------------------------------------------------
string ChangeSpeedOperation::getName() const {
    return TR("Change speed", "Изменить скорость");
}

void ChangeSpeedOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CHANGE VIDEO SPEED", " ИЗМЕНЕНИЕ СКОРОСТИ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file...", "Выберите файл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл" : L"Select file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    vector<string> speedOpts = {
        "1. 0.5x (" + TR("Slow motion", "Замедление") + ")",
        "2. 0.75x",
        "3. 1.25x",
        "4. 1.5x",
        "5. 2.0x (" + TR("Fast motion", "Ускорение") + ")",
        "6. " + TR("Custom speed...", "Своя скорость...")
    };
    int sel = Menu::arrowSelect(TR("SELECT SPEED MULTIPLIER", "ВЫБЕРИТЕ КОЭФФИЦИЕНТ СКОРОСТИ"), "", speedOpts, 2, {}, true);
    if (sel < 0) return;

    double speed = 1.0;
    switch (sel) {
        case 0: speed = 0.5; break;
        case 1: speed = 0.75; break;
        case 2: speed = 1.25; break;
        case 3: speed = 1.5; break;
        case 4: speed = 2.0; break;
        case 5: {
            string spdStr;
            cout << TR("Enter speed factor (e.g. 1.5): ", "Введите множитель скорости (напр. 1.5): ");
            if (!Console::inputLineWithEscape(spdStr, "")) return;
            speed = NumberFormat::parseNumberLoose(spdStr);
            if (speed <= 0.05 || speed > 100.0) {
                Console::printColor(TR("[ERROR] Invalid speed factor!", "[ОШИБКА] Неверный множитель!"), Console::Color::RED);
                Console::waitForKey();
                return;
            }
            break;
        }
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    double ptsFactor = 1.0 / speed;
    string speedTag = "_speed" + NumberFormat::formatDot(speed, 2);
    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, speedTag);
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    // Audio tempo chain (atempo is restricted to 0.5-2.0 per filter instance)
    string atempoFilter;
    double remSpeed = speed;
    while (remSpeed > 2.0) {
        if (!atempoFilter.empty()) atempoFilter += ",";
        atempoFilter += "atempo=2.0";
        remSpeed /= 2.0;
    }
    while (remSpeed < 0.5) {
        if (!atempoFilter.empty()) atempoFilter += ",";
        atempoFilter += "atempo=0.5";
        remSpeed /= 0.5;
    }
    if (!atempoFilter.empty()) atempoFilter += ",";
    atempoFilter += "atempo=" + NumberFormat::formatDot(remSpeed, 4);

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -filter_complex \"[0:v]setpts=" + StringUtil::utf8ToWstring(NumberFormat::formatDot(ptsFactor, 4)) + L"*PTS[v];[0:a]" + StringUtil::utf8ToWstring(atempoFilter) + L"[a]\"";
    cmd += L" -map \"[v]\" -map \"[a]\"";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" CHANGING SPEED...", " ИЗМЕНЕНИЕ СКОРОСТИ..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    double newDuration = duration / speed;
    bool ok = FfmpegRunner::executeWithProgress(cmd, newDuration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Speed changed successfully!", "[OK] Скорость успешно изменена!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Speed change failed!", "[ОШИБКА] Ошибка изменения скорости!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 5. RotateVideoOperation
// ----------------------------------------------------------------------------
string RotateVideoOperation::getName() const {
    return TR("Rotate / Flip", "Повернуть / Отразить");
}

void RotateVideoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" ROTATE / FLIP VIDEO", " ПОВОРОТ / ОТРАЖЕНИЕ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file...", "Выберите файл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл" : L"Select file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    vector<string> rotOpts = {
        "1. " + TR("Rotate 90 degrees clockwise", "Повернуть на 90 градусов по часовой стрелке"),
        "2. " + TR("Rotate 90 degrees counter-clockwise", "Повернуть на 90 градусов против часовой стрелки"),
        "3. " + TR("Rotate 180 degrees", "Повернуть на 180 градусов"),
        "4. " + TR("Flip horizontally", "Отразить по горизонтали"),
        "5. " + TR("Flip vertically", "Отразить по вертикали")
    };
    int sel = Menu::arrowSelect(TR("SELECT ROTATION", "ВЫБЕРИТЕ ОПЕРАЦИЮ"), "", rotOpts, 0, {}, true);
    if (sel < 0) return;

    string filter;
    switch (sel) {
        case 0: filter = "transpose=1"; break;
        case 1: filter = "transpose=2"; break;
        case 2: filter = "transpose=2,transpose=2"; break;
        case 3: filter = "hflip"; break;
        case 4: filter = "vflip"; break;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_rotated");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vf \"" + StringUtil::utf8ToWstring(filter) + L"\" -c:a copy";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" ROTATING VIDEO...", " ПОВОРОТ ВИДЕО..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Video rotated successfully!", "[OK] Видео успешно повернуто!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Rotation failed!", "[ОШИБКА] Ошибка поворота!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 6. CompressVideoOperation
// ----------------------------------------------------------------------------
string CompressVideoOperation::getName() const {
    return TR("Compress video", "Сжать видео");
}

void CompressVideoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" COMPRESS VIDEO", " СЖАТИЕ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input file...", "Выберите файл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите файл для сжатия" : L"Select file to compress");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    vector<string> compOpts = {
        "1. " + TR("Compress to target file size (MB)", "Сжать до заданного размера (МБ)"),
        "2. " + TR("High compression (CRF 28, Slow preset)", "Высокое сжатие (CRF 28, Slow)"),
        "3. " + TR("Balanced compression (CRF 24, Medium preset)", "Сбалансированное (CRF 24, Medium)"),
        "4. " + TR("Light compression (CRF 20, Fast preset)", "Лёгкое сжатие (CRF 20, Fast)")
    };
    int sel = Menu::arrowSelect(TR("SELECT COMPRESSION METHOD", "ВЫБЕРИТЕ МЕТОД СЖАТИЯ"), "", compOpts, 2, {}, true);
    if (sel < 0) return;

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_compressed");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";

    if (sel == 0) {
        string targetMbStr;
        cout << TR("Enter target size in MB (e.g. 25 for Discord): ", "Введите желаемый размер в МБ (напр. 25 для Discord): ");
        if (!Console::inputLineWithEscape(targetMbStr, "")) return;
        double targetMb = NumberFormat::parseNumberLoose(targetMbStr);
        if (targetMb <= 0 || duration <= 0) {
            Console::printColor(TR("[ERROR] Invalid target size or duration!", "[ОШИБКА] Неверный размер или длительность!"), Console::Color::RED);
            Console::waitForKey();
            return;
        }
        double targetBits = targetMb * 8.0 * 1024.0 * 1024.0;
        double totalBitrateKbps = (targetBits / duration) / 1000.0;
        double audioKbps = 128.0;
        double videoKbps = totalBitrateKbps - audioKbps;
        if (videoKbps < 100.0) videoKbps = 100.0;

        cmd += L" -c:v libx264 -b:v " + StringUtil::utf8ToWstring(to_string((int)videoKbps)) + L"k -preset medium";
        cmd += L" -c:a aac -b:a 128k";
    } else {
        string crf = (sel == 1) ? "28" : (sel == 2 ? "24" : "20");
        string preset = (sel == 1) ? "slow" : (sel == 2 ? "medium" : "fast");
        cmd += L" -c:v libx264 -crf " + StringUtil::utf8ToWstring(crf) + L" -preset " + StringUtil::utf8ToWstring(preset);
        cmd += L" -c:a aac -b:a 128k";
    }

    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" COMPRESSING VIDEO...", " СЖАТИЕ ВИДЕО..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Video compressed successfully!", "[OK] Видео успешно сжато!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Compression failed!", "[ОШИБКА] Ошибка сжатия!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 7. AddWatermarkOperation
// ----------------------------------------------------------------------------
string AddWatermarkOperation::getName() const {
    return TR("Add watermark", "Добавить водяной знак");
}

void AddWatermarkOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" ADD WATERMARK", " ДОБАВЛЕНИЕ ВОДЯНОГО ЗНАКА"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Select VIDEO file...", "Выберите ВИДЕОФАЙЛ...") << "\n";
    string videoFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (videoFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    cout << "\n" << TR("Select WATERMARK image (PNG/JPG)...", "Выберите ИЗОБРАЖЕНИЕ водяного знака...") << "\n";
    string imageFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите картинку" : L"Select image file");
    if (imageFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    double duration = ctx.ffprobeService.getMediaDuration(videoFile);

    vector<string> posOpts = {
        "1. " + TR("Top Left", "Сверху слева"),
        "2. " + TR("Top Right", "Сверху справа"),
        "3. " + TR("Bottom Left", "Снизу слева"),
        "4. " + TR("Bottom Right", "Снизу справа"),
        "5. " + TR("Center", "По центру")
    };
    int sel = Menu::arrowSelect(TR("WATERMARK POSITION", "ПОЛОЖЕНИЕ ВОДЯНОГО ЗНАКА"), "", posOpts, 3, {}, true);
    if (sel < 0) return;

    string overlayPos;
    switch (sel) {
        case 0: overlayPos = "10:10"; break;
        case 1: overlayPos = "main_w-overlay_w-10:10"; break;
        case 2: overlayPos = "10:main_h-overlay_h-10"; break;
        case 3: overlayPos = "main_w-overlay_w-10:main_h-overlay_h-10"; break;
        case 4: overlayPos = "(main_w-overlay_w)/2:(main_h-overlay_h)/2"; break;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(videoFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_watermarked");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {videoFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(videoFile)) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(imageFile)) + L"\"";
    cmd += L" -filter_complex \"overlay=" + StringUtil::utf8ToWstring(overlayPos) + L"\"";
    cmd += L" -c:a copy";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" ADDING WATERMARK...", " НАЛОЖЕНИЕ ВОДЯНОГО ЗНАКА..."), Console::Color::CYAN);
    Console::printColor(" \"" + videoFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(videoFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Watermark added successfully!", "[OK] Водяной знак успешно добавлен!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Failed to add watermark!", "[ОШИБКА] Ошибка наложения водяного знака!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 8. AddSubtitlesOperation
// ----------------------------------------------------------------------------
string AddSubtitlesOperation::getName() const {
    return TR("Add subtitles", "Добавить субтитры");
}

void AddSubtitlesOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" ADD SUBTITLES", " ДОБАВЛЕНИЕ СУБТИТРОВ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    vector<string> opts = {
        "1. " + TR("Add subtitles to video (Softsub track)", "Добавить субтитры к видео (Отключаемая дорожка)"),
        "2. " + TR("Burn subtitles into video (Hardsub)", "Вшить субтитры в видео (Хардсаб / Наложение)")
    };
    int mode = Menu::arrowSelect(TR("SUBTITLE MODE", "РЕЖИМ СУБТИТРОВ"), "", opts, 0, {}, true);
    if (mode < 0) return;

    cout << "\n" << TR("Select VIDEO file...", "Выберите ВИДЕОФАЙЛ...") << "\n";
    string videoFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (videoFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    cout << "\n" << TR("Select SUBTITLE file (.srt, .ass, .vtt)...", "Выберите ФАЙЛ СУБТИТРОВ (.srt, .ass, .vtt)...") << "\n";
    string subFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите субтитры" : L"Select subtitle file");
    if (subFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    double duration = ctx.ffprobeService.getMediaDuration(videoFile);
    string outPath = FfmpegCommandBuilder::buildOutputPath(videoFile, cfg.outputPath, deleteOrig, cfg.outputFormat, (mode == 0 ? "_softsub" : "_hardsub"));
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {videoFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(videoFile)) + L"\"";

    if (mode == 0) {
        cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(subFile)) + L"\"";
        cmd += L" -c copy -c:s mov_text";
    } else {
        string safeSub = PathProvider::getSafeFFmpegPath(subFile);
        replace(safeSub.begin(), safeSub.end(), '\\', '/');
        size_t cpos = safeSub.find(':');
        if (cpos != string::npos && cpos == 1) safeSub.insert(1, "\\");
        cmd += L" -vf \"subtitles='" + StringUtil::utf8ToWstring(safeSub) + L"'\" -c:a copy";
    }

    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" PROCESSING SUBTITLES...", " ОБРАБОТКА СУБТИТРОВ..."), Console::Color::CYAN);
    Console::printColor(" \"" + videoFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(videoFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Subtitles processed successfully!", "[OK] Субтитры успешно обработаны!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Subtitle processing failed!", "[ОШИБКА] Ошибка обработки субтитров!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 9. ExtractAudioOperation
// ----------------------------------------------------------------------------
string ExtractAudioOperation::getName() const {
    return TR("Extract audio", "Извлечь аудио");
}

void ExtractAudioOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" EXTRACT AUDIO FROM VIDEO", " ИЗВЛЕЧЕНИЕ АУДИО ИЗ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select video file...", "Выберите видеофайл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл для извлечения звука" : L"Select video to extract audio from");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    string audioMapArg;
    if (!Dialogs::selectAudioTrackForFile(inputFile, ctx.ffprobeService, audioMapArg, false)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    vector<string> audioFmtOpts = {
        "1. MP3",
        "2. M4A (AAC)",
        "3. WAV",
        "4. FLAC",
        "5. OGG (Vorbis)"
    };
    int sel = Menu::arrowSelect(TR("SELECT AUDIO FORMAT", "ВЫБЕРИТЕ ФОРМАТ АУДИО"), "", audioFmtOpts, 0, {}, true);
    if (sel < 0) return;

    string ext, codecArgs;
    switch (sel) {
        case 0: ext = "mp3"; codecArgs = "-c:a libmp3lame -ac 2 -b:a " + cfg.audioBitrate + "k"; break;
        case 1: ext = "m4a"; codecArgs = "-c:a aac -b:a " + cfg.audioBitrate + "k"; break;
        case 2: ext = "wav"; codecArgs = "-c:a pcm_s16le"; break;
        case 3: ext = "flac"; codecArgs = "-c:a flac"; break;
        case 4: ext = "ogg"; codecArgs = "-c:a libvorbis -b:a " + cfg.audioBitrate + "k"; break;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_audio", ext);
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    if (!audioMapArg.empty()) cmd += StringUtil::utf8ToWstring(audioMapArg);
    cmd += L" -vn " + StringUtil::utf8ToWstring(codecArgs);
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" EXTRACTING AUDIO...", " ИЗВЛЕЧЕНИЕ АУДИО..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Audio extracted successfully!", "[OK] Аудио успешно извлечено!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Extraction failed!", "[ОШИБКА] Ошибка извлечения!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 10. MergeVideoAudioOperation
// ----------------------------------------------------------------------------
string MergeVideoAudioOperation::getName() const {
    return TR("Merge video + audio", "Объединить видео + аудио");
}

void MergeVideoAudioOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    string opAudioCodec;
    if (!Dialogs::promptAudioCodecSettings(cfg, opAudioCodec)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" MERGE VIDEO + AUDIO", " ОБЪЕДИНЕНИЕ ВИДЕО И АУДИО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Select VIDEO file...", "Выберите ВИДЕОФАЙЛ...") << "\n";
    string videoFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (videoFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    cout << "\n" << TR("Select AUDIO file...", "Выберите АУДИОФАЙЛ...") << "\n";
    string audioFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите аудиофайл" : L"Select audio file");
    if (audioFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    double duration = ctx.ffprobeService.getMediaDuration(videoFile);
    string outPath = FfmpegCommandBuilder::buildOutputPath(videoFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_merged");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {videoFile, audioFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(videoFile)) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(audioFile)) + L"\"";
    cmd += L" -c:v copy -c:a " + StringUtil::utf8ToWstring(opAudioCodec.empty() ? "aac" : opAudioCodec) + L" -shortest";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" MERGING VIDEO + AUDIO...", " ОБЪЕДИНЕНИЕ ВИДЕО И АУДИО..."), Console::Color::CYAN);
    Console::printColor(" \"" + videoFile + "\" + \"" + audioFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(videoFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Merged successfully!", "[OK] Успешно объединено!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Merge failed!", "[ОШИБКА] Ошибка объединения!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 11. StripAudioOperation
// ----------------------------------------------------------------------------
string StripAudioOperation::getName() const {
    return TR("Strip audio (mute)", "Удалить звук");
}

void StripAudioOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" STRIP AUDIO FROM VIDEO", " УДАЛЕНИЕ ЗВУКА ИЗ ВИДЕО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select video file...", "Выберите видеофайл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    bool deleteOrig = false;
    if (!Dialogs::promptDeleteOriginal(false, cfg.deleteOriginal, deleteOrig)) return;

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, deleteOrig, cfg.outputFormat, "_muted");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -c:v copy -an";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" STRIPPING AUDIO...", " УДАЛЕНИЕ ЗВУКА..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    if (ok) {
        Dialogs::handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Audio removed successfully!", "[OK] Звук успешно удален!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Failed to strip audio!", "[ОШИБКА] Ошибка удаления звука!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 12. ConcatenateFilesOperation
// ----------------------------------------------------------------------------
string ConcatenateFilesOperation::getName() const {
    return TR("Concatenate (join) files", "Склеить файлы");
}

void ConcatenateFilesOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CONCATENATE MEDIA FILES", " СКЛЕИВАНИЕ МЕДИАФАЙЛОВ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    vector<string> files;
    cout << "\n" << TR("Select files to concatenate in order (or cancel when done):", "Выбирайте файлы по порядку (нажмите Отмена для завершения выбора):") << "\n";
    while (true) {
        string f = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите следующий файл (Отмена для завершения)" : L"Select next file (Cancel to finish)");
        if (f.empty()) break;
        files.push_back(f);
        Console::printColor("  " + to_string(files.size()) + ". " + f, Console::Color::GREEN);
    }

    if (files.size() < 2) {
        Console::printColor(TR("[ERROR] At least 2 files are required!", "[ОШИБКА] Требуется минимум 2 файла!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    string listPath = cfg.outputPath + "concat_list.txt";
    ofstream lf(fs::u8path(listPath), ios::out | ios::binary);
    if (!lf.is_open()) {
        Console::printColor(TR("[ERROR] Could not create file list!", "[ОШИБКА] Не удалось создать список файлов!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    for (const auto& f : files) {
        string p = f;
        replace(p.begin(), p.end(), '\\', '/');
        lf << "file '" << p << "'\n";
    }
    lf.close();

    string outPath = FfmpegCommandBuilder::buildOutputPath(files[0], cfg.outputPath, false, cfg.outputFormat, "_joined");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, files);

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -f concat -safe 0 -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(listPath)) + L"\"";
    cmd += L" -c copy";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" JOINING FILES...", " СКЛЕИВАНИЕ ФАЙЛОВ..."), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    double totalDur = 0;
    for (const auto& f : files) totalDur += ctx.ffprobeService.getMediaDuration(f);

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, totalDur, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok, &ctx.coverService, cfg.saveCover);

    error_code ec;
    fs::remove(fs::u8path(listPath), ec);

    if (ok) {
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Files joined successfully!", "[OK] Файлы успешно склеены!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Failed to join files!", "[ОШИБКА] Ошибка склеивания файлов!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 13. CreateGifOperation
// ----------------------------------------------------------------------------
string CreateGifOperation::getName() const {
    return TR("Create GIF", "Создать GIF");
}

void CreateGifOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CREATE GIF ANIMATION", " СОЗДАНИЕ GIF АНИМАЦИИ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select input video...", "Выберите видеофайл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    string startTime = "0";
    cout << TR("Start time (seconds or HH:MM:SS, default 0): ", "Время начала (секунды или ЧЧ:ММ:СС, по умолчанию 0): ");
    Console::inputLineWithEscape(startTime, "");
    if (startTime.empty()) startTime = "0";

    string gifDur = "5";
    cout << TR("Duration in seconds (default 5): ", "Длительность в секундах (по умолчанию 5): ");
    Console::inputLineWithEscape(gifDur, "");
    if (gifDur.empty()) gifDur = "5";

    string fps = "15";
    cout << TR("FPS (frames per second, default 15): ", "FPS (кадров в секунду, по умолчанию 15): ");
    Console::inputLineWithEscape(fps, "");
    if (fps.empty()) fps = "15";

    string width = "480";
    cout << TR("Width in pixels (default 480): ", "Ширина в пикселях (по умолчанию 480): ");
    Console::inputLineWithEscape(width, "");
    if (width.empty()) width = "480";

    string outPath = FfmpegCommandBuilder::buildOutputPath(inputFile, cfg.outputPath, false, cfg.outputFormat, "_anim", "gif");
    auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inputFile});

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    cmd += L" -ss " + StringUtil::utf8ToWstring(startTime);
    cmd += L" -t " + StringUtil::utf8ToWstring(NumberFormat::sanitizeNumberArg(gifDur));
    cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -filter_complex \"[0:v]fps=" + StringUtil::utf8ToWstring(NumberFormat::sanitizeNumberArg(fps)) +
           L",scale=" + StringUtil::utf8ToWstring(NumberFormat::sanitizeNumberArg(width)) +
           L":-1:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse[out]\" -map \"[out]\"";
    cmd += L" -y";
    cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ft.writePath)) + L"\"";

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" CREATING GIF...", " СОЗДАНИЕ GIF..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    double gDur = NumberFormat::parseNumberLoose(gifDur);
    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, gDur > 0 ? gDur : duration, escaped);
    ok = FfmpegCommandBuilder::finalizeTarget(ft, ok);

    if (ok) {
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] GIF created successfully!", "[OK] GIF успешно создан!"), Console::Color::GREEN);
        Console::printColor(TR("Output: ", "Выход: ") + outPath, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Failed to create GIF!", "[ОШИБКА] Ошибка создания GIF!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 14. ExtractFramesOperation
// ----------------------------------------------------------------------------
string ExtractFramesOperation::getName() const {
    return TR("Extract frames", "Извлечь кадры");
}

void ExtractFramesOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" EXTRACT FRAMES (SCREENSHOTS)", " ИЗВЛЕЧЕНИЕ КАДРОВ (СКРИНШОТЫ)"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select video file...", "Выберите видеофайл...") << "\n";

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n" + TR("Input: ", "Вход: ") + inputFile, Console::Color::GREEN);
    double duration = ctx.ffprobeService.getMediaDuration(inputFile);

    string videoMapArg;
    if (!Dialogs::selectVideoTrackForFile(inputFile, ctx.ffprobeService, videoMapArg)) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    vector<string> modeOpts = {
        "1. " + TR("Single frame at time position", "Один кадр по времени"),
        "2. " + TR("One frame per second", "Один кадр в секунду"),
        "3. " + TR("One frame per N seconds", "Один кадр каждые N секунд"),
        "4. " + TR("Every Nth frame", "Каждый N-й кадр")
    };
    int sel = Menu::arrowSelect(TR("EXTRACTION MODE", "РЕЖИМ ИЗВЛЕЧЕНИЯ"), "", modeOpts, 0, {}, true);
    if (sel < 0) return;

    fs::path inPath = fs::u8path(inputFile);
    string framesDir = cfg.outputPath + inPath.stem().u8string() + "_frames\\";
    PathProvider::createDirRecursive(framesDir);

    wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
    if (sel == 0) {
        string timePos;
        cout << TR("Time position (HH:MM:SS or seconds): ", "Позиция по времени (ЧЧ:ММ:СС или секунды): ");
        if (!Console::inputLineWithEscape(timePos, "")) return;
        cmd += L" -ss " + StringUtil::utf8ToWstring(timePos);
        cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
        if (!videoMapArg.empty()) cmd += StringUtil::utf8ToWstring(videoMapArg);
        cmd += L" -vframes 1 -q:v 2";
        cmd += L" -y";
        cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(framesDir + "frame.png")) + L"\"";
    } else {
        cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inputFile)) + L"\"";
        if (!videoMapArg.empty()) cmd += StringUtil::utf8ToWstring(videoMapArg);
        if (sel == 1) {
            cmd += L" -vf fps=1 -q:v 2";
        } else if (sel == 2) {
            string interval;
            cout << TR("Interval in seconds: ", "Интервал в секундах: ");
            if (!Console::inputLineWithEscape(interval, "")) return;
            cmd += L" -vf \"fps=1/" + StringUtil::utf8ToWstring(NumberFormat::sanitizeNumberArg(interval)) + L"\" -q:v 2";
        } else if (sel == 3) {
            string nth;
            cout << TR("Extract every Nth frame (e.g. 30): ", "Извлекать каждый N-й кадр (напр. 30): ");
            if (!Console::inputLineWithEscape(nth, "")) return;
            cmd += L" -vf \"select=not(mod(n\\," + StringUtil::utf8ToWstring(nth) + L"))\" -vsync vfr -q:v 2";
        }
        cmd += L" -y";
        cmd += L" \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(framesDir + "frame_%04d.png")) + L"\"";
    }

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" EXTRACTING FRAMES...", " ИЗВЛЕЧЕНИЕ КАДРОВ..."), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << endl;

    bool escaped = false;
    bool ok = FfmpegRunner::executeWithProgress(cmd, duration, escaped);

    if (ok) {
        Console::printColor("\n========================================", Console::Color::GREEN);
        Console::printColor(TR("[OK] Frames extracted successfully!", "[OK] Кадры успешно извлечены!"), Console::Color::GREEN);
        Console::printColor(TR("Output folder: ", "Папка вывода: ") + framesDir, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
    } else {
        Console::printColor("\n========================================", Console::Color::RED);
        Console::printColor(TR("[ERROR] Frame extraction failed!", "[ОШИБКА] Ошибка извлечения кадров!"), Console::Color::RED);
        Console::printColor("========================================", Console::Color::RED);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 15. ShowFileInfoOperation
// ----------------------------------------------------------------------------
string ShowFileInfoOperation::getName() const {
    return TR("File information", "Информация о файле");
}

void ShowFileInfoOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" MEDIA FILE INFORMATION", " ИНФОРМАЦИЯ О МЕДИАФАЙЛЕ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Select media file...\n", "Выберите медиафайл...\n");

    string inputFile = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите медиафайл" : L"Select media file");
    if (inputFile.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" MEDIA FILE INFORMATION", " ИНФОРМАЦИЯ О МЕДИАФАЙЛЕ"), Console::Color::CYAN);
    Console::printColor(" \"" + inputFile + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    MediaProperties mp = ctx.ffprobeService.parseMediaProperties(inputFile);
    if (mp.durationSec > 0 || !mp.videoTracks.empty() || !mp.audioTracks.empty() || !mp.format.empty()) {
        Console::printColor("\n" + TR("--- Detailed Media Info ---", "--- Подробная информация о файле ---"), Console::Color::CYAN);
        cout << ctx.ffprobeService.formatMediaPropertiesDisplay(mp);
        Console::printColor("---------------------------", Console::Color::CYAN);
    } else {
        Console::printColor(TR("[WARNING] Could not retrieve media info", "[ВНИМАНИЕ] Не удалось получить информацию о медиа"), Console::Color::YELLOW);
    }
    Console::waitForKey();
}

// ----------------------------------------------------------------------------
// 16. CompareFilesOperation
// ----------------------------------------------------------------------------
string CompareFilesOperation::getName() const {
    return TR("Compare two files", "Сравнить два файла");
}

void CompareFilesOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" COMPARE TWO MEDIA FILES", " СРАВНЕНИЕ ДВУХ МЕДИАФАЙЛОВ"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Select FIRST file...", "Выберите ПЕРВЫЙ файл...") << "\n";
    string file1 = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите первый файл" : L"Select first file");
    if (file1.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }
    Console::printColor(TR("File 1: ", "Файл 1: ") + file1, Console::Color::GREEN);

    cout << "\n" << TR("Select SECOND file...", "Выберите ВТОРОЙ файл...") << "\n";
    string file2 = ShellDialogs::openFileDialogMedia(cfg.currentLang == Language::RU ? L"Выберите второй файл" : L"Select second file");
    if (file2.empty()) {
        Console::printColor(TR("[INFO] Cancelled", "[ИНФО] Отменено"), Console::Color::YELLOW);
        Console::waitForKey();
        return;
    }
    Console::printColor(TR("File 2: ", "Файл 2: ") + file2, Console::Color::GREEN);

    cout << "\n" << TR("Analyzing files...", "Анализ файлов...") << "\n";
    MediaProperties mp1 = ctx.ffprobeService.parseMediaProperties(file1);
    MediaProperties mp2 = ctx.ffprobeService.parseMediaProperties(file2);

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" COMPARISON TABLE", " ТАБЛИЦА СРАВНЕНИЯ"), Console::Color::CYAN);
    Console::printColor(" 1: \"" + file1 + "\"", Console::Color::CYAN);
    Console::printColor(" 2: \"" + file2 + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    auto utf8Len = [](const string& s) -> size_t { return StringUtil::utf8Length(s); };
    auto padUtf8 = [&](const string& s, size_t targetWidth) -> string {
        size_t len = utf8Len(s);
        if (len < targetWidth) return s + string(targetWidth - len, ' ');
        return s;
    };

    struct CompareItem {
        string label;
        string v1, v2;
        double n1 = 0, n2 = 0;
        bool hasNumbers = false;
        bool higherBetter = true;
    };

    vector<CompareItem> items;
    auto addItem = [&](const string& label, const string& v1, const string& v2, double n1 = 0, double n2 = 0, bool higherBetter = true, bool hasNum = false) {
        CompareItem item;
        item.label = label;
        item.v1 = v1.empty() ? "N/A" : v1;
        item.v2 = v2.empty() ? "N/A" : v2;
        item.n1 = n1;
        item.n2 = n2;
        item.hasNumbers = hasNum;
        item.higherBetter = higherBetter;
        items.push_back(item);
    };

    addItem(TR("Format", "Формат"), mp1.format, mp2.format);
    addItem(TR("Duration", "Длительность"), mp1.durationStr, mp2.durationStr, mp1.durationSec, mp2.durationSec, true, true);
    addItem(TR("Size", "Размер"), mp1.sizeStr, mp2.sizeStr, mp1.sizeBytes, mp2.sizeBytes, false, true);
    addItem(TR("Overall Bitrate", "Общий битрейт"), mp1.bitrateStr, mp2.bitrateStr, mp1.bitrateVal, mp2.bitrateVal, true, true);
    addItem(TR("Resolution", "Разрешение"), mp1.resolution, mp2.resolution);
    addItem(TR("Framerate", "Частота кадров"), mp1.fps.empty() ? "" : mp1.fps + TR(" fps", " к/с"), mp2.fps.empty() ? "" : mp2.fps + TR(" fps", " к/с"));
    addItem(TR("Video Codec", "Видеокодек"), mp1.videoCodec.empty() ? "" : (mp1.pixFmt.empty() ? mp1.videoCodec : mp1.videoCodec + " (" + mp1.pixFmt + ")"),
                                             mp2.videoCodec.empty() ? "" : (mp2.pixFmt.empty() ? mp2.videoCodec : mp2.videoCodec + " (" + mp2.pixFmt + ")"));
    addItem(TR("Video Bitrate", "Видео битрейт"), mp1.videoBitrateStr, mp2.videoBitrateStr, mp1.videoBitrateVal, mp2.videoBitrateVal, true, mp1.videoBitrateVal > 0 && mp2.videoBitrateVal > 0);
    addItem(TR("Audio Codec", "Аудиокодек"), mp1.audioCodec, mp2.audioCodec);
    addItem(TR("Audio Bitrate", "Аудио битрейт"), mp1.audioBitrateStr, mp2.audioBitrateStr, mp1.audioBitrateVal, mp2.audioBitrateVal, true, mp1.audioBitrateVal > 0 && mp2.audioBitrateVal > 0);
    addItem(TR("Sample Rate", "Частота аудио"), mp1.sampleRate.empty() ? "" : mp1.sampleRate + TR(" Hz", " Гц"), mp2.sampleRate.empty() ? "" : mp2.sampleRate + TR(" Hz", " Гц"));
    addItem(TR("Channels", "Каналы"), mp1.channels, mp2.channels);
    addItem(TR("Video Streams", "Видеопотоки"), to_string(mp1.videoTracks.size()), to_string(mp2.videoTracks.size()));
    addItem(TR("Audio Tracks", "Аудиодорожки"), to_string(mp1.audioTracks.size()), to_string(mp2.audioTracks.size()));
    addItem(TR("Subtitles", "Субтитры"), to_string(mp1.subtitleTracks.size()), to_string(mp2.subtitleTracks.size()));

    size_t tableLabelWidth = utf8Len(TR("Property", "Свойство"));
    size_t tableCol1Width = utf8Len(TR("File 1", "Файл 1"));
    for (const auto& it : items) {
        tableLabelWidth = max(tableLabelWidth, utf8Len(it.label));
        tableCol1Width = max(tableCol1Width, utf8Len(it.v1));
    }
    tableLabelWidth += 2;
    tableCol1Width += 3;

    cout << "\n";
    cout << padUtf8("", tableLabelWidth) << padUtf8(TR("File 1", "Файл 1"), tableCol1Width) << TR("File 2", "Файл 2") << "\n";
    cout << string(tableLabelWidth - 1, '-') << " " << string(tableCol1Width - 1, '-') << " " << string(tableCol1Width - 1, '-') << "\n";

    for (const auto& it : items) {
        cout << padUtf8(it.label, tableLabelWidth) << padUtf8(it.v1, tableCol1Width) << it.v2 << "\n";
    }

    Console::waitForKey();
}

} // namespace operations
} // namespace mr
