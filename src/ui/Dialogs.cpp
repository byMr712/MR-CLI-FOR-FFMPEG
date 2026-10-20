#include "Dialogs.hpp"
#include "Menu.hpp"
#include "../core/Console.hpp"
#include "../core/PathProvider.hpp"
#include "../services/LocalizationService.hpp"
#include "../services/GpuDetector.hpp"
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace mr {
namespace ui {

bool Dialogs::selectVideoTrackForFile(const std::string& filePath,
                                     const services::FfprobeService& probe,
                                     std::string& mapArgs,
                                     bool allowAllTracks,
                                     bool isBatchMode,
                                     bool* outApplyToAllBatch,
                                     int* outSelectedTrackIndex) {
    mapArgs.clear();
    if (outApplyToAllBatch) *outApplyToAllBatch = false;
    if (outSelectedTrackIndex) *outSelectedTrackIndex = -1;

    std::vector<domain::VideoTrack> allTracks = probe.getVideoTracks(filePath);
    if (allTracks.empty()) return true;

    std::vector<int> realIndices;
    for (size_t i = 0; i < allTracks.size(); ++i) {
        if (!allTracks[i].isCoverOrAttachedPic()) {
            realIndices.push_back(static_cast<int>(i));
        }
    }
    if (realIndices.empty()) {
        for (size_t i = 0; i < allTracks.size(); ++i) {
            realIndices.push_back(static_cast<int>(i));
        }
    }

    if (realIndices.size() <= 1) {
        return true;
    }

    const int FIRST_STREAM_CHOICE = 1;
    std::vector<std::string> opts;
    std::vector<std::string> hints;

    int primaryIdx = realIndices[0];
    if (allowAllTracks) {
        opts.push_back(services::tr("Keep all video streams", "Сохранить все видеопотоки"));
        hints.push_back(services::tr("Preserves every video stream of this file.", "Сохраняет все видеопотоки этого файла."));
    } else {
        opts.push_back(services::tr("Use primary video stream", "Использовать основной видеопоток"));
        hints.push_back(services::tr("Selects the first real video stream, ignoring cover art.", "Выбирает первый основной видеопоток, игнорируя обложку."));
    }
    for (size_t i = 0; i < realIndices.size(); ++i) {
        int idx = realIndices[i];
        opts.push_back(services::tr("Stream ", "Поток ") + std::to_string(i + 1) + ": " + allTracks[idx].getDisplayString(services::LocalizationService::instance().getLanguage()));
        hints.push_back(services::tr("Select this video stream for processing.", "Выбрать этот видеопоток для обработки."));
    }

    std::string desc = services::tr("This file contains multiple video streams (" + std::to_string(realIndices.size()) + ").\n\"" + filePath + "\"\n\nSelect which video stream to process:",
                                    "В этом файле обнаружено несколько видеопотоков (" + std::to_string(realIndices.size()) + ").\n\"" + filePath + "\"\n\nВыберите, какой видеопоток обработать:");

    int sel = Menu::arrowSelect(services::tr("VIDEO STREAM SELECTION", "ВЫБОР ВИДЕОПОТОКА"), desc, opts, 0, hints, true);
    if (sel < 0) {
        return false;
    }

    bool applyToAll = false;
    if (isBatchMode) {
        std::vector<std::string> scopeOptions = {
            services::tr("Apply to this video only", "Применить только к этому видео"),
            services::tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
        };
        std::vector<std::string> scopeHints = {
            services::tr("The choice applies only to the current video. It will be asked again for the next file.",
                         "Выбор применяется только к текущему видео. Для следующего файла вопрос будет задан снова."),
            services::tr("The choice applies to all remaining videos in this batch, matching by resolution, language and title.",
                         "Выбор применяется ко всем оставшимся видео в этом пакете, с подбором по разрешению, языку и названию.")
        };

        int scopeSel = Menu::arrowSelect(
            services::tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            services::tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
                         "Ваш текущий выбор НЕ меняет глобальных настроек.\nТот же выбор можно сделать в глобальных настройках для сохранения."),
            scopeOptions,
            0,
            scopeHints,
            true
        );
        applyToAll = (scopeSel == 1);
    }
    if (outApplyToAllBatch) *outApplyToAllBatch = applyToAll;

    if (sel == 0) {
        if (allowAllTracks) {
            mapArgs = " -map 0:v?";
            if (outSelectedTrackIndex) *outSelectedTrackIndex = (isBatchMode && !applyToAll) ? -4 : -3;
        } else {
            mapArgs = " -map 0:v:" + std::to_string(allTracks[primaryIdx].videoIndex);
            if (outSelectedTrackIndex) *outSelectedTrackIndex = applyToAll ? -1 : -2;
        }
    } else {
        int chosenRealIdx = realIndices[sel - FIRST_STREAM_CHOICE];
        mapArgs = " -map 0:v:" + std::to_string(allTracks[chosenRealIdx].videoIndex);
        if (outSelectedTrackIndex) *outSelectedTrackIndex = chosenRealIdx;
    }

    return true;
}

bool Dialogs::selectAudioTrackForFile(const std::string& filePath,
                                     const services::FfprobeService& probe,
                                     std::string& mapArgs,
                                     bool allowAllTracks,
                                     bool isBatchMode,
                                     bool* outKeepAllForBatch,
                                     int* outSelectedTrackIndex,
                                     bool* outApplyToAllBatch) {
    mapArgs.clear();
    if (outKeepAllForBatch) *outKeepAllForBatch = false;
    if (outSelectedTrackIndex) *outSelectedTrackIndex = -1;
    if (outApplyToAllBatch) *outApplyToAllBatch = false;

    std::vector<domain::AudioTrack> tracks = probe.getAudioTracks(filePath);
    if (tracks.size() <= 1) {
        return true;
    }

    std::vector<std::string> opts;
    std::vector<std::string> hints;

    if (allowAllTracks) {
        opts.push_back(services::tr("Keep all audio tracks", "Сохранить все аудиодорожки"));
        hints.push_back(services::tr("Preserves every audio stream of this file.", "Сохраняет все аудиопотоки этого файла."));
    }
    for (size_t i = 0; i < tracks.size(); ++i) {
        opts.push_back(services::tr("Track ", "Дорожка ") + std::to_string(i + 1) + ": " + tracks[i].getDisplayString(services::LocalizationService::instance().getLanguage()));
        hints.push_back(services::tr("Select only this audio stream for the output file.", "Выбрать только этот аудиопоток для выходного файла."));
    }

    std::string desc = services::tr("This video contains multiple audio tracks (" + std::to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nSelect which audio track to include in the output:",
                                    "В этом видео обнаружено несколько аудиодорожек (" + std::to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nВыберите, какую аудиодорожку включить в результат:");

    int sel = Menu::arrowSelect(services::tr("AUDIO TRACK SELECTION", "ВЫБОР АУДИОДОРОЖКИ"), desc, opts, 0, hints, true);
    if (sel < 0) {
        return false;
    }

    bool keepAll = allowAllTracks && sel == 0;
    int trackIdx = allowAllTracks ? sel - 1 : sel;

    bool applyToAll = false;
    if (isBatchMode) {
        std::vector<std::string> scopeOptions = {
            services::tr("Apply to this video only", "Применить только к этому видео"),
            services::tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
        };
        std::vector<std::string> scopeHints = {
            services::tr("The choice applies only to the current video. It will be asked again for the next file.",
                         "Выбор применяется только к текущему видео. Для следующего файла вопрос будет задан снова."),
            services::tr("The choice applies to all remaining videos in this batch, matching by language and title.",
                         "Выбор применяется ко всем оставшимся видео в этом пакете, с подбором по языку и названию.")
        };

        int scopeSel = Menu::arrowSelect(
            services::tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            services::tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
                         "Ваш текущий выбор НЕ меняет глобальных настроек.\nТот же выбор можно сделать в глобальных настройках для сохранения."),
            scopeOptions,
            0,
            scopeHints,
            true
        );
        applyToAll = (scopeSel == 1);
    }

    if (keepAll) {
        mapArgs = " -map 0:a?";
        if (outKeepAllForBatch) *outKeepAllForBatch = applyToAll;
        if (outSelectedTrackIndex) *outSelectedTrackIndex = applyToAll ? -1 : -2;
    } else {
        mapArgs = " -map 0:a:" + std::to_string(tracks[trackIdx].audioIndex);
        if (outKeepAllForBatch) *outKeepAllForBatch = false;
        if (outSelectedTrackIndex) *outSelectedTrackIndex = trackIdx;
    }
    if (outApplyToAllBatch) *outApplyToAllBatch = applyToAll;
    return true;
}

domain::SubtitleDialogResult Dialogs::dialogSubtitleIncompatibility(const std::string& filePath,
                                                                    const std::string& detectedSubTypes,
                                                                    bool batchMode) {
    domain::SubtitleDialogResult result;
    std::string desc = services::tr(
        "Complex subtitles (" + detectedSubTypes + ") were found that are not supported by the MP4 container.\n\""
        + filePath + "\"\n\nChoose subtitle processing method:",
        "В исходном файле обнаружены сложные субтитры (" + detectedSubTypes + "), которые не поддерживаются форматом MP4.\n\""
        + filePath + "\"\n\nВыберите способ обработки субтитров:"
    );

    std::vector<std::string> opts = {
        services::tr("Convert subtitles (Recommended, but will be plain text without original styling)",
                     "Конвертировать субтитры (Рекомендуется, но будут как просто текст без изначального стиля)"),
        services::tr("Burn subtitles into video (Preserves style, but permanently embeds them in video frames)",
                     "Вшить субтитры в видео (Сохраняет стиль, но вшивает субтитры прям в само видео, их нельзя будет отключить)"),
        services::tr("Save video without subtitles",
                     "Сохранить видео без субтитров"),
        services::tr("Change format to MKV (Preserves all subtitles with full original styles)",
                     "Изменить формат на MKV (Сохранит все субтитры без изменений)"),
        services::tr("Skip current video",
                     "Пропустить текущее видео"),
        services::tr("Cancel all operations",
                     "Отменить все операции")
    };

    std::vector<std::string> hints = {
        services::tr("Converts subtitles to mov_text format. Retains toggleable tracks in MP4, but strips colors, custom fonts, and positioning.",
                     "Конвертирует субтитры в стандартный формат mov_text. Сохраняет отключаемые дорожки в MP4, но удаляет цвета, шрифты и позиционирование."),
        services::tr("Re-encodes video while burning subtitles into image frames. Retains 100% of fonts, colors and effects, but cannot be toggled off.",
                     "Перекодирует видео с наложением субтитров прямо на кадры. Сохраняет 100% стилей, цветов и эффектов, но субтитры нельзя будет отключить."),
        services::tr("Removes all subtitle tracks and outputs video with audio streams only.",
                     "Удаляет все дорожки субтитров и сохраняет видео только с аудиодорожками."),
        services::tr("Changes the output container to MKV. Retains all subtitle streams and complex styling bit-for-bit without loss.",
                     "Переключает выходной контейнер на MKV. Сохраняет все дорожки субтитров и сложные стили бит-в-бит без потерь."),
        services::tr("Skips processing of this file and proceeds to the next one in queue.",
                     "Пропускает обработку этого файла и переходит к следующему в очереди."),
        services::tr("Aborts all remaining operations and finishes with summary of successes and errors.",
                     "Прерывает оставшиеся операции и завершает работу со сводкой успехов и ошибок.")
    };

    int sel = Menu::arrowSelect(services::tr("SUBTITLE INCOMPATIBILITY", "НЕСОВМЕСТИМОСТЬ СУБТИТРОВ"), desc, opts, 0, hints, true);
    if (sel < 0) {
        result.action = domain::SubtitleIncompatAction::SkipCurrent;
        result.applyToAll = false;
        return result;
    }
    result.action = static_cast<domain::SubtitleIncompatAction>(sel);

    if (batchMode && result.action != domain::SubtitleIncompatAction::CancelAll) {
        std::vector<std::string> scopeOptions;
        std::vector<std::string> scopeHints;
        if (result.action == domain::SubtitleIncompatAction::SkipCurrent) {
            scopeOptions = {
                services::tr("Skip this video only", "Пропустить только это видео"),
                services::tr("Skip ALL remaining videos with subtitles", "Пропустить ВСЕ оставшиеся видео с субтитрами")
            };
            scopeHints = {
                services::tr("Only this video will be skipped. The batch continues with the next file.",
                             "Только это видео будет пропущено. Пакет продолжится со следующего файла."),
                services::tr("All remaining videos with incompatible subtitles in this batch will be skipped.",
                             "Все оставшиеся видео с несовместимыми субтитрами в этом пакете будут пропущены.")
            };
        } else {
            scopeOptions = {
                services::tr("Apply to this video only", "Применить только к этому видео"),
                services::tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
            };
            scopeHints = {
                services::tr("The choice applies only to the current video. Global settings remain unchanged.",
                             "Выбор применяется только к текущему видео. Глобальные настройки не изменяются."),
                services::tr("The choice applies to all remaining videos with incompatible subtitles in this batch.",
                             "Выбор применяется ко всем оставшимся видео с несовместимыми субтитрами в этом пакете.")
            };
        }

        int scopeSel = Menu::arrowSelect(
            services::tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            services::tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
                         "Ваш текущий выбор НЕ меняет глобальных настроек.\nТот же выбор можно сделать в глобальных настройках для сохранения."),
            scopeOptions,
            0,
            scopeHints,
            true
        );
        result.applyToAll = (scopeSel == 1);
    }

    return result;
}

EncodingDialogResult Dialogs::dialogEncodingProblem(const domain::EncodingProblem& ep, bool batchMode) {
    EncodingDialogResult result;

    std::string fileLine = ep.filePath.empty() ? "" : ("\"" + ep.filePath + "\"\n\n");
    std::string description = services::tr(
        "The selected codec or format or circumstances are not suitable\n"
        "for creating the final video due to possible information loss\n"
        "(e.g. color space) or hardware decoder/encoder failure.\n"
        + fileLine +
        "Detailed reason:\n",
        "Похоже выбранный вами кодек или формат или аппаратное ускорение не подходят\n"
        "для создания конечного видео (несовместимость кодека/декодера или ошибка энкодера).\n"
        + fileLine +
        "Подробная причина:\n"
    );
    std::istringstream reasonStream(ep.detailedReason);
    std::string reasonLine;
    while (std::getline(reasonStream, reasonLine)) {
        description += "  " + reasonLine + "\n";
    }

    std::vector<std::string> options;
    std::vector<std::string> hints;
    int formatOptIdx = -1;
    int skipOptIdx = -1;

    if (ep.isOddDimensions) {
        std::string evenDimStr = std::to_string(ep.evenWidth) + "x" + std::to_string(ep.evenHeight);
        options = {
            services::tr("Automatically align resolution to even (" + evenDimStr + ")",
                         "Автоматически выровнять разрешение до чётного (" + evenDimStr + ")"),
            services::tr("Use software encoder libx264 on CPU (Safest)",
                         "Использовать программный кодировщик libx264 на CPU (Самый надёжный)"),
            services::tr("Choose a different output codec",
                         "Выбрать другой конечный кодек"),
            services::tr("Use hybrid encoder: CPU decodes, GPU encodes",
                         "Использовать гибридный кодировщик: процессор декодирует, а видеокарта кодирует"),
            services::tr("Try anyway / Retry (possible error and file loss)",
                         "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            services::tr("Skip video",
                         "Пропустить видео")
        };
        hints = {
            services::tr("Applies auto scale filter to make width and height even. Video quality remains 100% identical.",
                         "Применяет фильтр scale для выравнивания разрешения. Качество видео остаётся на 100% прежним."),
            services::tr("Converts to libx264 software encoding. CPU handles any odd resolutions without problems.",
                         "Переключает на программный кодек libx264. Процессор без проблем работает с любым нечётным разрешением."),
            services::tr("Opens format selection menu where you can pick another codec.",
                         "Откроет меню выбора формата, где можно выбрать другой кодек."),
            services::tr("CPU decodes the source, GPU encodes the output.",
                         "Процессор декодирует исходник, видеокарта кодирует результат."),
            services::tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
                         "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            services::tr("Do not process this video, move to the next one.",
                         "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 2;
        skipOptIdx = 5;
    } else if (ep.isUltraHighRes) {
        options = {
            services::tr("Use software encoder libx264 on CPU (No resolution limits)",
                         "Использовать программный кодировщик libx264 на CPU (Без ограничений по разрешению)"),
            services::tr("Downscale resolution to 4K (3840x2160) and encode on GPU",
                         "Уменьшить разрешение до 4K (3840x2160) и кодировать на видеокарте"),
            services::tr("Choose a different output codec",
                         "Выбрать другой конечный кодек"),
            services::tr("Try anyway / Retry (possible error and file loss)",
                         "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            services::tr("Skip video",
                         "Пропустить видео")
        };
        hints = {
            services::tr("CPU encodes video of any resolution without GPU hardware limits.",
                         "Процессор кодирует видео любого разрешения без аппаратных ограничений видеокарты."),
            services::tr("Downscales video to 4K UHD so it fits GPU hardware encoder limits.",
                         "Уменьшает видео до 4K UHD, чтобы оно укладывалось в аппаратные лимиты видеокарты."),
            services::tr("Opens format selection menu where you can pick another codec.",
                         "Откроет меню выбора формата, где можно выбрать другой кодек."),
            services::tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
                         "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            services::tr("Do not process this video, move to the next one.",
                         "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 2;
        skipOptIdx = 4;
    } else {
        options = {
            services::tr("Use software encoder libx264 on your CPU (Recommended if unsure)",
                         "Использовать программный кодировщик libx264 на вашем процессоре (Рекомендуется, если не знаете что выбрать)"),
            services::tr("Choose a different output codec (may help)",
                         "Выбрать другой конечный кодек (может помочь)"),
            services::tr("Use hybrid encoder: CPU decodes, GPU encodes",
                         "Использовать гибридный кодировщик: процессор декодирует, а видеокарта кодирует"),
            services::tr("Use reverse hybrid: GPU decodes, CPU encodes",
                         "Использовать обратный гибридный кодировщик: видеокарта декодирует, а процессор кодирует"),
            services::tr("Try anyway / Retry (possible error and file loss)",
                         "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            services::tr("Skip video",
                         "Пропустить видео")
        };
        hints = {
            services::tr("Converts to libx264 software encoding. Safest option, guaranteed compatibility.",
                         "Конвертирует в программный кодировщик libx264. Самый безопасный вариант, гарантированная совместимость."),
            services::tr("Opens format selection menu where you can pick a compatible codec (e.g. H.265).",
                         "Откроет меню выбора формата, где можно выбрать совместимый кодек (напр. H.265)."),
            services::tr("CPU decodes the source, GPU encodes the output. Safest hardware encoding mode.",
                         "Процессор декодирует исходник, видеокарта кодирует результат. Самый надежный режим аппаратного кодирования."),
            services::tr("GPU decodes the source, CPU encodes the output. Uses hardware decoding with software encoding.",
                         "Видеокарта декодирует исходник, процессор кодирует результат. Аппаратное декодирование с программным кодированием."),
            services::tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
                         "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            services::tr("Do not process this video, move to the next one.",
                         "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 1;
        skipOptIdx = 5;
    }

    int sel = Menu::arrowSelect(
        services::tr("ENCODING PROBLEM", "ПРОБЛЕМА КОДИРОВАНИЯ"),
        description,
        options,
        0,
        hints,
        true
    );

    if (ep.isOddDimensions) {
        if (sel == 0) { result.action = 10; result.autoAlignResolution = true; }
        else if (sel == 1) { result.action = 0; }
        else if (sel == 2) { result.action = 1; }
        else if (sel == 3) { result.action = 2; }
        else if (sel == 4) { result.action = 4; }
        else { result.action = 5; }
    } else if (ep.isUltraHighRes) {
        if (sel == 0) { result.action = 0; }
        else if (sel == 1) { result.action = 11; result.downscaleTo4K = true; }
        else if (sel == 2) { result.action = 1; }
        else if (sel == 3) { result.action = 4; }
        else { result.action = 5; }
    } else {
        result.action = sel;
    }

    if (sel == formatOptIdx) {
        std::vector<std::string> fmtKeys = {
            "MP4(H.264)", "MP4(H.265/HEVC)", "MP4(AV1)",
            "MKV(H.264)", "MKV(H.265/HEVC)",
            "WEBM(VP9)", "WEBM(AV1)", "MOV(H.264)", "AVI(MPEG4)"
        };
        std::vector<std::string> fmtOpts = {
            services::tr("MP4 (H.264 / AVC)", "MP4 (H.264 / AVC)"),
            services::tr("MP4 (H.265 / HEVC)", "MP4 (H.265 / HEVC)"),
            services::tr("MP4 (AV1)", "MP4 (AV1)"),
            services::tr("MKV (H.264)", "MKV (H.264)"),
            services::tr("MKV (H.265 / HEVC)", "MKV (H.265 / HEVC)"),
            services::tr("WEBM (VP9)", "WEBM (VP9)"),
            services::tr("WEBM (AV1)", "WEBM (AV1)"),
            services::tr("MOV (H.264)", "MOV (H.264)"),
            services::tr("AVI (MPEG-4)", "AVI (MPEG-4)")
        };
        std::vector<std::string> fmtHints = {
            services::tr("Maximum compatibility. Plays on all devices.",
                         "Максимальная совместимость. Воспроизводится на любых устройствах."),
            services::tr("Modern high-efficiency codec. 30-50% smaller than H.264.",
                         "Современный кодек. Файлы на 30-50% меньше H.264."),
            services::tr("Next-gen royalty-free codec. Best compression, slower encoding.",
                         "Кодек нового поколения. Максимальное сжатие, медленнее кодирование."),
            services::tr("Matroska container with H.264.",
                         "Контейнер Matroska с H.264."),
            services::tr("Matroska container with HEVC codec.",
                         "Контейнер Matroska с кодеком HEVC."),
            services::tr("Google web video format. Supported by all browsers.",
                         "Веб-формат Google. Поддерживается всеми браузерами."),
            services::tr("Next-gen web format with AV1.",
                         "Новейший веб-формат с AV1."),
            services::tr("Apple QuickTime. Native for macOS/iPhone and video editors.",
                         "Apple QuickTime. Родной формат для macOS/iPhone и видеоредакторов."),
            services::tr("Legacy format for older DVD players and car stereos.",
                         "Устаревший формат для старых DVD-плееров и автомагнитол.")
        };

        int fmtSel = Menu::arrowSelect(
            services::tr("CHOOSE CODEC", "ВЫБЕРИТЕ КОДЕК"),
            services::tr("Select a different output format/codec.\nThis change applies ONLY to the current video.\nGlobal settings remain unchanged.",
                         "Выберите другой выходной формат/кодек.\nИзменение действует ТОЛЬКО на текущее видео.\nГлобальные настройки не изменяются."),
            fmtOpts,
            0,
            fmtHints,
            true
        );
        if (fmtSel >= 0) {
            result.chosenFormat = fmtKeys[fmtSel];
        }
    }

    if (sel >= 0 && batchMode) {
        std::vector<std::string> scopeOptions;
        std::vector<std::string> scopeHints;
        if (sel == skipOptIdx) {
            scopeOptions = {
                services::tr("Skip this video only", "Пропустить только это видео"),
                services::tr("Skip ALL remaining videos", "Пропустить ВСЕ оставшиеся видео")
            };
            scopeHints = {
                services::tr("Only this video will be skipped. The batch continues with the next file.",
                             "Только это видео будет пропущено. Пакет продолжится со следующего файла."),
                services::tr("All remaining videos in this batch will be skipped.",
                             "Все оставшиеся видео в этом пакете будут пропущены.")
            };
        } else {
            scopeOptions = {
                services::tr("Apply to this video only", "Применить только к этому видео"),
                services::tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
            };
            scopeHints = {
                services::tr("The choice applies only to the current video. Global settings remain unchanged.",
                             "Выбор применяется только к текущему видео. Глобальные настройки не изменяются."),
                services::tr("The choice applies to all remaining videos. You can also change this in global settings.",
                             "Выбор применяется ко всем оставшимся видео. Тот же выбор можно сделать в глобальных настройках.")
            };
        }
        int scopeSel = Menu::arrowSelect(
            services::tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            services::tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
                         "Ваш текущий выбор НЕ меняет глобальных настроек.\nТот же выбор можно сделать в глобальных настройках для сохранения."),
            scopeOptions,
            0,
            scopeHints,
            true
        );
        result.applyToAll = (scopeSel == 1);
    }

    return result;
}

bool Dialogs::promptVideoCodecSettings(const domain::AppConfig& cfg) {
    if (!cfg.videoCodecAsk) return true;
    core::Console::clearScreen();
    core::Console::print("========================================", core::ConsoleColor::CYAN);
    core::Console::print(" " + services::tr("VIDEO CODEC SETTINGS REVIEW", "ПРОВЕРКА НАСТРОЕК ВИДЕОКОДЕКА") + "\n", core::ConsoleColor::CYAN);
    core::Console::print("========================================\n", core::ConsoleColor::CYAN);
    std::cout << "\n" << services::tr("Current video settings:", "Текущие настройки видео:") << "\n"
              << "  " << services::tr("Format: ", "Формат: ") << cfg.outputFormat << "\n"
              << "  " << services::tr("CRF: ", "CRF: ") << cfg.crfValue << "\n"
              << "  " << services::tr("Preset: ", "Пресет: ") << cfg.preset << "\n"
              << "  " << services::tr("Resolution: ", "Разрешение: ") << cfg.outputResolution << "\n"
              << "  " << services::tr("Program acceleration: ", "Программное ускорение: ") << services::GpuDetector::getAccelerationModeName(cfg.accelMode) << "\n"
              << "\n" << services::tr("Proceed with these settings? [Y/N]: ", "Продолжить с этими настройками? [Y/N]: ");

    char ch = core::Console::getMenuChoice();
    if (ch == 27) return false;
    if (ch == 'n') {
        std::cout << "N\n";
        core::Console::print(services::tr("[INFO] Change settings in the Settings menu and try again.\n",
                                          "[ИНФО] Измените настройки в меню Настроек и повторите.\n"),
                             core::ConsoleColor::YELLOW);
        core::Console::waitForKey();
        return false;
    }
    std::cout << "Y\n";
    return true;
}

std::string Dialogs::selectAudioCodecForOperation() {
    std::vector<std::string> keys = {
        "copy", "aac", "ac3", "eac3", "mp3", "opus", "flac", "pcm_s16le"
    };

    std::vector<std::string> options = {
        services::tr("Copy original (copy)            - Keep original stream, no quality loss (Fastest)",
                     "Как в оригинале (copy)          - Без пережатия звука и без потерь качества (Быстрее всего)"),
        services::tr("AAC                             - Modern universal high-quality standard",
                     "AAC                             - Универсальный стандарт высокого качества"),
        services::tr("AC3 (Dolby Digital)             - Surround 5.1 / Home Cinema standard",
                     "AC3 (Dolby Digital)             - Стандарт объемного звука 5.1 для ТВ и кинотеатров"),
        services::tr("E-AC3 (Dolby Digital Plus)      - Advanced streaming surround audio",
                     "E-AC3 (Dolby Digital Plus)      - Улучшенный объемный звук для современных медиаплееров"),
        services::tr("MP3 (libmp3lame)                - Classic MP3 compression",
                     "MP3 (libmp3lame)                - Классическое сжатие MP3"),
        services::tr("Opus (libopus)                  - Ultra-efficient modern speech & music codec",
                     "Opus (libopus)                  - Сверхэффективный современный кодек"),
        services::tr("FLAC                            - Lossless compression (100% studio quality)",
                     "FLAC                            - Сжатие без потерь (100% студийное качество)"),
        services::tr("PCM / WAV                       - Uncompressed studio PCM audio",
                     "PCM / WAV                       - Несжатый студийный звук")
    };

    std::vector<std::string> hints = {
        services::tr("Preserves the original audio stream bit-for-bit without re-encoding. Zero quality loss and maximum speed.",
                     "Сохраняет исходный аудиопоток бит-в-бит без перекодирования. Нулевая потеря качества и максимальная скорость."),
        services::tr("Advanced Audio Coding. Standard for MP4, YouTube and Apple. Best balance of quality and compatibility.",
                     "Стандарт для MP4, YouTube и Apple. Оптимальный баланс совместимости и качества."),
        services::tr("Dolby Digital 5.1 AC-3. Supported by virtually all home theaters, AV receivers and TVs.",
                     "Dolby Digital 5.1. Поддерживается практически всеми домашними кинотеатрами, ресиверами и ТВ."),
        services::tr("Dolby Digital Plus. Enhanced bitrate efficiency with up to 7.1 channels for modern setups.",
                     "Dolby Digital Plus. Улучшенная эффективность и поддержка до 7.1 каналов для современных устройств."),
        services::tr("Classic MPEG-1 Audio Layer III. Plays everywhere, but less efficient than AAC.",
                     "Классический MP3. Воспроизводится на любых устройствах, но менее эффективен, чем AAC."),
        services::tr("Modern low-latency open codec with superior clarity at lower bitrates (ideal for WebM/MKV).",
                     "Современный открытый кодек с отличной детализацией при низких битрейтах (идеален для WebM/MKV)."),
        services::tr("Free Lossless Audio Codec. Exact mathematical bit-perfect audio preservation.",
                     "Сжатие без потерь. Точное побитовое сохранение оригинального звука без изменений."),
        services::tr("Uncompressed raw PCM. Zero compression, maximum compatibility with editing software.",
                     "Несжатый PCM. Максимальная совместимость с программами монтажа, большие файлы.")
    };

    int sel = Menu::arrowSelect(services::tr("SELECT AUDIO CODEC", "ВЫБОР АУДИОКОДЕКА"),
                                services::tr("Choose audio codec for this operation:", "Выберите аудиокодек для этой операции:"),
                                options, 0, hints, true);
    if (sel < 0) return "";
    return keys[sel];
}

bool Dialogs::promptAudioCodecSettings(const domain::AppConfig& cfg, std::string& chosenCodec) {
    if (cfg.audioCodec != "ask") {
        chosenCodec = cfg.audioCodec;
        return true;
    }
    std::string codec = selectAudioCodecForOperation();
    if (codec.empty()) return false;
    chosenCodec = codec;
    return true;
}

bool Dialogs::promptDeleteOriginal(bool isBatch, const std::string& deleteOriginalSetting, bool& outDelete) {
    if (deleteOriginalSetting == "yes" || deleteOriginalSetting == "always") {
        outDelete = true;
        return true;
    }
    if (deleteOriginalSetting == "no" || deleteOriginalSetting == "never") {
        outDelete = false;
        return true;
    }
    if (isBatch) {
        std::cout << "\n" << services::tr("Delete original files after conversion? [Y/N]: ",
                                          "Удалить оригиналы после преобразования? [Y/N]: ");
    } else {
        std::cout << "\n" << services::tr("Delete original file after processing? [Y/N]: ",
                                          "Удалить исходный файл после обработки? [Y/N]: ");
    }
    char chDel = core::Console::getMenuChoice();
    if (chDel == 27) return false;
    outDelete = (chDel == 'y' || chDel == 'Y');
    std::cout << (outDelete ? "Y\n" : "N\n");
    return true;
}

void Dialogs::handleOriginalDeletion(const std::string& srcPath, const std::string& dstPath, bool isTemp, bool shouldDelete) {
    if (!shouldDelete || srcPath.empty() || isTemp) return;
    std::error_code ec;
    auto absIn = fs::weakly_canonical(fs::u8path(srcPath), ec);
    if (ec) return;
    auto absOut = fs::weakly_canonical(fs::u8path(dstPath), ec);
    if (ec) return;
    if (absIn != absOut && core::PathProvider::fileExists(srcPath)) {
        fs::remove(fs::u8path(srcPath), ec);
    }
}

int Dialogs::askBatchPauseAction(const std::string& fileLabel) {
    core::Console::flushInput();
    std::cout << "\n";
    core::Console::print("========================================", core::ConsoleColor::YELLOW);
    core::Console::print(" " + services::tr("PAUSED - Batch Processing Interrupted", "ПАУЗА - Пакетная обработка прервана") + "\n", core::ConsoleColor::YELLOW);
    core::Console::print("========================================\n", core::ConsoleColor::YELLOW);
    std::cout << services::tr(" Current file: ", " Текущий файл: ") << fileLabel << "\n\n";

    std::vector<std::string> pauseOpts = {
        services::tr("Retry this video (run again)", "Повторить это видео (запустить снова)"),
        services::tr("Skip this video (continue with next)", "Пропустить это видео (продолжить со следующего)"),
        services::tr("Cancel entire batch (stop everything)", "Отменить весь пакет (остановить всё)")
    };
    std::vector<std::string> pauseHints = {
        services::tr("Re-runs processing of this video from the beginning.",
                     "Запускает обработку этого видео заново с начала."),
        services::tr("Leaves this video unprocessed, marks it skipped, and moves to the next.",
                     "Оставляет это видео необработанным, помечает пропущенным и переходит к следующему."),
        services::tr("Immediately stops all remaining batch processing and prints the summary.",
                     "Немедленно прекращает обработку всех оставшихся файлов и выводит сводку.")
    };
    int pauseSel = Menu::arrowSelect(
        services::tr("BATCH PAUSE", "ПАУЗА ПАКЕТА"),
        services::tr("Batch was interrupted. Choose what to do next:",
                     "Пакетная обработка прервана. Выберите дальнейшее действие:"),
        pauseOpts, 0, pauseHints, true);

    if (pauseSel == 0) return 3; // retry
    if (pauseSel == 1) return 1; // skip
    return 2;                    // cancel
}

} // namespace ui
} // namespace mr
