#include "ComponentInstaller.hpp"
#include "LocalizationService.hpp"
#include "../core/BitsDownloader.hpp"
#include "../core/ZipInflate.hpp"
#include "../core/PathProvider.hpp"
#include "../core/Console.hpp"
#include "../core/StringUtil.hpp"
#include <filesystem>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

namespace mr {
namespace services {

ComponentInstaller::ComponentInstaller(const std::string& configDir)
    : m_configDir(configDir) {
    if (m_configDir.empty()) {
        m_configDir = core::PathProvider::getDefaultConfigDir();
    }
}

void ComponentInstaller::organizeExtractedTool(const std::string& targetExe, const std::string& targetDirStr) {
    fs::path targetDir = fs::u8path(targetDirStr);
    std::error_code ec;

    fs::path foundBinDir;
    for (const auto& entry : fs::recursive_directory_iterator(targetDir, fs::directory_options::skip_permission_denied, ec)) {
        if (!ec && entry.is_regular_file(ec)) {
            if (entry.path().filename().u8string() == targetExe) {
                if (entry.path().parent_path() != targetDir) {
                    foundBinDir = entry.path().parent_path();
                    break;
                }
            }
        }
    }

    if (foundBinDir.empty()) return;

    for (const auto& entry : fs::directory_iterator(foundBinDir, ec)) {
        if (!ec && entry.is_regular_file(ec)) {
            fs::copy_file(entry.path(), targetDir / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
    }

    for (const auto& entry : fs::directory_iterator(targetDir, ec)) {
        if (!ec && entry.is_directory(ec)) {
            fs::remove_all(entry.path(), ec);
        }
    }
}

bool ComponentInstaller::checkAndCopyFromPeer() {
    std::string peerConfigs = core::PathProvider::getPeerAppConfigDir();
    if (peerConfigs.empty() || !core::PathProvider::fileExists(peerConfigs + "ffmpeg.exe")) {
        return false;
    }

    std::string otherAppName = "MR CLI FOR YT-DLP";
    core::Console::print("========================================", core::ConsoleColor::CYAN);
    core::Console::print(" " + tr("FOUND FFMPEG", "ОБНАРУЖЕН FFMPEG") + "\n", core::ConsoleColor::CYAN);
    core::Console::print("========================================\n", core::ConsoleColor::CYAN);

    std::cout << tr(
        "The program just detected that you also use " + otherAppName + ".\n"
        "It already has FFmpeg installed.\n"
        "Would you like to use a shared copy to avoid downloading again?\n\n",
        "Программа только что обнаружила, что вы также используете " + otherAppName + ".\n"
        "У неё уже установлен FFmpeg.\n"
        "Хотите использовать общую копию, чтобы не скачивать повторно?\n\n"
    );

    std::cout << "1. " << tr("Yes, use copy (fast and offline)", "Да, использовать копию (быстро и без интернета)") << "\n"
              << "2. " << tr("No, download anew", "Нет, скачать заново") << "\n\n"
              << tr("Your choice: ", "Ваш выбор: ") << std::flush;

    char ch = core::Console::getMenuChoice();
    if (ch != '1' && ch != 13) {
        return false;
    }

    core::PathProvider::ensureDirExists(m_configDir);

    core::Console::clearScreen();
    core::Console::print("========================================", core::ConsoleColor::CYAN);
    core::Console::print(" " + tr("COPYING FFMPEG FROM ", "КОПИРОВАНИЕ FFMPEG ИЗ ") + otherAppName + "\n", core::ConsoleColor::CYAN);
    core::Console::print("========================================\n\n", core::ConsoleColor::CYAN);
    core::Console::print(tr("[INFO] Copying FFmpeg components...\n", "[ИНФО] Копирование компонентов FFmpeg...\n"), core::ConsoleColor::CYAN);

    std::vector<std::string> filesToCopy = {"ffmpeg.exe", "ffprobe.exe", "ffplay.exe"};
    bool copiedAny = false;
    for (const auto& f : filesToCopy) {
        std::string src = peerConfigs + f;
        std::string dst = m_configDir + f;
        if (core::PathProvider::fileExists(src)) {
            std::wstring wSrc = core::StringUtil::utf8ToWstring(src);
            std::wstring wDst = core::StringUtil::utf8ToWstring(dst);
            if (CopyFileW(wSrc.c_str(), wDst.c_str(), FALSE)) {
                copiedAny = true;
            }
        }
    }

    if (copiedAny && core::PathProvider::fileExists(m_configDir + "ffmpeg.exe")) {
        m_ffmpegFound = true;
        m_ffmpegPath = m_configDir + "ffmpeg.exe";
        m_ffprobeFound = core::PathProvider::fileExists(m_configDir + "ffprobe.exe");
        if (m_ffprobeFound) {
            m_ffprobePath = m_configDir + "ffprobe.exe";
        }
        std::cout << "\n";
        core::Console::print(tr("[OK] FFmpeg successfully copied!\n", "[OK] FFmpeg успешно скопирован!\n"), core::ConsoleColor::GREEN);
        Sleep(1200);
        return true;
    }

    return false;
}

bool ComponentInstaller::downloadAndInstall() {
    core::PathProvider::ensureDirExists(m_configDir);

    core::Console::print("\n" + tr("[INFO] Installing FFmpeg (~160MB)...\n", "[ИНФО] Установка FFmpeg (~160MB)...\n"), core::ConsoleColor::CYAN);
    std::string zipFile = m_configDir + "ffmpeg.zip";
    const std::string url = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";

    if (core::BitsDownloader::download(url, zipFile, "FFmpeg")) {
        if (core::ZipInflate::extract(zipFile, m_configDir, "FFmpeg",
                                      tr("Extracting components...", "Распаковка компонентов..."))) {
            organizeExtractedTool("ffmpeg.exe", m_configDir);
            m_ffmpegFound = core::PathProvider::fileExists(m_configDir + "ffmpeg.exe");
            m_ffprobeFound = core::PathProvider::fileExists(m_configDir + "ffprobe.exe");
            if (m_ffmpegFound) {
                m_ffmpegPath = m_configDir + "ffmpeg.exe";
                m_ffprobePath = m_configDir + "ffprobe.exe";
                std::cout << "\n";
                core::Console::print(tr("[OK] FFmpeg and FFprobe installed successfully!\n", "[OK] FFmpeg и FFprobe успешно установлены!\n"), core::ConsoleColor::GREEN);
            } else {
                std::cout << "\n";
                core::Console::print(tr("[ERROR] Failed to locate ffmpeg.exe after extraction!\n", "[ОШИБКА] Не удалось найти ffmpeg.exe после распаковки!\n"), core::ConsoleColor::RED);
            }
        } else {
            std::cout << "\n";
            core::Console::print(tr("[ERROR] Failed to extract FFmpeg archive!\n", "[ОШИБКА] Ошибка распаковки архива FFmpeg!\n"), core::ConsoleColor::RED);
        }

        std::error_code ec;
        fs::remove(fs::u8path(zipFile), ec);
    } else {
        core::Console::print(tr("[ERROR] Failed to download FFmpeg!\n", "[ОШИБКА] Не удалось скачать FFmpeg!\n"), core::ConsoleColor::RED);
    }

    return m_ffmpegFound;
}

bool ComponentInstaller::checkDependencies() {
    m_ffmpegPath = m_configDir + "ffmpeg.exe";
    m_ffprobePath = m_configDir + "ffprobe.exe";

    m_ffmpegFound = core::PathProvider::fileExists(m_ffmpegPath);
    m_ffprobeFound = core::PathProvider::fileExists(m_ffprobePath);

    if (!m_ffmpegFound) {
        if (checkAndCopyFromPeer()) {
            m_ffmpegFound = core::PathProvider::fileExists(m_ffmpegPath);
            m_ffprobeFound = core::PathProvider::fileExists(m_ffprobePath);
        }
    }

    if (!m_ffmpegFound) {
        core::Console::print("========================================", core::ConsoleColor::RED);
        core::Console::print(" " + tr("[ERROR] FFmpeg not found!", "[ОШИБКА] FFmpeg не найден!") + "\n", core::ConsoleColor::RED);
        core::Console::print("========================================\n", core::ConsoleColor::RED);

        core::Console::print("\n========================================", core::ConsoleColor::CYAN);
        core::Console::print(" " + tr("AUTO INSTALLER", "АВТОУСТАНОВЩИК") + "\n", core::ConsoleColor::CYAN);
        core::Console::print("========================================\n", core::ConsoleColor::CYAN);

        while (true) {
            core::Console::print("\n" + tr("Install FFmpeg automatically? (y/n): \n", "Установить FFmpeg автоматически? (y/n): \n"), core::ConsoleColor::CYAN);

            char ch = core::Console::getMenuChoice();
            if (ch == 'y' || ch == 'Y') {
                std::cout << "y" << std::endl;
                break;
            } else if (ch == 'n' || ch == 'N' || ch == 27) {
                std::cout << "n" << std::endl;
                core::Console::print("\n========================================", core::ConsoleColor::YELLOW);
                core::Console::print(" " + tr("[ERROR] FFmpeg is required to run the application!", "[ОШИБКА] FFmpeg необходим для работы программы!") + "\n", core::ConsoleColor::RED);
                core::Console::print(" " + tr("[INFO] Download from: https://github.com/BtbN/FFmpeg-Builds/releases", "[ИНФО] Скачайте с: https://github.com/BtbN/FFmpeg-Builds/releases") + "\n", core::ConsoleColor::YELLOW);
                core::Console::print(" " + tr("[INFO] Place 'ffmpeg.exe' and 'ffprobe.exe' in: ", "[ИНФО] Поместите 'ffmpeg.exe' и 'ffprobe.exe' в: ") + m_configDir + "\n", core::ConsoleColor::YELLOW);
                core::Console::print("========================================\n", core::ConsoleColor::YELLOW);
                core::Console::waitForKey();
                return false;
            }
        }

        return downloadAndInstall();
    }

    return true;
}

bool ComponentInstaller::updateComponents() {
    core::Console::clearScreen();
    core::Console::print("========================================", core::ConsoleColor::CYAN);
    core::Console::print(" " + tr("COMPONENT UPDATER", "ОБНОВЛЕНИЕ КОМПОНЕНТОВ") + "\n", core::ConsoleColor::CYAN);
    core::Console::print("========================================\n", core::ConsoleColor::CYAN);
    std::cout << "\n1. " << tr("Re-download FFmpeg + FFprobe", "Переустановить FFmpeg + FFprobe")
              << "\n0. " << tr("Return (ESC)", "Назад (ESC)") << "\n\n" << tr("Your choice: ", "Ваш выбор: ") << std::flush;

    char ch = core::Console::getMenuChoice();
    if (ch == 27 || ch == '0') {
        return false;
    }

    if (ch == '1') {
        core::Console::clearScreen();
        core::Console::print("========================================", core::ConsoleColor::CYAN);
        core::Console::print(" " + tr("Downloading latest FFmpeg (~160MB)...", "Загрузка последней версии FFmpeg (~160МБ)...") + "\n", core::ConsoleColor::CYAN);
        core::Console::print("========================================\n", core::ConsoleColor::CYAN);

        bool ok = downloadAndInstall();
        core::Console::waitForKey();
        return ok;
    }
    return false;
}

} // namespace services
} // namespace mr
