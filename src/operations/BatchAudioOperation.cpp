#include "BatchAudioOperation.hpp"
#include "../app/AppContext.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/ShellDialogs.hpp"
#include "../services/FfmpegCommandBuilder.hpp"
#include "../services/FfmpegRunner.hpp"
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

string BatchAudioOperation::getName() const {
    return TR("Batch convert audio", "Пакетное конвертирование аудио");
}

void BatchAudioOperation::execute() {
    auto& ctx = AppContext::instance();
    auto& cfg = ctx.config();

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH AUDIO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ АУДИО"), Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Select folder with audio files...", "Выберите папку с аудиофайлами...") << "\n";
    Console::flushInput();
    string folder = ShellDialogs::openFolderDialog(StringUtil::utf8ToWstring(TR("Select folder with audio files", "Выберите папку с аудиофайлами")).c_str());
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

    vector<string> audioExts = {".mp3", ".m4a", ".aac", ".wav", ".ogg", ".flac", ".opus", ".wma"};
    vector<string> files;
    error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        string ext = entry.path().extension().u8string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& ae : audioExts) {
            if (ext == ae) { files.push_back(entry.path().u8string()); break; }
        }
    }

    if (files.empty()) {
        Console::printColor(TR("[ERROR] No audio files found in the selected folder!", "[ОШИБКА] Аудиофайлы в выбранной папке не найдены!"), Console::Color::RED);
        Console::waitForKey();
        return;
    }

    string batchAudioCodec;
    if (!Dialogs::promptAudioCodecSettings(cfg, batchAudioCodec)) return;

    Console::clearScreen();
    Console::printColor("========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH AUDIO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ АУДИО"), Console::Color::CYAN);
    Console::printColor(" \"" + folder + "\"", Console::Color::CYAN);
    Console::printColor("========================================", Console::Color::CYAN);

    cout << "\n" << TR("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << TR("Settings:", "Настройки:") << "\n"
         << "  " << TR("Audio codec: ", "Аудиокодек: ") << FfmpegCommandBuilder::getAudioCodecSettingName(batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec) << "\n"
         << "  " << TR("Audio bitrate: ", "Битрейт аудио: ") << cfg.audioBitrate << " kbps\n";

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

    string targetExt = "mp3";
    string effCodec = batchAudioCodec.empty() ? cfg.audioCodec : batchAudioCodec;
    if (effCodec == "aac") targetExt = "m4a";
    else if (effCodec == "ac3") targetExt = "ac3";
    else if (effCodec == "eac3") targetExt = "eac3";
    else if (effCodec == "opus") targetExt = "opus";
    else if (effCodec == "flac") targetExt = "flac";

    int success = 0, fail = 0;
    vector<string> skippedFiles;

    for (size_t i = 0; i < files.size(); i++) {
        string inPath = files[i];
        string fileName = fs::u8path(inPath).filename().u8string();

        cout << "\n";
        Console::printColor("----------------------------------------", Console::Color::CYAN);
        Console::printColor(TR("Processing audio ", "Обработка аудио ") + to_string(i + 1) + "/" + to_string(files.size()) + ": " + fileName, Console::Color::CYAN);
        Console::printColor("----------------------------------------", Console::Color::CYAN);

        double duration = ctx.ffprobeService.getMediaDuration(inPath);
        string outPath = FfmpegCommandBuilder::buildOutputPath(inPath, cfg.outputPath, cfg.overwriteFiles, cfg.outputFormat, "_compressed", targetExt);
        auto ft = FfmpegCommandBuilder::prepareTarget(outPath, {inPath});

        string codecArgs = FfmpegCommandBuilder::getAudioCodecArgs(effCodec, cfg.audioBitrate, "");

        wstring cmd = L"\"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(ctx.installer.getFfmpegPath())) + L"\"";
        cmd += L" -i \"" + StringUtil::utf8ToWstring(PathProvider::getSafeFFmpegPath(inPath)) + L"\"";
        cmd += L" -vn " + StringUtil::utf8ToWstring(codecArgs);
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
                i--;
                continue;
            } else {
                skippedFiles.push_back(inPath);
                fail++;
                FfmpegCommandBuilder::finalizeTarget(ft, false);
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
    }

    Console::printColor("\n========================================", Console::Color::CYAN);
    Console::printColor(TR(" BATCH AUDIO PROCESSING COMPLETE", " ПАКЕТНАЯ ОБРАБОТКА АУДИО ЗАВЕРШЕНА"), Console::Color::CYAN);
    Console::printColor(TR(" Successfully: ", " Успешно: ") + to_string(success), Console::Color::GREEN);
    if (fail > 0) Console::printColor(TR(" Failed/Skipped: ", " С ошибками/Пропущено: ") + to_string(fail), Console::Color::RED);
    Console::printColor("========================================", Console::Color::CYAN);
    Console::waitForKey();
}

} // namespace operations
} // namespace mr
