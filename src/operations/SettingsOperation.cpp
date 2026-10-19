#include "SettingsOperation.hpp"
#include "../app/AppContext.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/ShellDialogs.hpp"
#include "../services/LocalizationService.hpp"
#include "../services/FfmpegCommandBuilder.hpp"
#include "../ui/Menu.hpp"
#include <iostream>
#include <vector>

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

string SettingsOperation::getName() const {
    return TR("Settings", "Настройки");
}

void SettingsOperation::selectOutputFormat() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    vector<string> formatKeys = {
        "MP4(H.264)", "MP4(H.265/HEVC)", "MP4(AV1)",
        "M4V(H.264)", "M4V(H.265/HEVC)",
        "MKV(H.264)", "MKV(H.265/HEVC)",
        "WEBM(VP9)", "WEBM(AV1)", "MOV(H.264)", "AVI(MPEG4)",
        "MP3", "M4A(AAC)", "WAV", "FLAC", "OGG(Vorbis)"
    };

    vector<string> options = {
        TR("[Video] MP4 (H.264 / AVC)       - Maximum compatibility", "[Видео] MP4 (H.264 / AVC)       - Максимальная совместимость"),
        TR("[Video] MP4 (H.265 / HEVC)      - High efficiency (1080p / 4K)", "[Видео] MP4 (H.265 / HEVC)      - Высокая эффективность (1080p / 4K)"),
        TR("[Video] MP4 (AV1)               - Next-gen best compression", "[Видео] MP4 (AV1)               - Новейшее ультра-сжатие"),
        TR("[Video] M4V (H.264 / AVC)       - Apple iTunes / Apple TV", "[Видео] M4V (H.264 / AVC)       - Apple iTunes / Apple TV"),
        TR("[Video] M4V (H.265 / HEVC)      - Apple efficient HEVC", "[Видео] M4V (H.265 / HEVC)      - Эффективный HEVC для Apple"),
        TR("[Video] MKV (H.264)             - Universal film container", "[Видео] MKV (H.264)             - Универсальный контейнер для кино"),
        TR("[Video] MKV (H.265 / HEVC)      - Modern container with HEVC", "[Видео] MKV (H.265 / HEVC)      - Современный контейнер с HEVC"),
        TR("[Video] WEBM (VP9)              - Web video (YouTube standard)", "[Видео] WEBM (VP9)              - Веб-видео (стандарт YouTube)"),
        TR("[Video] WEBM (AV1)              - Ultra-efficient web video", "[Видео] WEBM (AV1)              - Ультра-сжатие для веб-видео"),
        TR("[Video] MOV (H.264)             - Apple QuickTime & editing", "[Видео] MOV (H.264)             - Apple QuickTime и видеомонтаж"),
        TR("[Video] AVI (MPEG-4)            - Legacy car / DVD players", "[Видео] AVI (MPEG-4)            - Старые DVD-плееры и магнитолы"),
        TR("[Audio] MP3                     - Universal audio format", "[Аудио] MP3                     - Универсальный аудиоформат"),
        TR("[Audio] M4A (AAC)               - High quality (Apple / YouTube)", "[Аудио] M4A (AAC)               - Качественный звук (Apple / YouTube)"),
        TR("[Audio] WAV                     - Uncompressed studio PCM", "[Аудио] WAV                     - Несжатый студийный звук (PCM)"),
        TR("[Audio] FLAC                    - Lossless compression (100% quality)", "[Аудио] FLAC                    - Сжатие без потерь (100% качество)"),
        TR("[Audio] OGG (Vorbis)            - Open-source audio format", "[Аудио] OGG (Vorbis)            - Свободный аудиоформат")
    };

    int cur = 0;
    for (size_t i = 0; i < formatKeys.size(); i++) {
        if (formatKeys[i] == cfg.outputFormat) { cur = (int)i; break; }
    }

    int sel = Menu::arrowSelect(TR("OUTPUT FORMAT", "ФОРМАТ ВЫВОДА"), "", options, cur);
    if (sel >= 0) {
        cfg.outputFormat = formatKeys[sel];
        ctx.configService.saveConfig(cfg);
    }
}

void SettingsOperation::selectResolution() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    vector<string> options = {
        TR("Original (no change)", "Оригинал (без изменений)"),
        "2160p (4K)", "1440p (2K)", "1080p (FullHD)", "720p (HD)", "480p", "360p"
    };
    vector<string> values = {"original", "2160", "1440", "1080", "720", "480", "360"};
    int cur = 0;
    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == cfg.outputResolution) { cur = (int)i; break; }
    }
    int sel = Menu::arrowSelect(TR("RESOLUTION", "РАЗРЕШЕНИЕ"), "", options, cur);
    if (sel >= 0) {
        cfg.outputResolution = values[sel];
        ctx.configService.saveConfig(cfg);
        Console::printColor(TR("[OK] Resolution set to ", "[OK] Разрешение: ") + cfg.outputResolution, Console::Color::GREEN);
        Console::waitForKey();
    }
}

void SettingsOperation::selectFPS() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    vector<string> options = {
        TR("Original (no change)", "Оригинал (без изменений)"),
        "60fps", "30fps", "24fps", "15fps"
    };
    vector<string> values = {"original", "60", "30", "24", "15"};
    int cur = 0;
    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == cfg.outputFps) { cur = (int)i; break; }
    }
    int sel = Menu::arrowSelect(TR("FPS", "FPS"), "", options, cur);
    if (sel >= 0) {
        cfg.outputFps = values[sel];
        ctx.configService.saveConfig(cfg);
        Console::printColor(TR("[OK] FPS set to ", "[OK] FPS: ") + cfg.outputFps, Console::Color::GREEN);
        Console::waitForKey();
    }
}

void SettingsOperation::selectPreset() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    vector<string> options = {
        "ultrafast", "superfast", "veryfast", "faster", "fast",
        "medium", "slow", "slower", "veryslow"
    };
    int cur = 0;
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i] == cfg.preset) { cur = (int)i; break; }
    }
    int sel = Menu::arrowSelect(TR("ENCODING PRESET", "ПРЕСЕТ КОДИРОВАНИЯ"), "", options, cur);
    if (sel >= 0) {
        cfg.preset = options[sel];
        ctx.configService.saveConfig(cfg);
        Console::printColor(TR("[OK] Preset set to ", "[OK] Пресет: ") + cfg.preset, Console::Color::GREEN);
        Console::waitForKey();
    }
}

void SettingsOperation::selectCRF() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" CRF VALUE", " ЗНАЧЕНИЕ CRF"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);
    cout << "\n" << TR("Current CRF: ", "Текущий CRF: ") << cfg.crfValue
         << "\n\n" << TR("Enter new CRF value (0-51, ESC to cancel): ", "Введите CRF (0-51, ESC для отмены): ");
    string val;
    if (!Console::inputLineWithEscape(val, "")) return;
    try {
        int v = stoi(val);
        if (v < 0 || v > 51) {
            Console::printColor(TR("[ERROR] CRF must be 0-51!", "[ОШИБКА] CRF должен быть 0-51!"), Console::Color::RED);
            Console::waitForKey();
            return;
        }
        cfg.crfValue = val;
        ctx.configService.saveConfig(cfg);
        Console::printColor(TR("[OK] CRF set to ", "[OK] CRF: ") + cfg.crfValue, Console::Color::GREEN);
    } catch (...) {
        Console::printColor(TR("[ERROR] Invalid number!", "[ОШИБКА] Неверное число!"), Console::Color::RED);
    }
    Console::waitForKey();
}

void SettingsOperation::selectAudioBitrate() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    vector<string> options = {"96 kbps", "128 kbps", "192 kbps", "256 kbps", "320 kbps"};
    vector<string> values = {"96", "128", "192", "256", "320"};
    int cur = 0;
    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == cfg.audioBitrate) { cur = (int)i; break; }
    }
    int sel = Menu::arrowSelect(TR("AUDIO BITRATE", "БИТРЕЙТ АУДИО"), "", options, cur);
    if (sel >= 0) {
        cfg.audioBitrate = values[sel];
        ctx.configService.saveConfig(cfg);
        Console::printColor(TR("[OK] Audio bitrate set to ", "[OK] Битрейт аудио: ") + cfg.audioBitrate + " kbps", Console::Color::GREEN);
        Console::waitForKey();
    }
}

void SettingsOperation::selectCodecSettings() {
    selectOutputFormat();
}

void SettingsOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    while (true) {
        vector<string> opts = {
            "1. " + TR("Output location: [", "Папка вывода: [") + cfg.outputPath + "]",
            "2. " + TR("Output format: [", "Формат вывода: [") + cfg.outputFormat + "]",
            "3. " + TR("Resolution: [", "Разрешение: [") + cfg.outputResolution + "]",
            "4. " + TR("FPS: [", "FPS: [") + cfg.outputFps + "]",
            "5. " + TR("Encoding preset: [", "Пресет кодирования: [") + cfg.preset + "]",
            "6. " + TR("CRF value: [", "Значение CRF: [") + cfg.crfValue + "]",
            "7. " + TR("Audio bitrate: [", "Битрейт аудио: [") + cfg.audioBitrate + " kbps]",
            "8. " + TR("Program acceleration: [", "Программное ускорение: [") + ctx.hardware.getAccelerationModeName(cfg.accelMode) + "]",
            "m. " + TR("Keep metadata: [", "Сохранять метаданные: [") + (cfg.keepMetadata ? TR("ON", "ВКЛ") : TR("OFF", "ВЫКЛ")) + "]",
            "v. " + TR("Video codec prompt: [", "Запрос видеокодека: [") + (cfg.videoCodecAsk ? TR("Always Ask", "Всегда спрашивать") : TR("Always use configured settings", "Всегда как задано моими настройками")) + "]",
            "a. " + TR("Audio codec prompt: [", "Запрос аудиокодека: [") + FfmpegCommandBuilder::getAudioCodecSettingName(cfg.audioCodec) + "]",
            "t. " + TR("Subtitle prompt: [", "Запрос субтитров: [") + cfg.subtitleAction + "]",
            "d. " + TR("Delete original prompt: [", "Запрос удаления оригинала: [") + cfg.deleteOriginal + "]",
            "c. " + TR("Save video cover: [", "Сохранять обложку видео: [") + (cfg.saveCover ? TR("ON", "ВКЛ") : TR("OFF", "ВЫКЛ")) + "]",
            "p. " + TR("Allow double processing in batch mode: [", "Разрешить двойную обработку в пакетном режиме: [") + (cfg.parallelBatch ? TR("ON", "ВКЛ") : TR("OFF", "ВЫКЛ")) + "]",
            "u. " + TR("Update FFmpeg & components", "Обновить FFmpeg")
        };
        vector<int> actions = {1,2,3,4,5,6,7,8,10,11,12,13,14,15,16,17};

        int sel = Menu::arrowSelect(TR("SETTINGS", "НАСТРОЙКИ"), "", opts, 0);
        if (sel < 0) return;

        int act = actions[sel];
        switch (act) {
            case 1: {
                string p = ShellDialogs::openFolderDialog(StringUtil::utf8ToWstring(TR("Select output folder", "Выберите папку вывода")).c_str());
                if (!p.empty()) {
                    cfg.outputPath = p;
                    PathProvider::createDirRecursive(cfg.outputPath);
                    ctx.configService.saveConfig(cfg);
                    Console::printColor(TR("[OK] Output location updated!", "[OK] Папка вывода обновлена!"), Console::Color::GREEN);
                } else {
                    Console::printColor(TR("[INFO] Not changed", "[ИНФО] Не изменено"), Console::Color::YELLOW);
                }
                Console::waitForKey();
                break;
            }
            case 2: selectOutputFormat(); break;
            case 3: selectResolution(); break;
            case 4: selectFPS(); break;
            case 5: selectPreset(); break;
            case 6: selectCRF(); break;
            case 7: selectAudioBitrate(); break;
            case 8: {
                vector<string> modesList = {TR("Software CPU (libx264)", "Программный CPU (libx264)")};
                vector<AccelMode> modesVal = {AccelMode::CPU_ONLY};

                if (ctx.hardware.hasNvidia || ctx.hardware.hasIntel || ctx.hardware.hasAmd) {
                    modesList.push_back(TR("Software + Hardware (CPU Decoding, GPU Encoding)", "Программный + Аппаратный (Декодирование CPU, Кодирование GPU)"));
                    modesVal.push_back(AccelMode::CPU_DEC_GPU_ENC);
                    modesList.push_back(TR("Reverse Hybrid (GPU Decoding, CPU Encoding)", "Обратный гибридный (Декодирование GPU, Кодирование CPU)"));
                    modesVal.push_back(AccelMode::GPU_DEC_CPU_ENC);
                }
                if (ctx.hardware.hasNvidia) {
                    modesList.push_back(TR("Hardware NVIDIA (NVENC)", "Аппаратный NVIDIA (NVENC)"));
                    modesVal.push_back(AccelMode::NVIDIA);
                }
                if (ctx.hardware.hasIntel) {
                    modesList.push_back(TR("Hardware Intel (Quick Sync Video)", "Аппаратный Intel (Quick Sync Video)"));
                    modesVal.push_back(AccelMode::INTEL);
                }
                if (ctx.hardware.hasAmd) {
                    modesList.push_back(TR("Hardware AMD (AMF)", "Аппаратный AMD (AMF)"));
                    modesVal.push_back(AccelMode::AMD);
                }

                int curMode = 0;
                for (size_t m = 0; m < modesVal.size(); m++) {
                    if (modesVal[m] == cfg.accelMode) { curMode = (int)m; break; }
                }

                int msel = Menu::arrowSelect(TR("ACCELERATION MODE", "РЕЖИМ УСКОРЕНИЯ"), "", modesList, curMode);
                if (msel >= 0) {
                    cfg.accelMode = modesVal[msel];
                    ctx.configService.saveConfig(cfg);
                }
                break;
            }
            case 10: {
                cfg.keepMetadata = !cfg.keepMetadata;
                ctx.configService.saveConfig(cfg);
                break;
            }
            case 11: {
                cfg.videoCodecAsk = !cfg.videoCodecAsk;
                ctx.configService.saveConfig(cfg);
                break;
            }
            case 12: {
                vector<string> aopts = {"copy", "ask", "aac", "ac3", "eac3", "mp3", "opus", "flac"};
                int asel = Menu::arrowSelect(TR("AUDIO CODEC PROMPT", "ЗАПРОС АУДИОКОДЕКА"), "", aopts, 0);
                if (asel >= 0) {
                    cfg.audioCodec = aopts[asel];
                    ctx.configService.saveConfig(cfg);
                }
                break;
            }
            case 13: {
                vector<string> sopts = {"ask", "convert", "burn", "drop"};
                int ssel = Menu::arrowSelect(TR("SUBTITLE ACTION", "ДЕЙСТВИЕ С СУБТИТРАМИ"), "", sopts, 0);
                if (ssel >= 0) {
                    cfg.subtitleAction = sopts[ssel];
                    ctx.configService.saveConfig(cfg);
                }
                break;
            }
            case 14: {
                vector<string> dopts = {"ask", "always", "never"};
                vector<string> dhints = {
                    TR("Ask every time before processing. If 'N', preserves original and adds suffix.",
                       "Спрашивать перед обработкой. При 'N' сохраняет оригинал и добавляет суффикс."),
                    TR("Always delete original and replace it with the processed file (no suffix).",
                       "Всегда удалять оригинал и заменять его обработанным файлом (без суффикса)."),
                    TR("Never delete original, always preserve it and add suffix to the new file.",
                       "Никогда не удалять оригинал, всегда сохранять его и добавлять суффикс к новому файлу.")
                };
                int curD = 0;
                if (cfg.deleteOriginal == "always" || cfg.deleteOriginal == "yes") curD = 1;
                else if (cfg.deleteOriginal == "never" || cfg.deleteOriginal == "no") curD = 2;
                int dsel = Menu::arrowSelect(TR("DELETE ORIGINAL", "УДАЛЕНИЕ ОРИГИНАЛА"), "", dopts, curD, dhints);
                if (dsel >= 0) {
                    cfg.deleteOriginal = dopts[dsel];
                    ctx.configService.saveConfig(cfg);
                }
                break;
            }
            case 15: {
                cfg.saveCover = !cfg.saveCover;
                ctx.configService.saveConfig(cfg);
                break;
            }
            case 16: {
                cfg.parallelBatch = !cfg.parallelBatch;
                ctx.configService.saveConfig(cfg);
                break;
            }
            case 17: {
                ctx.installer.checkOrInstallComponents();
                ctx.refreshPaths();
                break;
            }
        }
    }
}

} // namespace operations
} // namespace mr
