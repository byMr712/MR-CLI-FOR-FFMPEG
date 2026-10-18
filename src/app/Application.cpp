#include "Application.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/WinLibs.hpp"
#include "../services/LocalizationService.hpp"
#include "../ui/Dialogs.hpp"
#include <iostream>
#include <shlobj.h>

using namespace std;
using namespace mr::core;
using namespace mr::domain;
using namespace mr::services;
using namespace mr::ui;

namespace mr {
namespace app {

static inline string TR(const string& en, const string& ru) {
    return LocalizationService::tr(en, ru);
}

Application::Application()
    : m_context(AppContext::instance()) {
}

Application::~Application() {
    shutdown();
}

bool Application::initialize() {
    if (m_initialized) return true;

    // Initialize COM for Windows Shell and Dialogs (STA model)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    Console::initConsole();
    Console::setConsoleTitle(L"MR CLI FOR FFMPEG v1.2.2");

    // Base directories setup in User Documents
    wchar_t docPath[MAX_PATH];
    string basePathStr;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
        basePathStr = StringUtil::wstringToUtf8(wstring(docPath) + L"\\MR-CLI-FOR-FFMPEG\\");
    } else {
        basePathStr = "C:\\MR-CLI-FOR-FFMPEG\\";
    }

    auto& cfg = m_context.config();
    cfg.configPath = basePathStr + "configs\\";
    cfg.outputPath = basePathStr + "output\\";

    PathProvider::createDirRecursive(cfg.configPath);
    PathProvider::createDirRecursive(cfg.outputPath);

    m_context.configService.loadConfig(cfg, cfg.configPath + "mr-config.txt");
    LocalizationService::setLanguage(cfg.currentLang);

    m_context.installer.setBaseDir(cfg.configPath);
    m_context.refreshPaths();

    if (!m_context.installer.checkOrInstallComponents()) {
        Console::printColor(TR("[ERROR] FFmpeg is missing and could not be installed!",
                               "[ОШИБКА] FFmpeg отсутствует и не может быть установлен!"), Console::Color::RED);
        Console::waitForKey();
        return false;
    }

    m_context.refreshPaths();
    m_context.hardware.detectAll();

    m_initialized = true;
    return true;
}

void Application::shutdown() {
    if (!m_initialized) return;
    Console::cleanupConsole();
    CoUninitialize();
    m_initialized = false;
}

char Application::showMainMenu() {
    auto& cfg = m_context.config();
    bool ffmpegFound = m_context.installer.isFfmpegFound();
    bool ffprobeFound = m_context.installer.isFfprobeFound();

    vector<string> opts = {
        "--- " + TR("VIDEO OPERATIONS", "ВИДЕО ОПЕРАЦИИ") + " ---",
        " 1. " + TR("Convert video", "Конвертировать видео"),
        " 2. " + TR("Trim / Cut video", "Обрезать видео"),
        " 3. " + TR("Change resolution", "Изменить разрешение"),
        " 4. " + TR("Change speed", "Изменить скорость"),
        " 5. " + TR("Rotate / Flip", "Повернуть / Отразить"),
        " 6. " + TR("Compress video", "Сжать видео"),
        " 7. " + TR("Add watermark", "Добавить водяной знак"),
        " 8. " + TR("Add subtitles", "Добавить субтитры"),
        "--- " + TR("AUDIO OPERATIONS", "АУДИО ОПЕРАЦИИ") + " ---",
        " 9. " + TR("Extract audio", "Извлечь аудио"),
        " a. " + TR("Merge video + audio", "Объединить видео + аудио"),
        " b. " + TR("Strip audio (mute)", "Удалить звук"),
        "--- " + TR("OTHER", "ДРУГОЕ") + " ---",
        " c. " + TR("Concatenate (join) files", "Склеить файлы"),
        " d. " + TR("Create GIF", "Создать GIF"),
        " e. " + TR("Extract frames", "Извлечь кадры"),
        " f. " + TR("File information", "Информация о файле"),
        " g. " + TR("Compare two files", "Сравнить два файла"),
        " h. " + TR("Batch convert video", "Пакетное конвертирование видео"),
        " i. " + TR("Batch convert audio", "Пакетное конвертирование аудио"),
        "--- " + TR("PROGRAM", "ПРОГРАММА") + " ---",
        " s. " + TR("Settings", "Настройки"),
        " l. " + string(cfg.currentLang == Language::EN ? "Language: English" : "Язык: Русский"),
        " 0. " + TR("Exit (ESC)", "Выход (ESC)"),
    };

    vector<int> actions = {
        -1,
        '1','2','3','4','5','6','7','8',
        -1,
        '9','a','b',
        -1,
        'c','d','e','f','g','h','i',
        -1,
        's','l','0'
    };

    int selected = 1;
    while (true) {
        Console::clearScreen();
        Console::printColor("========================================", Console::Color::CYAN);
        Console::printColor(" MR CLI FOR FFMPEG v1.2.2", Console::Color::CYAN);
        Console::printColor("========================================", Console::Color::CYAN);
        Console::printColor("========================================", Console::Color::GREEN);
        Console::printColor(" FFMPEG:  " + string(ffmpegFound ? TR("[OK] installed", "[OK] установлен") : TR("[ERROR] not found", "[ОШИБКА] не найден")), ffmpegFound ? Console::Color::GREEN : Console::Color::RED);
        Console::printColor(" FFPROBE: " + string(ffprobeFound ? TR("[OK] installed", "[OK] установлен") : TR("[WARNING] not installed", "[ВНИМАНИЕ] не установлен")), ffprobeFound ? Console::Color::GREEN : Console::Color::YELLOW);
        string hwInfo = m_context.hardware.getAccelerationHardwareInfo(cfg.accelMode, cfg.hybridGpuChoice);
        Console::printColor(" " + TR("Hardware: ", "Железо: ") + hwInfo, Console::Color::GREEN);
        Console::printColor("========================================", Console::Color::GREEN);
        cout << "========================================\n";

        for (size_t i = 0; i < opts.size(); i++) {
            if (actions[i] == -1) {
                cout << opts[i] << "\n";
            } else if ((int)i == selected) {
                Console::setColor(Console::Color::GREEN);
                cout << " > " << opts[i] << "\n";
                Console::setColor(Console::Color::WHITE);
            } else {
                cout << "   " << opts[i] << "\n";
            }
        }
        cout << "========================================\n";
        cout << "\n" << TR("Arrow keys to select, Enter to confirm, ESC or 0 to exit",
                           "Стрелки для выбора, Enter для подтверждения, ESC или 0 для выхода") << "\n";

        wint_t key = Console::getWChar();
        if (key == 27 || key == '0') return '0';
        if (key == 13) return (char)actions[selected];
        if (key == 0 || key == 0xE0) {
            wint_t scan = Console::getWChar();
            if (scan == 72) {
                do { selected = (selected > 0) ? selected - 1 : (int)opts.size() - 1; } while (actions[selected] == -1);
            } else if (scan == 80) {
                do { selected = (selected < (int)opts.size() - 1) ? selected + 1 : 0; } while (actions[selected] == -1);
            }
        } else {
            char ch = StringUtil::normalizeKeyToEnglish(key);
            for (size_t i = 0; i < opts.size(); i++) {
                if (actions[i] == -1) continue;
                if (opts[i].length() >= 2 && opts[i][0] == ' ' && opts[i][1] == ch) {
                    return (char)actions[i];
                }
                if (opts[i].length() >= 1 && opts[i][0] == ch) {
                    return (char)actions[i];
                }
            }
        }
    }
}

int Application::run() {
    if (!initialize()) return 1;

    auto& cfg = m_context.config();
    while (true) {
        char ch = showMainMenu();
        if (ch == 27 || ch == '0') {
            cout << TR("Exiting...", "Выход...") << "\n";
            break;
        }

        m_context.ffprobeService.clearCache();

        if (ch == 'l') {
            cfg.currentLang = (cfg.currentLang == Language::EN) ? Language::RU : Language::EN;
            LocalizationService::setLanguage(cfg.currentLang);
            m_context.configService.saveConfig(cfg);
            continue;
        }

        string opId(1, ch);
        if (!m_registry.executeOperation(opId)) {
            Console::printColor(TR("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), Console::Color::RED);
            Console::waitForKey();
        }
    }

    shutdown();
    return 0;
}

} // namespace app
} // namespace mr
