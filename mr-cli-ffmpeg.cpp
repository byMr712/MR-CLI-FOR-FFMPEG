#include <iostream>
#include <string>
#include <cstdlib>
#include <conio.h>
#include <windows.h>
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <shlobj.h>
#include <commdlg.h>
#include <locale>
#include <cctype>
#include <filesystem>
#include <wininet.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Wininet.lib")
#pragma comment(lib, "User32.lib")

using namespace std;
namespace fs = std::filesystem;

// ========== CONFIG & CONSTANTS ==========
string FFMPEG_PATH, FFPROBE_PATH, CONFIG_PATH, OUTPUT_PATH;
string OUTPUT_FORMAT = "MP4(H.264)";
string OUTPUT_RESOLUTION = "original";
string OUTPUT_FPS = "original";
string AUDIO_BITRATE = "192";
string VIDEO_BITRATE = "auto";
string CRF_VALUE = "23";
string PRESET = "medium";
string WATERMARK_PATH = "";
string WATERMARK_POSITION = "bottom-right";
bool FFMPEG_FOUND = false, FFPROBE_FOUND = false;
bool OVERWRITE_FILES = true;
bool HARDWARE_ACCEL = false;
bool KEEP_METADATA = true;

// ========== UTF-8 HELPERS ==========
string wstringToUtf8(const wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 1) return "";
    string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
    return result;
}

wstring utf8ToWstring(const string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (size <= 0) return L"";
    wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

// ========== COLORS ==========
enum Color { BLACK = 0, BLUE = 1, GREEN = 2, RED = 4, YELLOW = 6, WHITE = 7, CYAN = 11 };

void setColor(int c) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (WORD)c);
}

void printColor(const string& t, int c = WHITE, bool nl = true) {
    setColor(c);
    cout << t;
    setColor(WHITE);
    if (nl) cout << endl;
}

// ========== CONSOLE & SCREEN HELPERS ==========
void setUTF8() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    SetConsoleTitleW(L"MR CLI FOR FFMPEG v1.0.0");
}

void clearScreen() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        cout << "\033[2J\033[H" << flush;
        return;
    }
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
        cout << "\033[2J\033[H" << flush;
        return;
    }
    DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD count = 0;
    COORD homeCoords = { 0, 0 };
    FillConsoleOutputCharacterW(hOut, (WCHAR)' ', cellCount, homeCoords, &count);
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, cellCount, homeCoords, &count);
    SetConsoleCursorPosition(hOut, homeCoords);
}

void waitForKey() {
    cout << "\nPress any key...";
    (void)_getch();
    while (_kbhit()) (void)_getch();
    cout << "\n";
}

char getMenuChoice() {
    while (_kbhit()) {
        (void)_getch();
    }
    char c = _getch();
    if (c == 27) return 27; // ESC
    if (c >= 'A' && c <= 'Z') {
        c += 32;
    }
    return c;
}

// ========== CLIPBOARD HELPERS ==========
string getClipboard() {
    if (!OpenClipboard(NULL)) return "";

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return "";
    }

    wchar_t* t = (wchar_t*)GlobalLock(h);
    if (!t) {
        CloseClipboard();
        return "";
    }

    wstring r(t);
    GlobalUnlock(h);
    CloseClipboard();
    return wstringToUtf8(r);
}

// ========== KEYBOARD INPUT (WITH ESCAPE TO CANCEL, CTRL+V, BACKSPACE) ==========
bool inputLineWithEscape(string& result, const string& prompt) {
    if (!prompt.empty()) cout << prompt << flush;
    result.clear();

    wstring buffer;

    while (true) {
        wint_t wc = _getwch();

        if (wc == 27) { // ESC key
            cout << "\n";
            return false;
        }

        if (wc == 13) { // Enter (\r)
            cout << "\n";
            break;
        }

        if (wc == 8) { // Backspace
            if (!buffer.empty()) {
                buffer.pop_back();
                cout << "\b \b" << flush;
            }
            continue;
        }

        if (wc == 22) { // Ctrl+V (Paste)
            string clip = getClipboard();
            while (!clip.empty() && (clip.back() == '\r' || clip.back() == '\n')) clip.pop_back();
            if (!clip.empty()) {
                wstring wclip = utf8ToWstring(clip);
                buffer += wclip;
                cout << clip << flush;
            }
            continue;
        }

        if (wc == 3) { // Ctrl+C
            cout << "\n";
            return false;
        }

        if (wc == 0 || wc == 0xE0) { // Extended keys (arrows, F-keys, etc.)
            (void)_getwch(); // consume the secondary scan code
            continue;
        }

        if (wc >= 32) {
            buffer.push_back((wchar_t)wc);
            cout << wstringToUtf8(wstring(1, (wchar_t)wc)) << flush;
        }
    }

    result = wstringToUtf8(buffer);
    if (result == "0") {
        return false;
    }
    return true;
}

// ========== FILE & DIRECTORY HELPERS ==========
bool fileExists(const string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(p), ec);
}

bool dirExists(const string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::is_directory(fs::u8path(p), ec);
}

bool createDirRecursive(const string& p) {
    if (p.empty()) return false;
    std::error_code ec;
    return fs::create_directories(fs::u8path(p), ec) || dirExists(p);
}

int runProcessWait(const wstring& cmdLine) {
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wstring mutableCmd = cmdLine;
    if (!CreateProcessW(NULL, &mutableCmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}

int execCmd(const string& cmd) {
    return runProcessWait(utf8ToWstring(cmd));
}

string findFileRecursive(const string& dir, const string& f) {
    if (dir.empty() || !dirExists(dir)) return "";
    try {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(fs::u8path(dir), fs::directory_options::skip_permission_denied, ec)) {
            if (!ec && entry.is_regular_file(ec)) {
                if (entry.path().filename().u8string() == f || entry.path().filename().string() == f) {
                    return entry.path().u8string();
                }
            }
        }
    } catch (...) {}
    return "";
}

// ========== NATIVE DOWNLOADER & ZIP EXTRACTOR ==========
void printComponentProgress(const string& label, double percent, const string& extraInfo = "") {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int barWidth = 25;
    int pos = (int)(barWidth * percent / 100.0);

    string line = "\r";
    if (!label.empty()) {
        line += "[" + label + "] ";
    }
    line += "[";
    for (int i = 0; i < barWidth; i++) {
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
    cout << line << flush;
}

bool downloadFile(const string& url, const string& destFile, const string& label = "") {
    HINTERNET hInternet = InternetOpenW(L"MR-CLI-FOR-FFMPEG/1.0.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_SECURE;
    wstring wUrl = utf8ToWstring(url);
    HINTERNET hUrl = InternetOpenUrlW(hInternet, wUrl.c_str(), NULL, 0, flags, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return false;
    }

    // Query Content-Length
    DWORD contentLength = 0;
    DWORD clLen = sizeof(contentLength);
    DWORD index = 0;
    HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &clLen, &index);

    ofstream outFile(fs::u8path(destFile), ios::binary);
    if (!outFile.is_open()) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        return false;
    }

    DWORD bytesRead = 0;
    DWORD totalDownloaded = 0;
    char buffer[65536];

    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        outFile.write(buffer, bytesRead);
        totalDownloaded += bytesRead;
        if (contentLength > 0) {
            double pct = ((double)totalDownloaded / (double)contentLength) * 100.0;
            double curMB = (double)totalDownloaded / (1024.0 * 1024.0);
            double totMB = (double)contentLength / (1024.0 * 1024.0);
            char info[64];
            snprintf(info, sizeof(info), "(%.1f / %.1f MB)", curMB, totMB);
            printComponentProgress(label, pct, info);
        } else {
            double curMB = (double)totalDownloaded / (1024.0 * 1024.0);
            char info[64];
            snprintf(info, sizeof(info), "(%.1f MB)", curMB);
            printComponentProgress(label, 0, info);
        }
    }

    outFile.close();
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    cout << "\n";
    return fileExists(destFile) && (totalDownloaded > 1000);
}

bool extractZip(const string& zipPath, const string& destDir) {
    if (!fileExists(zipPath) || !dirExists(destDir)) return false;

    // 1. Fast native Windows tar.exe (System32 bsdtar on Windows 10/11)
    wchar_t sysDir[MAX_PATH];
    if (GetSystemDirectoryW(sysDir, MAX_PATH) > 0) {
        wstring tarExe = wstring(sysDir) + L"\\tar.exe";
        if (GetFileAttributesW(tarExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            wstring wDest = utf8ToWstring(destDir);
            while (!wDest.empty() && (wDest.back() == L'\\' || wDest.back() == L'/')) wDest.pop_back();

            wstring wZip = utf8ToWstring(zipPath);
            while (!wZip.empty() && (wZip.back() == L'\\' || wZip.back() == L'/')) wZip.pop_back();

            wstring cmd = L"\"" + tarExe + L"\" -xf \"" + wZip + L"\" -C \"" + wDest + L"\"";
            if (runProcessWait(cmd) == 0) {
                return true;
            }
        }
    }

    // 2. Fallback to PowerShell Expand-Archive
    wstring wDest = utf8ToWstring(destDir);
    wstring wZip = utf8ToWstring(zipPath);
    wstring psCmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath '" + wZip + L"' -DestinationPath '" + wDest + L"' -Force\"";
    return (runProcessWait(psCmd) == 0);
}

void organizeExtractedTool(const string& targetExe, const string& destDir) {
    std::error_code ec;
    fs::path targetDir = fs::weakly_canonical(fs::u8path(destDir), ec);
    if (ec || targetDir.empty()) {
        targetDir = fs::u8path(destDir);
        while (targetDir.has_filename() && targetDir.filename().empty()) {
            targetDir = targetDir.parent_path();
        }
    }

    // 1. Search for targetExe in subdirectories of targetDir
    fs::path foundExePath;
    for (const auto& entry : fs::recursive_directory_iterator(targetDir, fs::directory_options::skip_permission_denied, ec)) {
        if (!ec && entry.is_regular_file(ec)) {
            if (entry.path().filename().u8string() == targetExe || entry.path().filename().string() == targetExe) {
                if (entry.path().parent_path() != targetDir) {
                    foundExePath = entry.path();
                    break;
                }
            }
        }
    }

    if (foundExePath.empty()) {
        return;
    }

    fs::path binDir = foundExePath.parent_path();

    // 2. Copy all .exe / tool files from binDir into targetDir
    for (const auto& entry : fs::directory_iterator(binDir, ec)) {
        if (!ec && entry.is_regular_file(ec)) {
            fs::copy_file(entry.path(), targetDir / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
    }

    // 3. Delete any extracted subdirectories inside targetDir
    for (const auto& entry : fs::directory_iterator(targetDir, ec)) {
        if (!ec && entry.is_directory(ec)) {
            fs::remove_all(entry.path(), ec);
        }
    }
}

// ========== PROGRESS PARSER & BAR FOR FFMPEG ==========
bool parseFFmpegProgress(const string& line, string& timeStr, string& speed) {
    timeStr.clear();
    speed.clear();

    // FFmpeg outputs lines like: "frame= 100 fps= 30 ... time=00:01:23.45 ... speed=2.5x"
    size_t tPos = line.find("time=");
    if (tPos == string::npos) return false;

    size_t tEnd = line.find(' ', tPos + 5);
    if (tEnd == string::npos) tEnd = line.size();
    timeStr = line.substr(tPos + 5, tEnd - (tPos + 5));

    size_t sPos = line.find("speed=");
    if (sPos != string::npos) {
        size_t sEnd = line.find(' ', sPos + 6);
        if (sEnd == string::npos) sEnd = line.size();
        speed = line.substr(sPos + 6, sEnd - (sPos + 6));
    }

    return !timeStr.empty();
}

double timeToSeconds(const string& timeStr) {
    // Parse HH:MM:SS.ms format
    int h = 0, m = 0;
    double s = 0;
    if (sscanf_s(timeStr.c_str(), "%d:%d:%lf", &h, &m, &s) >= 3) {
        return h * 3600.0 + m * 60.0 + s;
    }
    return 0;
}

void printFFmpegProgressBar(double currentSec, double totalSec, const string& speed) {
    double percent = 0;
    if (totalSec > 0) {
        percent = (currentSec / totalSec) * 100.0;
        if (percent > 100) percent = 100;
    }
    const int barWidth = 30;
    int pos = (int)(barWidth * percent / 100.0);
    string line = "\r[";
    for (int i = 0; i < barWidth; i++) line += (i < pos) ? '=' : (i == pos ? '>' : ' ');

    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%3.0f%%", percent);
    line += "] ";
    line += pctBuf;

    if (!speed.empty()) line += "  Speed: " + speed;

    // Show current time
    int cm = (int)(currentSec / 60);
    int cs = (int)currentSec % 60;
    int tm = (int)(totalSec / 60);
    int ts = (int)totalSec % 60;
    char timeBuf[64];
    snprintf(timeBuf, sizeof(timeBuf), "  [%02d:%02d/%02d:%02d]", cm, cs, tm, ts);
    line += timeBuf;
    line += "        ";
    cout << line << flush;
}

// ========== GET MEDIA DURATION VIA FFPROBE ==========
double getMediaDuration(const string& filePath) {
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return 0;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + filePath + "\"";

    // Create pipe to read output
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return 0;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wstring wcmd = utf8ToWstring(cmd);
    if (!CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return 0;
    }
    CloseHandle(hWritePipe);

    string output;
    char buf[256];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Parse duration
    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) output.pop_back();
    try {
        return stod(output);
    } catch (...) {
        return 0;
    }
}

// ========== GET MEDIA INFO VIA FFPROBE ==========
string getMediaInfo(const string& filePath) {
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return "[ffprobe not available]";

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_format -show_streams -of default \"" + filePath + "\"";

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wstring wcmd = utf8ToWstring(cmd);
    if (!CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "";
    }
    CloseHandle(hWritePipe);

    string output;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return output;
}

// ========== FORMAT MEDIA INFO FOR DISPLAY ==========
string formatMediaInfoDisplay(const string& rawInfo) {
    if (rawInfo.empty()) return "[No info available]";

    string result;
    string codec_name, codec_type, width, height, duration, bit_rate, r_frame_rate, sample_rate, channels;
    string fmt_name, fmt_duration, fmt_size, fmt_bit_rate;

    istringstream iss(rawInfo);
    string line;
    bool inStream = false;
    int streamIdx = 0;

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();

        if (line == "[STREAM]") { inStream = true; streamIdx++; continue; }
        if (line == "[/STREAM]") {
            if (!codec_name.empty()) {
                result += "  Stream #" + to_string(streamIdx) + " [" + codec_type + "]: " + codec_name;
                if (!width.empty() && !height.empty()) result += " " + width + "x" + height;
                if (!r_frame_rate.empty() && codec_type == "video") result += " @ " + r_frame_rate + " fps";
                if (!sample_rate.empty() && codec_type == "audio") result += " " + sample_rate + " Hz";
                if (!channels.empty() && codec_type == "audio") result += " " + channels + "ch";
                result += "\n";
            }
            codec_name.clear(); codec_type.clear(); width.clear(); height.clear();
            r_frame_rate.clear(); sample_rate.clear(); channels.clear();
            inStream = false;
            continue;
        }
        if (line == "[FORMAT]" || line == "[/FORMAT]") continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (inStream) {
            if (key == "codec_name") codec_name = val;
            else if (key == "codec_type") codec_type = val;
            else if (key == "width") width = val;
            else if (key == "height") height = val;
            else if (key == "r_frame_rate") r_frame_rate = val;
            else if (key == "sample_rate") sample_rate = val;
            else if (key == "channels") channels = val;
        } else {
            if (key == "format_name") fmt_name = val;
            else if (key == "duration") fmt_duration = val;
            else if (key == "size") fmt_size = val;
            else if (key == "bit_rate") fmt_bit_rate = val;
        }
    }

    if (!fmt_name.empty()) result += "  Format: " + fmt_name + "\n";
    if (!fmt_duration.empty()) {
        try {
            double dur = stod(fmt_duration);
            int h = (int)(dur / 3600);
            int m = (int)((dur - h * 3600) / 60);
            int s = (int)(dur) % 60;
            char buf[64];
            snprintf(buf, sizeof(buf), "  Duration: %02d:%02d:%02d (%.1f sec)", h, m, s, dur);
            result += buf;
            result += "\n";
        } catch (...) {}
    }
    if (!fmt_size.empty()) {
        try {
            double sz = stod(fmt_size);
            char buf[64];
            if (sz > 1024 * 1024 * 1024) snprintf(buf, sizeof(buf), "  Size: %.2f GB", sz / (1024.0 * 1024.0 * 1024.0));
            else if (sz > 1024 * 1024) snprintf(buf, sizeof(buf), "  Size: %.2f MB", sz / (1024.0 * 1024.0));
            else snprintf(buf, sizeof(buf), "  Size: %.2f KB", sz / 1024.0);
            result += buf;
            result += "\n";
        } catch (...) {}
    }
    if (!fmt_bit_rate.empty()) {
        try {
            double br = stod(fmt_bit_rate);
            char buf[64];
            snprintf(buf, sizeof(buf), "  Bitrate: %.0f kbps", br / 1000.0);
            result += buf;
            result += "\n";
        } catch (...) {}
    }

    return result;
}

// ========== EXECUTE FFMPEG WITH LIVE PROGRESS ==========
bool execFFmpegWithProgress(const wstring& cmdLine, double totalDuration = 0) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        printColor("[ERROR] Failed to create process pipe!", RED);
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    wstring mutableCmd = cmdLine;
    BOOL success = CreateProcessW(
        NULL,
        &mutableCmd[0],
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hWritePipe);

    if (!success) {
        CloseHandle(hReadPipe);
        printColor("[ERROR] Failed to launch FFmpeg! Error code: " + to_string(GetLastError()), RED);
        return false;
    }

    string line;
    bool progressActive = false;
    bool hasError = false;
    string errorMsg;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (true) {
        if (_kbhit()) {
            int key = _getch();
            if (key == 27) { // ESC
                if (progressActive) { cout << endl; progressActive = false; }
                printColor("\n[INFO] Operation cancelled by user (ESC).", YELLOW);
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }

        DWORD bytesAvail = 0;
        if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytesAvail, NULL)) {
            break;
        }

        if (bytesAvail == 0) {
            DWORD waitRes = WaitForSingleObject(pi.hProcess, 50);
            if (waitRes == WAIT_OBJECT_0) {
                if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytesAvail, NULL) || bytesAvail == 0) {
                    break;
                }
            }
            continue;
        }

        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        for (DWORD i = 0; i < bytesRead; i++) {
            char c = buffer[i];
            if (c == '\r' || c == '\n') {
                if (!line.empty()) {
                    string timeStr, speed;
                    if (parseFFmpegProgress(line, timeStr, speed)) {
                        double currentSec = timeToSeconds(timeStr);
                        printFFmpegProgressBar(currentSec, totalDuration, speed);
                        progressActive = true;
                    }
                    else if (line.find("Error") != string::npos || line.find("error") != string::npos ||
                             line.find("Invalid") != string::npos || line.find("No such file") != string::npos) {
                        if (line.find("encoder") == string::npos || line.find("Error") != string::npos) {
                            hasError = true;
                            errorMsg = line;
                            if (progressActive) { cout << endl; progressActive = false; }
                            printColor("[ERROR] " + line, RED);
                        }
                    }
                    else if (line.find("Overwrite") != string::npos) {
                        // Silently handled via -y flag
                    }
                    line.clear();
                }
            }
            else {
                line += c;
            }
        }
    }

    // Process any remaining line
    if (!line.empty()) {
        string timeStr, speed;
        if (parseFFmpegProgress(line, timeStr, speed)) {
            double currentSec = timeToSeconds(timeStr);
            printFFmpegProgressBar(currentSec, totalDuration, speed);
            progressActive = true;
        }
    }

    if (progressActive) cout << endl;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

// ========== DIALOGS ==========
string openFolderDialog() {
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = L"Select folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return "";

    wchar_t p[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, p)) {
        CoTaskMemFree(pidl);
        wstring wp(p);
        if (wp.back() != L'\\' && wp.back() != L'/') wp += L'\\';
        return wstringToUtf8(wp);
    }
    CoTaskMemFree(pidl);
    return "";
}

string openFileDialogMedia(const wchar_t* title = L"Select media file") {
    OPENFILENAMEW ofn = { 0 };
    wchar_t fn[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = L"Media Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff\0"
                      L"Video Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v\0"
                      L"Audio Files\0*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma\0"
                      L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff\0"
                      L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;

    if (GetOpenFileNameW(&ofn)) {
        return wstringToUtf8(wstring(fn));
    }
    return "";
}

string openMultiFileDialog(vector<string>& files) {
    // For concat - uses manual multi-input
    files.clear();
    return "";
}

// ========== CONFIG ==========
void saveConfig() {
    string configPath = CONFIG_PATH + "mr-config.txt";
    ofstream f(configPath, ios::out | ios::binary);
    if (!f.is_open()) return;
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    f.write((char*)bom, sizeof(bom));
    f << "OUTPUT_PATH=" << OUTPUT_PATH << "\n"
      << "OUTPUT_FORMAT=" << OUTPUT_FORMAT << "\n"
      << "OUTPUT_RESOLUTION=" << OUTPUT_RESOLUTION << "\n"
      << "OUTPUT_FPS=" << OUTPUT_FPS << "\n"
      << "AUDIO_BITRATE=" << AUDIO_BITRATE << "\n"
      << "VIDEO_BITRATE=" << VIDEO_BITRATE << "\n"
      << "CRF_VALUE=" << CRF_VALUE << "\n"
      << "PRESET=" << PRESET << "\n"
      << "OVERWRITE_FILES=" << (OVERWRITE_FILES ? "true" : "false") << "\n"
      << "HARDWARE_ACCEL=" << (HARDWARE_ACCEL ? "true" : "false") << "\n"
      << "KEEP_METADATA=" << (KEEP_METADATA ? "true" : "false") << "\n"
      << "WATERMARK_PATH=" << WATERMARK_PATH << "\n"
      << "WATERMARK_POSITION=" << WATERMARK_POSITION << "\n";
    f.close();
}

void loadConfig() {
    string configPath = CONFIG_PATH + "mr-config.txt";

    if (fileExists(configPath)) {
        ifstream f(configPath);
        if (f.is_open()) {
            string l;
            while (getline(f, l)) {
                if (l.length() >= 3 && (unsigned char)l[0] == 0xEF &&
                    (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF) {
                    l = l.substr(3);
                }
                while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();

                if (l.find("OUTPUT_PATH=") == 0) OUTPUT_PATH = l.substr(12);
                else if (l.find("OUTPUT_FORMAT=") == 0) OUTPUT_FORMAT = l.substr(14);
                else if (l.find("OUTPUT_RESOLUTION=") == 0) OUTPUT_RESOLUTION = l.substr(18);
                else if (l.find("OUTPUT_FPS=") == 0) OUTPUT_FPS = l.substr(11);
                else if (l.find("AUDIO_BITRATE=") == 0) AUDIO_BITRATE = l.substr(14);
                else if (l.find("VIDEO_BITRATE=") == 0) VIDEO_BITRATE = l.substr(14);
                else if (l.find("CRF_VALUE=") == 0) CRF_VALUE = l.substr(10);
                else if (l.find("PRESET=") == 0) PRESET = l.substr(7);
                else if (l.find("OVERWRITE_FILES=") == 0) OVERWRITE_FILES = (l.substr(16) == "true");
                else if (l.find("HARDWARE_ACCEL=") == 0) HARDWARE_ACCEL = (l.substr(15) == "true");
                else if (l.find("KEEP_METADATA=") == 0) KEEP_METADATA = (l.substr(14) == "true");
                else if (l.find("WATERMARK_PATH=") == 0) WATERMARK_PATH = l.substr(15);
                else if (l.find("WATERMARK_POSITION=") == 0) WATERMARK_POSITION = l.substr(19);
            }
            f.close();
        }
    }

    if (OUTPUT_PATH.empty()) {
        wchar_t buf[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, buf))) {
            wstring wPath = wstring(buf) + L"\\MR-CLI-FOR-FFMPEG\\output\\";
            string path = wstringToUtf8(wPath);
            if (!dirExists(path)) createDirRecursive(path);
            OUTPUT_PATH = path;
        }
        else {
            OUTPUT_PATH = "C:\\MR-CLI-FOR-FFMPEG\\output\\";
            if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
        }
        saveConfig();
    }
    else {
        if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
    }
}

// ========== OUTPUT FORMAT HELPERS ==========
string getOutputExtension() {
    if (OUTPUT_FORMAT.find("MP4") != string::npos) return "mp4";
    if (OUTPUT_FORMAT.find("MKV") != string::npos) return "mkv";
    if (OUTPUT_FORMAT.find("WEBM") != string::npos) return "webm";
    if (OUTPUT_FORMAT.find("AVI") != string::npos) return "avi";
    if (OUTPUT_FORMAT.find("MOV") != string::npos) return "mov";
    if (OUTPUT_FORMAT.find("MP3") != string::npos) return "mp3";
    if (OUTPUT_FORMAT.find("M4A") != string::npos) return "m4a";
    if (OUTPUT_FORMAT.find("WAV") != string::npos) return "wav";
    if (OUTPUT_FORMAT.find("FLAC") != string::npos) return "flac";
    if (OUTPUT_FORMAT.find("OGG") != string::npos) return "ogg";
    if (OUTPUT_FORMAT.find("GIF") != string::npos) return "gif";
    return "mp4";
}

string getVideoCodecArgs() {
    if (OUTPUT_FORMAT.find("H.264") != string::npos) return "-c:v libx264";
    if (OUTPUT_FORMAT.find("H.265") != string::npos || OUTPUT_FORMAT.find("HEVC") != string::npos) return "-c:v libx265";
    if (OUTPUT_FORMAT.find("AV1") != string::npos) return "-c:v libaom-av1";
    if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-c:v libvpx-vp9";
    if (OUTPUT_FORMAT.find("MPEG4") != string::npos) return "-c:v mpeg4";
    return "-c:v libx264";
}

string getAudioCodecArgs() {
    if (OUTPUT_FORMAT.find("MP3") != string::npos) return "-c:a libmp3lame";
    if (OUTPUT_FORMAT.find("AAC") != string::npos || OUTPUT_FORMAT.find("M4A") != string::npos || OUTPUT_FORMAT.find("MP4") != string::npos) return "-c:a aac";
    if (OUTPUT_FORMAT.find("Opus") != string::npos) return "-c:a libopus";
    if (OUTPUT_FORMAT.find("Vorbis") != string::npos || OUTPUT_FORMAT.find("OGG") != string::npos) return "-c:a libvorbis";
    if (OUTPUT_FORMAT.find("FLAC") != string::npos) return "-c:a flac";
    if (OUTPUT_FORMAT.find("WAV") != string::npos) return "-c:a pcm_s16le";
    if (OUTPUT_FORMAT.find("WEBM") != string::npos) return "-c:a libopus";
    return "-c:a aac";
}

string buildOutputPath(const string& inputPath, const string& suffix = "") {
    fs::path inPath = fs::u8path(inputPath);
    string stem = inPath.stem().u8string();
    string ext = getOutputExtension();
    string outName = stem + suffix + "." + ext;
    string outPath = OUTPUT_PATH + outName;

    // Avoid collision
    int counter = 1;
    while (fileExists(outPath)) {
        outPath = OUTPUT_PATH + stem + suffix + "_" + to_string(counter) + "." + ext;
        counter++;
    }
    return outPath;
}

// ========== OPERATION 1: CONVERT FORMAT ==========
void convertFormat() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CONVERT VIDEO/AUDIO FORMAT", CYAN);
    printColor("========================================", CYAN);
    cout << "\nSelect input file (or press ESC to cancel)...\n";

    string inputFile = openFileDialogMedia(L"Select file to convert");
    if (inputFile.empty()) {
        printColor("[INFO] Cancelled", YELLOW);
        waitForKey();
        return;
    }

    printColor("\nInput: " + inputFile, GREEN);

    // Show file info
    string info = getMediaInfo(inputFile);
    if (!info.empty()) {
        printColor("\n--- File Info ---", CYAN);
        cout << formatMediaInfoDisplay(info);
        printColor("-----------------", CYAN);
    }

    double duration = getMediaDuration(inputFile);
    string outPath = buildOutputPath(inputFile, "_converted");

    printColor("\nOutput: " + outPath, GREEN);
    printColor("Format: " + OUTPUT_FORMAT, GREEN);

    cout << "\n1. Start conversion\n0. Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    if (HARDWARE_ACCEL) cmd += L" -hwaccel auto";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs());
    cmd += L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";

    if (VIDEO_BITRATE != "auto") {
        cmd += L" -b:v " + utf8ToWstring(VIDEO_BITRATE) + L"k";
    } else {
        cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    }

    cmd += L" -preset " + utf8ToWstring(PRESET);

    if (OUTPUT_RESOLUTION != "original") {
        cmd += L" -vf scale=-2:" + utf8ToWstring(OUTPUT_RESOLUTION);
    }
    if (OUTPUT_FPS != "original") {
        cmd += L" -r " + utf8ToWstring(OUTPUT_FPS);
    }
    if (KEEP_METADATA) cmd += L" -map_metadata 0";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CONVERTING...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Conversion completed successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Conversion failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 2: TRIM / CUT VIDEO ==========
void trimVideo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" TRIM / CUT VIDEO", CYAN);
    printColor("========================================", CYAN);
    cout << "\nSelect input file...\n";

    string inputFile = openFileDialogMedia(L"Select file to trim");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }

    printColor("\nInput: " + inputFile, GREEN);
    double duration = getMediaDuration(inputFile);
    if (duration > 0) {
        int m = (int)(duration / 60), s = (int)duration % 60;
        char buf[64];
        snprintf(buf, sizeof(buf), "Duration: %02d:%02d (%.1f sec)", m, s, duration);
        printColor(string(buf), CYAN);
    }

    string startTime, endTime;
    cout << "\nStart time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n";
    if (!inputLineWithEscape(startTime, "> ")) { return; }
    cout << "End time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n";
    if (!inputLineWithEscape(endTime, "> ")) { return; }

    if (startTime.empty() || endTime.empty()) {
        printColor("[ERROR] Both start and end times are required!", RED);
        waitForKey();
        return;
    }

    string outPath = buildOutputPath(inputFile, "_trimmed");
    printColor("\nOutput: " + outPath, GREEN);

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -ss " + utf8ToWstring(startTime);
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -to " + utf8ToWstring(endTime);
    cmd += L" -c copy";  // Stream copy for speed
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" TRIMMING...", CYAN);
    printColor("========================================", CYAN);
    printColor("From " + startTime + " to " + endTime, CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, 0);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Trim completed successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Trim failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 3: EXTRACT AUDIO ==========
void extractAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" EXTRACT AUDIO FROM VIDEO", CYAN);
    printColor("========================================", CYAN);
    cout << "\nSelect video file...\n";

    string inputFile = openFileDialogMedia(L"Select video to extract audio from");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }

    printColor("\nInput: " + inputFile, GREEN);
    double duration = getMediaDuration(inputFile);

    cout << "\nOutput audio format:\n1) MP3\n2) M4A (AAC)\n3) WAV\n4) FLAC\n5) OGG (Vorbis)\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string ext, codecArgs;
    switch (ch) {
        case '1': ext = "mp3"; codecArgs = "-c:a libmp3lame -b:a " + AUDIO_BITRATE + "k"; break;
        case '2': ext = "m4a"; codecArgs = "-c:a aac -b:a " + AUDIO_BITRATE + "k"; break;
        case '3': ext = "wav"; codecArgs = "-c:a pcm_s16le"; break;
        case '4': ext = "flac"; codecArgs = "-c:a flac"; break;
        case '5': ext = "ogg"; codecArgs = "-c:a libvorbis -b:a " + AUDIO_BITRATE + "k"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    fs::path inPath = fs::u8path(inputFile);
    string outPath = OUTPUT_PATH + inPath.stem().u8string() + "_audio." + ext;

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -vn " + utf8ToWstring(codecArgs);
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" EXTRACTING AUDIO...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Audio extraction completed!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Audio extraction failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 4: MERGE VIDEO + AUDIO ==========
void mergeVideoAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" MERGE VIDEO + AUDIO", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect VIDEO file...\n";
    string videoFile = openFileDialogMedia(L"Select video file");
    if (videoFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Video: " + videoFile, GREEN);

    cout << "\nSelect AUDIO file...\n";
    string audioFile = openFileDialogMedia(L"Select audio file");
    if (audioFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Audio: " + audioFile, GREEN);

    double duration = getMediaDuration(videoFile);
    string outPath = buildOutputPath(videoFile, "_merged");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(videoFile) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(audioFile) + L"\"";
    cmd += L" -c:v copy -c:a aac -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    cmd += L" -map 0:v:0 -map 1:a:0 -shortest";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" MERGING VIDEO + AUDIO...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Merge completed successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Merge failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 5: CHANGE RESOLUTION ==========
void changeResolution() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CHANGE VIDEO RESOLUTION", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nTarget resolution:\n1) 3840x2160 (4K)\n2) 2560x1440 (2K)\n3) 1920x1080 (FullHD)\n4) 1280x720 (HD)\n5) 854x480\n6) 640x360\n7) Custom\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string scale;
    switch (ch) {
        case '1': scale = "3840:2160"; break;
        case '2': scale = "2560:1440"; break;
        case '3': scale = "1920:1080"; break;
        case '4': scale = "1280:720"; break;
        case '5': scale = "854:480"; break;
        case '6': scale = "640:360"; break;
        case '7': {
            string w, h;
            cout << "Width: ";
            if (!inputLineWithEscape(w, "")) return;
            cout << "Height: ";
            if (!inputLineWithEscape(h, "")) return;
            scale = w + ":" + h;
            break;
        }
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, "_" + scale);
    // Clean colons from filename
    for (auto& c2 : outPath) { /* scale is in path only via buildOutputPath suffix */ }

    // Build a safe suffix
    string safeSuffix = "_resized";
    outPath = buildOutputPath(inputFile, safeSuffix);

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    if (HARDWARE_ACCEL) cmd += L" -hwaccel auto";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -vf \"scale=" + utf8ToWstring(scale) + L":flags=lanczos\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    cmd += L" -preset " + utf8ToWstring(PRESET);
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CHANGING RESOLUTION TO " + scale + "...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Resolution changed successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Resolution change failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 6: CHANGE SPEED ==========
void changeSpeed() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CHANGE VIDEO SPEED", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nSpeed multiplier:\n1) 0.25x (very slow)\n2) 0.5x (slow)\n3) 0.75x\n4) 1.5x\n5) 2x (fast)\n6) 4x (very fast)\n7) Custom\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    double speed = 1.0;
    switch (ch) {
        case '1': speed = 0.25; break;
        case '2': speed = 0.5; break;
        case '3': speed = 0.75; break;
        case '4': speed = 1.5; break;
        case '5': speed = 2.0; break;
        case '6': speed = 4.0; break;
        case '7': {
            string s;
            cout << "Enter speed multiplier (e.g. 1.5): ";
            if (!inputLineWithEscape(s, "")) return;
            try { speed = stod(s); } catch (...) {
                printColor("[ERROR] Invalid number!", RED); waitForKey(); return;
            }
            break;
        }
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    if (speed <= 0 || speed > 100) {
        printColor("[ERROR] Speed must be between 0.01 and 100!", RED);
        waitForKey();
        return;
    }

    char speedBuf[32];
    snprintf(speedBuf, sizeof(speedBuf), "%.2f", speed);
    string outPath = buildOutputPath(inputFile, "_speed" + string(speedBuf));

    // Video: setpts=PTS/speed, Audio: atempo=speed (atempo only supports 0.5-2.0 range, chain for wider)
    double videoFactor = 1.0 / speed;
    char vfBuf[64];
    snprintf(vfBuf, sizeof(vfBuf), "setpts=%.4f*PTS", videoFactor);

    // Build atempo chain for audio (each atempo supports 0.5-2.0)
    string atempoChain;
    double remainingSpeed = speed;
    while (remainingSpeed > 2.0) {
        if (!atempoChain.empty()) atempoChain += ",";
        atempoChain += "atempo=2.0";
        remainingSpeed /= 2.0;
    }
    while (remainingSpeed < 0.5) {
        if (!atempoChain.empty()) atempoChain += ",";
        atempoChain += "atempo=0.5";
        remainingSpeed /= 0.5;
    }
    char atBuf[64];
    snprintf(atBuf, sizeof(atBuf), "atempo=%.4f", remainingSpeed);
    if (!atempoChain.empty()) atempoChain += ",";
    atempoChain += atBuf;

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -vf \"" + utf8ToWstring(string(vfBuf)) + L"\"";
    cmd += L" -af \"" + utf8ToWstring(atempoChain) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    cmd += L" -preset " + utf8ToWstring(PRESET);
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    char speedLabel[64];
    snprintf(speedLabel, sizeof(speedLabel), " CHANGING SPEED TO %.2fx...", speed);
    printColor(speedLabel, CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration / speed);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Speed change completed!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Speed change failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 7: ADD WATERMARK ==========
void addWatermark() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" ADD WATERMARK / OVERLAY", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Video: " + inputFile, GREEN);

    cout << "\nSelect watermark image (PNG recommended)...\n";
    string wmFile = openFileDialogMedia(L"Select watermark image");
    if (wmFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Watermark: " + wmFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nWatermark position:\n1) Top-left\n2) Top-right\n3) Bottom-left\n4) Bottom-right\n5) Center\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string overlay;
    switch (ch) {
        case '1': overlay = "overlay=10:10"; break;
        case '2': overlay = "overlay=W-w-10:10"; break;
        case '3': overlay = "overlay=10:H-h-10"; break;
        case '4': overlay = "overlay=W-w-10:H-h-10"; break;
        case '5': overlay = "overlay=(W-w)/2:(H-h)/2"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, "_watermarked");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(wmFile) + L"\"";
    cmd += L" -filter_complex \"" + utf8ToWstring(overlay) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    cmd += L" -preset " + utf8ToWstring(PRESET);
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" ADDING WATERMARK...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Watermark added successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Watermark failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 8: COMPRESS VIDEO ==========
void compressVideo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" COMPRESS VIDEO", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file to compress");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nCompression level:\n1) Light (CRF 20 - high quality, larger file)\n2) Medium (CRF 26 - balanced)\n3) Heavy (CRF 32 - smaller file, lower quality)\n4) Extreme (CRF 38 - minimum size)\n5) Custom CRF\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string crf;
    switch (ch) {
        case '1': crf = "20"; break;
        case '2': crf = "26"; break;
        case '3': crf = "32"; break;
        case '4': crf = "38"; break;
        case '5': {
            string s;
            cout << "Enter CRF value (0-51, lower = better quality): ";
            if (!inputLineWithEscape(s, "")) return;
            crf = s;
            break;
        }
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, "_compressed");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    if (HARDWARE_ACCEL) cmd += L" -hwaccel auto";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(crf);
    cmd += L" -preset slower";  // Use slower preset for better compression
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a 128k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" COMPRESSING (CRF " + crf + ")...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        // Show size comparison
        std::error_code ec;
        auto inSize = fs::file_size(fs::u8path(inputFile), ec);
        auto outSize = fs::file_size(fs::u8path(outPath), ec);
        if (inSize > 0 && outSize > 0) {
            double ratio = (1.0 - (double)outSize / (double)inSize) * 100.0;
            char buf[128];
            snprintf(buf, sizeof(buf), "Size: %.2f MB -> %.2f MB (%.1f%% smaller)",
                (double)inSize / (1024.0*1024.0), (double)outSize / (1024.0*1024.0), ratio);
            printColor(string(buf), GREEN);
        }
        printColor("\n========================================", GREEN);
        printColor("[OK] Compression completed!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Compression failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 9: ROTATE / FLIP VIDEO ==========
void rotateVideo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" ROTATE / FLIP VIDEO", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nTransform:\n1) Rotate 90 clockwise\n2) Rotate 90 counter-clockwise\n3) Rotate 180\n4) Flip horizontal (mirror)\n5) Flip vertical\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string vf, suffix;
    switch (ch) {
        case '1': vf = "transpose=1"; suffix = "_rot90"; break;
        case '2': vf = "transpose=2"; suffix = "_rot270"; break;
        case '3': vf = "transpose=1,transpose=1"; suffix = "_rot180"; break;
        case '4': vf = "hflip"; suffix = "_hflip"; break;
        case '5': vf = "vflip"; suffix = "_vflip"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, suffix);

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -vf \"" + utf8ToWstring(vf) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    cmd += L" -preset " + utf8ToWstring(PRESET);
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" TRANSFORMING VIDEO...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Transform completed!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Transform failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 10: CREATE GIF ==========
void createGif() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CREATE GIF FROM VIDEO", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);
    if (duration > 0) {
        int m = (int)(duration / 60), s = (int)duration % 60;
        char buf[64];
        snprintf(buf, sizeof(buf), "Duration: %02d:%02d (%.1f sec)", m, s, duration);
        printColor(string(buf), CYAN);
    }

    string startTime, gifDuration;
    cout << "\nStart time (HH:MM:SS or seconds, Enter=start):\n";
    if (!inputLineWithEscape(startTime, "> ")) { startTime = "0"; }
    if (startTime.empty()) startTime = "0";
    cout << "Duration in seconds (Enter=5):\n";
    if (!inputLineWithEscape(gifDuration, "> ")) { gifDuration = "5"; }
    if (gifDuration.empty()) gifDuration = "5";

    cout << "\nGIF width (Enter=480):\n";
    string gifWidth;
    if (!inputLineWithEscape(gifWidth, "> ")) { gifWidth = "480"; }
    if (gifWidth.empty()) gifWidth = "480";

    cout << "\nFPS (Enter=15):\n";
    string gifFps;
    if (!inputLineWithEscape(gifFps, "> ")) { gifFps = "15"; }
    if (gifFps.empty()) gifFps = "15";

    fs::path inPath = fs::u8path(inputFile);
    string outPath = OUTPUT_PATH + inPath.stem().u8string() + ".gif";

    // Two-pass GIF creation for good quality
    string palettePath = OUTPUT_PATH + "palette_tmp.png";

    // Pass 1: Generate palette
    wstring cmd1 = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd1 += L" -ss " + utf8ToWstring(startTime);
    cmd1 += L" -t " + utf8ToWstring(gifDuration);
    cmd1 += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd1 += L" -vf \"fps=" + utf8ToWstring(gifFps) + L",scale=" + utf8ToWstring(gifWidth) + L":-1:flags=lanczos,palettegen\"";
    cmd1 += L" -y \"" + utf8ToWstring(palettePath) + L"\"";

    // Pass 2: Create GIF with palette
    wstring cmd2 = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd2 += L" -ss " + utf8ToWstring(startTime);
    cmd2 += L" -t " + utf8ToWstring(gifDuration);
    cmd2 += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd2 += L" -i \"" + utf8ToWstring(palettePath) + L"\"";
    cmd2 += L" -filter_complex \"fps=" + utf8ToWstring(gifFps) + L",scale=" + utf8ToWstring(gifWidth) + L":-1:flags=lanczos[x];[x][1:v]paletteuse\"";
    cmd2 += L" -y \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CREATING GIF...", CYAN);
    printColor("========================================", CYAN);

    printColor("\nPass 1: Generating palette...", CYAN);
    bool ok = execFFmpegWithProgress(cmd1, 0);

    if (ok) {
        printColor("\nPass 2: Creating GIF...", CYAN);
        ok = execFFmpegWithProgress(cmd2, 0);
    }

    // Cleanup palette
    std::error_code ec;
    fs::remove(fs::u8path(palettePath), ec);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] GIF created successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] GIF creation failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 11: CONCATENATE FILES ==========
void concatenateFiles() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CONCATENATE (JOIN) FILES", CYAN);
    printColor("========================================", CYAN);

    vector<string> files;
    cout << "\nAdd files one by one. Press ESC when done.\n";

    int fileNum = 1;
    while (true) {
        cout << "\nSelect file #" << fileNum << " (ESC to finish)...\n";
        string f = openFileDialogMedia(L"Select file to add");
        if (f.empty()) break;
        files.push_back(f);
        printColor("Added: " + f, GREEN);
        fileNum++;
    }

    if (files.size() < 2) {
        printColor("[ERROR] Need at least 2 files to concatenate!", RED);
        waitForKey();
        return;
    }

    // Create concat list file
    string listPath = CONFIG_PATH + "concat_list.txt";
    {
        ofstream listFile(fs::u8path(listPath), ios::out | ios::binary);
        for (const auto& f : files) {
            // Escape single quotes
            string escaped = f;
            size_t pos = 0;
            while ((pos = escaped.find("'", pos)) != string::npos) {
                escaped.replace(pos, 1, "'\\''");
                pos += 4;
            }
            listFile << "file '" << escaped << "'\n";
        }
        listFile.close();
    }

    double totalDuration = 0;
    for (const auto& f : files) totalDuration += getMediaDuration(f);

    string outPath = buildOutputPath(files[0], "_joined");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -f concat -safe 0 -i \"" + utf8ToWstring(listPath) + L"\"";
    cmd += L" -c copy";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" CONCATENATING " + to_string(files.size()) + " FILES...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, totalDuration);

    // Cleanup
    std::error_code ec;
    fs::remove(fs::u8path(listPath), ec);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Concatenation completed!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Concatenation failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 12: STRIP AUDIO (MUTE) ==========
void stripAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" STRIP AUDIO (REMOVE SOUND)", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);
    string outPath = buildOutputPath(inputFile, "_nosound");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -c:v copy -an";  // Copy video, no audio
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" STRIPPING AUDIO...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Audio stripped successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Strip audio failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 13: FILE INFO ==========
void showFileInfo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" MEDIA FILE INFORMATION", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect media file...\n";
    string inputFile = openFileDialogMedia(L"Select media file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }

    printColor("\nFile: " + inputFile, GREEN);

    // File size
    std::error_code ec;
    auto fsize = fs::file_size(fs::u8path(inputFile), ec);
    if (!ec) {
        char buf[64];
        if (fsize > 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "Size: %.2f GB", (double)fsize / (1024.0*1024.0*1024.0));
        else if (fsize > 1024 * 1024) snprintf(buf, sizeof(buf), "Size: %.2f MB", (double)fsize / (1024.0*1024.0));
        else snprintf(buf, sizeof(buf), "Size: %.2f KB", (double)fsize / 1024.0);
        printColor(string(buf), WHITE);
    }

    string info = getMediaInfo(inputFile);
    if (!info.empty()) {
        printColor("\n--- Detailed Info ---", CYAN);
        cout << formatMediaInfoDisplay(info);
        printColor("---------------------", CYAN);
    } else {
        printColor("[WARNING] Could not retrieve media info", YELLOW);
    }

    waitForKey();
}

// ========== OPERATION 14: EXTRACT FRAMES (SCREENSHOTS) ==========
void extractFrames() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" EXTRACT FRAMES (SCREENSHOTS)", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("\nInput: " + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\nExtraction mode:\n1) Single frame at time position\n2) One frame per second\n3) One frame per N seconds\n4) Every Nth frame\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    // Create output subfolder
    fs::path inPath = fs::u8path(inputFile);
    string framesDir = OUTPUT_PATH + inPath.stem().u8string() + "_frames\\";
    createDirRecursive(framesDir);

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";

    switch (ch) {
        case '1': {
            string timePos;
            cout << "Time position (HH:MM:SS or seconds): ";
            if (!inputLineWithEscape(timePos, "")) return;
            cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
            cmd += L" -ss " + utf8ToWstring(timePos);
            cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
            cmd += L" -vframes 1 -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(framesDir + "frame.png") + L"\"";
            break;
        }
        case '2': {
            cmd += L" -vf fps=1 -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(framesDir + "frame_%04d.png") + L"\"";
            break;
        }
        case '3': {
            string interval;
            cout << "Interval in seconds: ";
            if (!inputLineWithEscape(interval, "")) return;
            cmd += L" -vf \"fps=1/" + utf8ToWstring(interval) + L"\" -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(framesDir + "frame_%04d.png") + L"\"";
            break;
        }
        case '4': {
            string nth;
            cout << "Extract every Nth frame (e.g. 30): ";
            if (!inputLineWithEscape(nth, "")) return;
            cmd += L" -vf \"select=not(mod(n\\," + utf8ToWstring(nth) + L"))\" -vsync vfr -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(framesDir + "frame_%04d.png") + L"\"";
            break;
        }
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" EXTRACTING FRAMES...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Frames extracted!", GREEN);
        printColor("Output folder: " + framesDir, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Frame extraction failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 15: ADD SUBTITLES ==========
void addSubtitles() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" ADD SUBTITLES (BURN-IN)", CYAN);
    printColor("========================================", CYAN);

    cout << "\nSelect video file...\n";
    string inputFile = openFileDialogMedia(L"Select video file");
    if (inputFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Video: " + inputFile, GREEN);

    cout << "\nSelect subtitle file (SRT/ASS/SSA)...\n";
    // Use a custom file dialog for subtitles
    OPENFILENAMEW ofn = { 0 };
    wchar_t fn[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = L"Subtitle Files\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Select subtitle file";

    string subFile;
    if (GetOpenFileNameW(&ofn)) {
        subFile = wstringToUtf8(wstring(fn));
    }
    if (subFile.empty()) { printColor("[INFO] Cancelled", YELLOW); waitForKey(); return; }
    printColor("Subtitles: " + subFile, GREEN);

    double duration = getMediaDuration(inputFile);

    // Escape backslashes and colons for FFmpeg subtitle filter
    string escapedSubPath = subFile;
    string escaped;
    for (char c : escapedSubPath) {
        if (c == '\\') escaped += "\\\\\\\\";
        else if (c == ':') escaped += "\\\\:";
        else if (c == '\'') escaped += "\\'";
        else escaped += c;
    }

    string outPath = buildOutputPath(inputFile, "_subtitled");

    wstring cmd = L"\"" + utf8ToWstring(FFMPEG_PATH) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(inputFile) + L"\"";
    cmd += L" -vf \"subtitles='" + utf8ToWstring(escaped) + L"'\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" -crf " + utf8ToWstring(CRF_VALUE);
    cmd += L" -preset " + utf8ToWstring(PRESET);
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(outPath) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" BURNING SUBTITLES...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor("[OK] Subtitles added successfully!", GREEN);
        printColor("Output: " + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor("[ERROR] Adding subtitles failed!", RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== SETTINGS MENUS ==========
void selectOutputFormat() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT OUTPUT FORMAT", CYAN);
    printColor("========================================", CYAN);
    cout << "\n--- Video Formats ---"
         << "\n1) MP4(H.264)"
         << "\n2) MP4(H.265/HEVC)"
         << "\n3) MP4(AV1)"
         << "\n4) MKV(H.264)"
         << "\n5) MKV(H.265/HEVC)"
         << "\n6) WEBM(VP9)"
         << "\n7) WEBM(AV1)"
         << "\n8) AVI(MPEG4)"
         << "\n9) MOV(H.264)"
         << "\n--- Audio Formats ---"
         << "\na) MP3"
         << "\nb) M4A(AAC)"
         << "\nc) WAV"
         << "\nd) FLAC"
         << "\ne) OGG(Vorbis)"
         << "\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    switch (ch) {
        case '1': OUTPUT_FORMAT = "MP4(H.264)"; break;
        case '2': OUTPUT_FORMAT = "MP4(H.265/HEVC)"; break;
        case '3': OUTPUT_FORMAT = "MP4(AV1)"; break;
        case '4': OUTPUT_FORMAT = "MKV(H.264)"; break;
        case '5': OUTPUT_FORMAT = "MKV(H.265/HEVC)"; break;
        case '6': OUTPUT_FORMAT = "WEBM(VP9)"; break;
        case '7': OUTPUT_FORMAT = "WEBM(AV1)"; break;
        case '8': OUTPUT_FORMAT = "AVI(MPEG4)"; break;
        case '9': OUTPUT_FORMAT = "MOV(H.264)"; break;
        case 'a': OUTPUT_FORMAT = "MP3"; break;
        case 'b': OUTPUT_FORMAT = "M4A(AAC)"; break;
        case 'c': OUTPUT_FORMAT = "WAV"; break;
        case 'd': OUTPUT_FORMAT = "FLAC"; break;
        case 'e': OUTPUT_FORMAT = "OGG(Vorbis)"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }
    saveConfig();
    printColor("[OK] Output format set to " + OUTPUT_FORMAT, GREEN);
    waitForKey();
}

void selectResolution() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT OUTPUT RESOLUTION", CYAN);
    printColor("========================================", CYAN);
    cout << "\n1) Original (no change)"
         << "\n2) 2160p (4K)"
         << "\n3) 1440p (2K)"
         << "\n4) 1080p (FullHD)"
         << "\n5) 720p (HD)"
         << "\n6) 480p"
         << "\n7) 360p"
         << "\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;
    switch (ch) {
        case '1': OUTPUT_RESOLUTION = "original"; break;
        case '2': OUTPUT_RESOLUTION = "2160"; break;
        case '3': OUTPUT_RESOLUTION = "1440"; break;
        case '4': OUTPUT_RESOLUTION = "1080"; break;
        case '5': OUTPUT_RESOLUTION = "720"; break;
        case '6': OUTPUT_RESOLUTION = "480"; break;
        case '7': OUTPUT_RESOLUTION = "360"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }
    saveConfig();
    printColor("[OK] Output resolution set to " + OUTPUT_RESOLUTION, GREEN);
    waitForKey();
}

void selectFPS() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT OUTPUT FPS", CYAN);
    printColor("========================================", CYAN);
    cout << "\n1) Original (no change)"
         << "\n2) 60fps"
         << "\n3) 30fps"
         << "\n4) 24fps"
         << "\n5) 15fps"
         << "\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;
    switch (ch) {
        case '1': OUTPUT_FPS = "original"; break;
        case '2': OUTPUT_FPS = "60"; break;
        case '3': OUTPUT_FPS = "30"; break;
        case '4': OUTPUT_FPS = "24"; break;
        case '5': OUTPUT_FPS = "15"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }
    saveConfig();
    printColor("[OK] FPS set to " + OUTPUT_FPS, GREEN);
    waitForKey();
}

void selectPreset() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT ENCODING PRESET", CYAN);
    printColor("========================================", CYAN);
    cout << "\nFaster presets = larger file, quicker encoding"
         << "\nSlower presets = smaller file, slower encoding"
         << "\n\n1) ultrafast"
         << "\n2) superfast"
         << "\n3) veryfast"
         << "\n4) faster"
         << "\n5) fast"
         << "\n6) medium (default)"
         << "\n7) slow"
         << "\n8) slower"
         << "\n9) veryslow"
         << "\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;
    switch (ch) {
        case '1': PRESET = "ultrafast"; break;
        case '2': PRESET = "superfast"; break;
        case '3': PRESET = "veryfast"; break;
        case '4': PRESET = "faster"; break;
        case '5': PRESET = "fast"; break;
        case '6': PRESET = "medium"; break;
        case '7': PRESET = "slow"; break;
        case '8': PRESET = "slower"; break;
        case '9': PRESET = "veryslow"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }
    saveConfig();
    printColor("[OK] Preset set to " + PRESET, GREEN);
    waitForKey();
}

void selectCRF() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT CRF VALUE", CYAN);
    printColor("========================================", CYAN);
    cout << "\nCRF (Constant Rate Factor): 0-51"
         << "\nLower = better quality, larger file"
         << "\n0 = lossless, 23 = default, 51 = worst"
         << "\n\nCurrent CRF: " << CRF_VALUE
         << "\n\nEnter new CRF value (0-51, ESC to cancel): ";
    string val;
    if (!inputLineWithEscape(val, "")) return;
    try {
        int v = stoi(val);
        if (v < 0 || v > 51) { printColor("[ERROR] CRF must be 0-51!", RED); waitForKey(); return; }
        CRF_VALUE = val;
        saveConfig();
        printColor("[OK] CRF set to " + CRF_VALUE, GREEN);
    } catch (...) {
        printColor("[ERROR] Invalid number!", RED);
    }
    waitForKey();
}

void selectAudioBitrate() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" SELECT AUDIO BITRATE", CYAN);
    printColor("========================================", CYAN);
    cout << "\n1) 96 kbps"
         << "\n2) 128 kbps"
         << "\n3) 192 kbps (default)"
         << "\n4) 256 kbps"
         << "\n5) 320 kbps"
         << "\n0) Cancel (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;
    switch (ch) {
        case '1': AUDIO_BITRATE = "96"; break;
        case '2': AUDIO_BITRATE = "128"; break;
        case '3': AUDIO_BITRATE = "192"; break;
        case '4': AUDIO_BITRATE = "256"; break;
        case '5': AUDIO_BITRATE = "320"; break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey(); return;
    }
    saveConfig();
    printColor("[OK] Audio bitrate set to " + AUDIO_BITRATE + " kbps", GREEN);
    waitForKey();
}

// ========== UPDATE COMPONENTS ==========
void updateComponentsMenu() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" COMPONENT UPDATER", CYAN);
    printColor("========================================", CYAN);
    cout << "\n1. Re-download FFmpeg + FFprobe"
         << "\n0. Return (ESC)\n\nYour choice: ";
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') {
        return;
    }
    if (ch == '1') {
        clearScreen();
        printColor("========================================", CYAN);
        printColor(" Downloading latest FFmpeg (~160MB)...", CYAN);
        printColor("========================================", CYAN);
        string zipFile = CONFIG_PATH + "ffmpeg.zip";
        if (downloadFile("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip", zipFile, "FFmpeg")) {
            printComponentProgress("FFmpeg", 100.0, "Extracting components...");
            if (extractZip(zipFile, CONFIG_PATH)) {
                organizeExtractedTool("ffmpeg.exe", CONFIG_PATH);
                FFMPEG_FOUND = fileExists(CONFIG_PATH + "ffmpeg.exe");
                FFPROBE_FOUND = fileExists(CONFIG_PATH + "ffprobe.exe");
                if (FFMPEG_FOUND) {
                    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
                    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";
                    cout << "\n";
                    printColor("[OK] FFmpeg updated successfully!", GREEN);
                }
                else {
                    cout << "\n";
                    printColor("[ERROR] Failed to locate ffmpeg.exe after extraction!", RED);
                }
            }
            else {
                cout << "\n";
                printColor("[ERROR] Failed to extract FFmpeg archive!", RED);
            }
            std::error_code ec;
            fs::remove(fs::u8path(zipFile), ec);
        }
        else {
            printColor("[ERROR] Failed to download FFmpeg!", RED);
        }
        waitForKey();
    }
}

// ========== SETTINGS ==========
void settingsMenu() {
    while (true) {
        clearScreen();
        printColor("========================================", CYAN);
        printColor(" SETTINGS", CYAN);
        printColor("========================================", CYAN);
        cout << "\n1. Output location: [" << OUTPUT_PATH << "]"
             << "\n2. Output format: [" << OUTPUT_FORMAT << "]"
             << "\n3. Resolution: [" << OUTPUT_RESOLUTION << "]"
             << "\n4. FPS: [" << OUTPUT_FPS << "]"
             << "\n5. Encoding preset: [" << PRESET << "]"
             << "\n6. CRF value: [" << CRF_VALUE << "]"
             << "\n7. Audio bitrate: [" << AUDIO_BITRATE << " kbps]"
             << "\n8. Hardware acceleration: [" << (HARDWARE_ACCEL ? "ON" : "OFF") << "]"
             << "\n9. Overwrite files: [" << (OVERWRITE_FILES ? "ON" : "OFF") << "]"
             << "\nm. Keep metadata: [" << (KEEP_METADATA ? "ON" : "OFF") << "]"
             << "\nu. Update FFmpeg & components"
             << "\n0. Return (ESC)\n\nYour choice: ";
        char ch = getMenuChoice();
        if (ch == 27 || ch == '0') {
            return;
        }
        cout << ch << endl;
        switch (ch) {
        case '1': {
            string p = openFolderDialog();
            if (!p.empty()) {
                OUTPUT_PATH = p;
                if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
                saveConfig();
                printColor("[OK] Output location updated!", GREEN);
            }
            else {
                printColor("[INFO] Not changed", YELLOW);
            }
            waitForKey();
            break;
        }
        case '2': selectOutputFormat(); break;
        case '3': selectResolution(); break;
        case '4': selectFPS(); break;
        case '5': selectPreset(); break;
        case '6': selectCRF(); break;
        case '7': selectAudioBitrate(); break;
        case '8':
            HARDWARE_ACCEL = !HARDWARE_ACCEL;
            saveConfig();
            printColor(string("[OK] Hardware acceleration ") + (HARDWARE_ACCEL ? "ON" : "OFF"), GREEN);
            waitForKey();
            break;
        case '9':
            OVERWRITE_FILES = !OVERWRITE_FILES;
            saveConfig();
            printColor(string("[OK] Overwrite files ") + (OVERWRITE_FILES ? "ON" : "OFF"), GREEN);
            waitForKey();
            break;
        case 'm': case 'M':
            KEEP_METADATA = !KEEP_METADATA;
            saveConfig();
            printColor(string("[OK] Keep metadata ") + (KEEP_METADATA ? "ON" : "OFF"), GREEN);
            waitForKey();
            break;
        case 'u': case 'U': updateComponentsMenu(); break;
        default: printColor("[ERROR] Invalid choice!", RED); waitForKey();
        }
    }
}

// ========== DEPENDENCY CHECKS & AUTO INSTALLER ==========
bool checkDependencies() {
    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";

    FFMPEG_FOUND = fileExists(FFMPEG_PATH);
    FFPROBE_FOUND = fileExists(FFPROBE_PATH);

    if (!FFMPEG_FOUND) {
        printColor("========================================", RED);
        printColor("[ERROR] FFmpeg not found!", RED);
        printColor("========================================", RED);

        printColor("\n========================================", CYAN);
        printColor(" AUTO INSTALLER", CYAN);
        printColor("========================================", CYAN);

        while (true) {
            printColor("\nInstall FFmpeg automatically? (y/n): \n", CYAN, false);

            char ch = getMenuChoice();

            if (ch == 'y' || ch == 'Y') {
                cout << "y" << endl;
                break;
            }
            else if (ch == 'n' || ch == 'N' || ch == 27) {
                cout << "n" << endl;
                printColor("\n========================================", YELLOW);
                printColor(" [ERROR] FFmpeg is required to run the application!", RED);
                printColor(" [INFO] Download from: https://github.com/BtbN/FFmpeg-Builds/releases", YELLOW);
                printColor(" [INFO] Place 'ffmpeg.exe' and 'ffprobe.exe' in: " + CONFIG_PATH, YELLOW);
                printColor("========================================", YELLOW);
                waitForKey();
                return false;
            }
        }

        // ========== INSTALLATION ==========
        if (!dirExists(CONFIG_PATH)) {
            createDirRecursive(CONFIG_PATH);
        }

        printColor("\n[INFO] Installing FFmpeg (~160MB)...", CYAN);
        string zipFile = CONFIG_PATH + "ffmpeg.zip";
        if (downloadFile("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip", zipFile, "FFmpeg")) {
            printComponentProgress("FFmpeg", 100.0, "Extracting components...");
            if (extractZip(zipFile, CONFIG_PATH)) {
                organizeExtractedTool("ffmpeg.exe", CONFIG_PATH);
                FFMPEG_FOUND = fileExists(CONFIG_PATH + "ffmpeg.exe");
                FFPROBE_FOUND = fileExists(CONFIG_PATH + "ffprobe.exe");
                if (FFMPEG_FOUND) {
                    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
                    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";
                    cout << "\n";
                    printColor("[OK] FFmpeg and FFprobe installed successfully!", GREEN);
                }
                else {
                    cout << "\n";
                    printColor("[ERROR] Failed to locate ffmpeg.exe after extraction!", RED);
                }
            }
            else {
                cout << "\n";
                printColor("[ERROR] Failed to extract FFmpeg archive!", RED);
            }

            std::error_code ec;
            fs::remove(fs::u8path(zipFile), ec);
        }
        else {
            printColor("[ERROR] Failed to download FFmpeg!", RED);
        }

        // Final verification
        FFMPEG_FOUND = fileExists(FFMPEG_PATH);
        FFPROBE_FOUND = fileExists(FFPROBE_PATH);

        if (FFMPEG_FOUND) {
            printColor("\n[OK] Dependencies check completed!", GREEN);
            Sleep(1000);
            return true;
        }
        else {
            printColor("\n[ERROR] FFmpeg is missing and could not be installed!", RED);
            waitForKey();
            return false;
        }
    }

    return true;
}

// ========== MAIN MENU ==========
void displayMenu() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(" MR CLI FOR FFMPEG v1.0.0", CYAN);
    printColor("========================================", CYAN);
    printColor("========================================", GREEN);
    printColor(" FFMPEG:  " + string(FFMPEG_FOUND ? "[OK] installed" : "[ERROR] not found"), FFMPEG_FOUND ? GREEN : RED);
    printColor(" FFPROBE: " + string(FFPROBE_FOUND ? "[OK] installed" : "[WARNING] not installed"), FFPROBE_FOUND ? GREEN : YELLOW);
    printColor("========================================", GREEN);
    cout << "========================================\n"
         << "--- VIDEO OPERATIONS ---\n"
         << " 1. Convert format\n"
         << " 2. Trim / Cut video\n"
         << " 3. Change resolution\n"
         << " 4. Change speed\n"
         << " 5. Rotate / Flip\n"
         << " 6. Compress video\n"
         << " 7. Add watermark\n"
         << " 8. Add subtitles (burn-in)\n"
         << "--- AUDIO OPERATIONS ---\n"
         << " 9. Extract audio\n"
         << " a. Merge video + audio\n"
         << " b. Strip audio (mute)\n"
         << "--- OTHER ---\n"
         << " c. Concatenate (join) files\n"
         << " d. Create GIF\n"
         << " e. Extract frames\n"
         << " f. File information\n"
         << "--- PROGRAM ---\n"
         << " s. Settings\n"
         << " 0. Exit (ESC)\n"
         << "========================================\n"
         << "\nYour choice: ";
}

int main() {
    // Initialize COM for Windows Shell, Folder Dialogs, and Shell Zip extraction
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    setUTF8();

    // Create base directories in User Documents
    wchar_t docPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
        wstring basePath = wstring(docPath) + L"\\MR-CLI-FOR-FFMPEG\\";
        string basePathStr = wstringToUtf8(basePath);

        CONFIG_PATH = basePathStr + "configs\\";
        OUTPUT_PATH = basePathStr + "output\\";

        if (!dirExists(CONFIG_PATH)) createDirRecursive(CONFIG_PATH);
        if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
    }
    else {
        CONFIG_PATH = "C:\\MR-CLI-FOR-FFMPEG\\configs\\";
        OUTPUT_PATH = "C:\\MR-CLI-FOR-FFMPEG\\output\\";
        if (!dirExists(CONFIG_PATH)) createDirRecursive(CONFIG_PATH);
        if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
    }

    loadConfig();

    if (!checkDependencies()) {
        CoUninitialize();
        return 1;
    }

    while (true) {
        displayMenu();
        char ch = getMenuChoice();
        if (ch == 27 || ch == '0') {
            cout << "Exiting...\n";
            CoUninitialize();
            return 0;
        }
        cout << ch << "\n\n";
        switch (ch) {
        case '1': convertFormat(); break;
        case '2': trimVideo(); break;
        case '3': changeResolution(); break;
        case '4': changeSpeed(); break;
        case '5': rotateVideo(); break;
        case '6': compressVideo(); break;
        case '7': addWatermark(); break;
        case '8': addSubtitles(); break;
        case '9': extractAudio(); break;
        case 'a': mergeVideoAudio(); break;
        case 'b': stripAudio(); break;
        case 'c': concatenateFiles(); break;
        case 'd': createGif(); break;
        case 'e': extractFrames(); break;
        case 'f': showFileInfo(); break;
        case 's': settingsMenu(); break;
        default:
            printColor("[ERROR] Invalid choice!", RED);
            waitForKey();
        }
    }

    CoUninitialize();
    return 0;
}
