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
#pragma comment(lib, "Advapi32.lib")

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
bool OVERWRITE_FILES = false;
bool KEEP_METADATA = true;
bool VIDEO_CODEC_ASK = true;
bool AUDIO_CODEC_ASK = true;
bool g_ffmpegEscaped = false;

// ========== ACCELERATION & GPU MODES ==========
enum AccelMode {
    ACCEL_CPU_ONLY = 0,         // Программный CPU (libx264)
    ACCEL_CPU_DEC_GPU_ENC = 1,  // Программный + Аппаратный (Декодирование CPU, Кодирование GPU)
    ACCEL_NVIDIA = 2,           // Аппаратный NVIDIA (NVENC)
    ACCEL_INTEL = 3,            // Аппаратный INTEL (QSV)
    ACCEL_AMD = 4               // Аппаратный AMD (AMF)
};

bool CONFIG_LOADED = false;
AccelMode ACCELERATION_MODE = ACCEL_CPU_ONLY;
AccelMode HYBRID_GPU_CHOICE = ACCEL_CPU_ONLY; // Which GPU to use if ACCEL_CPU_DEC_GPU_ENC is chosen

bool HAS_NVIDIA_DEVICE = false;
bool HAS_INTEL_DEVICE = false;
bool HAS_AMD_DEVICE = false;

string DETECTED_GPU_NAME = "";
string DETECTED_NVIDIA_NAME = "";
string DETECTED_INTEL_NAME = "";
string DETECTED_AMD_NAME = "";
string DETECTED_CPU_NAME = "";

// ========== LANGUAGE ==========
enum Language { LANG_EN = 0, LANG_RU = 1 };
Language CURRENT_LANG = LANG_EN;

void initDefaultLanguage() {
    WORD langId = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(langId);
    if (primary == LANG_RUSSIAN || primary == LANG_BELARUSIAN || primary == LANG_UKRAINIAN) {
        CURRENT_LANG = LANG_RU;
    } else {
        CURRENT_LANG = LANG_EN;
    }
}

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

// ========== SAFE PATH HELPERS ==========
wstring getShortPathName(const wstring& longPath) {
    if (longPath.empty()) return longPath;
    DWORD size = GetShortPathNameW(longPath.c_str(), NULL, 0);
    if (size == 0) return longPath;
    wstring shortPath(size, 0);
    DWORD result = GetShortPathNameW(longPath.c_str(), &shortPath[0], size);
    if (result == 0 || result >= size) return longPath;
    shortPath.resize(result);
    return shortPath;
}

string getSafeFFmpegPath(const string& utf8Path) {
    wstring wPath = utf8ToWstring(utf8Path);
    wstring shortPath = getShortPathName(wPath);
    return wstringToUtf8(shortPath);
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

// ========== BILINGUAL KEYBOARD MAPPING (QWERTY ↔ ЙЦУКЕН) ==========
char normalizeKeyToEnglish(wint_t wc) {
    if (wc < 128) return (char)wc;
    switch (wc) {
        case 0x0439: case 0x0419: return 'q';  // й Й
        case 0x0446: case 0x0426: return 'w';  // ц Ц
        case 0x0443: case 0x0423: return 'e';  // у У
        case 0x043A: case 0x041A: return 'r';  // к К
        case 0x0435: case 0x0415: return 't';  // е Е
        case 0x043D: case 0x041D: return 'y';  // н Н
        case 0x0433: case 0x0413: return 'u';  // г Г
        case 0x0448: case 0x0428: return 'i';  // ш Ш
        case 0x0449: case 0x0429: return 'o';  // щ Щ
        case 0x0437: case 0x0417: return 'p';  // з З
        case 0x0445: case 0x0425: return '[';  // х Х
        case 0x044A: case 0x042A: return ']';  // ъ Ъ
        case 0x0444: case 0x0424: return 'a';  // ф Ф
        case 0x044B: case 0x042B: return 's';  // ы Ы
        case 0x0432: case 0x0412: return 'd';  // в В
        case 0x0430: case 0x0410: return 'f';  // а А
        case 0x043F: case 0x041F: return 'g';  // п П
        case 0x0440: case 0x0420: return 'h';  // р Р
        case 0x043E: case 0x041E: return 'j';  // о О
        case 0x043B: case 0x041B: return 'k';  // л Л
        case 0x0434: case 0x0414: return 'l';  // д Д
        case 0x0436: case 0x0416: return ';';  // ж Ж
        case 0x044D: case 0x042D: return '\''; // э Э
        case 0x044F: case 0x042F: return 'z';  // я Я
        case 0x0447: case 0x0427: return 'x';  // ч Ч
        case 0x0441: case 0x0421: return 'c';  // с С
        case 0x043C: case 0x041C: return 'v';  // м М
        case 0x0438: case 0x0418: return 'b';  // и И
        case 0x0442: case 0x0422: return 'n';  // т Т
        case 0x044C: case 0x042C: return 'm';  // ь Ь
        case 0x0431: case 0x0411: return ',';  // б Б
        case 0x044E: case 0x042E: return '.';  // ю Ю
        default: return (char)(wc & 0xFF);
    }
}

// ========== LOCALIZATION ==========
string tr(const string& en, const string& ru) {
    return (CURRENT_LANG == LANG_RU) ? ru : en;
}

// ========== CPU & GPU DETECTION ==========
void detectCPU() {
    DETECTED_CPU_NAME = "";
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[256] = {};
        DWORD bufSize = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, &type, (LPBYTE)buf, &bufSize) == ERROR_SUCCESS) {
            string cpu = wstringToUtf8(buf);
            while (!cpu.empty() && (cpu.front() == ' ' || cpu.front() == '\t')) cpu.erase(cpu.begin());
            while (!cpu.empty() && (cpu.back() == ' ' || cpu.back() == '\t' || cpu.back() == '\r' || cpu.back() == '\n')) cpu.pop_back();
            string res;
            bool inSpace = false;
            for (char c : cpu) {
                if (c == ' ' || c == '\t') {
                    if (!inSpace) {
                        res += ' ';
                        inSpace = true;
                    }
                } else {
                    res += c;
                    inSpace = false;
                }
            }
            DETECTED_CPU_NAME = res;
        }
        RegCloseKey(hKey);
    }
}

void detectGPU() {
    detectCPU();
    HAS_NVIDIA_DEVICE = false;
    HAS_INTEL_DEVICE = false;
    HAS_AMD_DEVICE = false;
    DETECTED_GPU_NAME = "";
    DETECTED_NVIDIA_NAME = "";
    DETECTED_INTEL_NAME = "";
    DETECTED_AMD_NAME = "";

    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);
    for (int i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); i++) {
        wstring name(dd.DeviceString);
        string nameUtf8 = wstringToUtf8(name);
        string lower = nameUtf8;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("nvidia") != string::npos || lower.find("geforce") != string::npos || lower.find("rtx") != string::npos || lower.find("gtx") != string::npos) {
            HAS_NVIDIA_DEVICE = true;
            if (DETECTED_NVIDIA_NAME.empty()) DETECTED_NVIDIA_NAME = nameUtf8;
        } else if (lower.find("amd") != string::npos || lower.find("radeon") != string::npos) {
            HAS_AMD_DEVICE = true;
            if (DETECTED_AMD_NAME.empty()) DETECTED_AMD_NAME = nameUtf8;
        } else if (lower.find("intel") != string::npos) {
            HAS_INTEL_DEVICE = true;
            if (DETECTED_INTEL_NAME.empty()) DETECTED_INTEL_NAME = nameUtf8;
        }
        dd.cb = sizeof(dd);
    }

    if (FFPROBE_FOUND && !FFPROBE_PATH.empty()) {
        string cmd = "\"" + FFPROBE_PATH + "\" -hide_banner -encoders 2>&1";
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;

        HANDLE hReadPipe = NULL, hWritePipe = NULL;
        if (CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.hStdOutput = hWritePipe;
            si.hStdError = hWritePipe;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi = {};
            wstring wcmd = utf8ToWstring(cmd);
            if (CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                CloseHandle(hWritePipe);

                string output;
                char buf[4096];
                DWORD bytesRead = 0;
                while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    buf[bytesRead] = '\0';
                    output += buf;
                }

                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(hReadPipe);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                bool hasNvenc = (output.find("h264_nvenc") != string::npos) || (output.find("hevc_nvenc") != string::npos);
                bool hasAmf = (output.find("h264_amf") != string::npos) || (output.find("hevc_amf") != string::npos);
                bool hasQsv = (output.find("h264_qsv") != string::npos) || (output.find("hevc_qsv") != string::npos);

                if (!hasNvenc) HAS_NVIDIA_DEVICE = false;
                if (!hasAmf) HAS_AMD_DEVICE = false;
                if (!hasQsv) HAS_INTEL_DEVICE = false;
            } else {
                CloseHandle(hReadPipe);
                CloseHandle(hWritePipe);
            }
        }
    }

    if (HAS_NVIDIA_DEVICE && DETECTED_NVIDIA_NAME.empty()) DETECTED_NVIDIA_NAME = "NVIDIA (NVENC)";
    if (HAS_INTEL_DEVICE && DETECTED_INTEL_NAME.empty()) DETECTED_INTEL_NAME = "Intel (QSV)";
    if (HAS_AMD_DEVICE && DETECTED_AMD_NAME.empty()) DETECTED_AMD_NAME = "AMD (AMF)";

    if (HAS_NVIDIA_DEVICE) DETECTED_GPU_NAME = DETECTED_NVIDIA_NAME;
    else if (HAS_INTEL_DEVICE) DETECTED_GPU_NAME = DETECTED_INTEL_NAME;
    else if (HAS_AMD_DEVICE) DETECTED_GPU_NAME = DETECTED_AMD_NAME;

    int hwCount = (HAS_NVIDIA_DEVICE ? 1 : 0) + (HAS_INTEL_DEVICE ? 1 : 0) + (HAS_AMD_DEVICE ? 1 : 0);
    AccelMode defaultHw = ACCEL_CPU_ONLY;
    if (HAS_NVIDIA_DEVICE) defaultHw = ACCEL_NVIDIA;
    else if (HAS_INTEL_DEVICE) defaultHw = ACCEL_INTEL;
    else if (HAS_AMD_DEVICE) defaultHw = ACCEL_AMD;

    if (!CONFIG_LOADED) {
        ACCELERATION_MODE = defaultHw;
        HYBRID_GPU_CHOICE = defaultHw;
    } else {
        // Validate current ACCELERATION_MODE against available devices
        if (ACCELERATION_MODE == ACCEL_NVIDIA && !HAS_NVIDIA_DEVICE) ACCELERATION_MODE = defaultHw;
        if (ACCELERATION_MODE == ACCEL_INTEL && !HAS_INTEL_DEVICE) ACCELERATION_MODE = defaultHw;
        if (ACCELERATION_MODE == ACCEL_AMD && !HAS_AMD_DEVICE) ACCELERATION_MODE = defaultHw;
        if (ACCELERATION_MODE == ACCEL_CPU_DEC_GPU_ENC && hwCount == 0) ACCELERATION_MODE = ACCEL_CPU_ONLY;

        // Validate HYBRID_GPU_CHOICE
        if (HYBRID_GPU_CHOICE == ACCEL_NVIDIA && !HAS_NVIDIA_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_INTEL && !HAS_INTEL_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_AMD && !HAS_AMD_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_CPU_ONLY && hwCount > 0) HYBRID_GPU_CHOICE = defaultHw;
    }
}

AccelMode getActiveGpuMode() {
    if (ACCELERATION_MODE == ACCEL_CPU_DEC_GPU_ENC) {
        return HYBRID_GPU_CHOICE;
    }
    return ACCELERATION_MODE;
}

string getAccelerationModeName() {
    switch (ACCELERATION_MODE) {
        case ACCEL_CPU_ONLY:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
        case ACCEL_CPU_DEC_GPU_ENC: {
            string gpuPart = "";
            if (HYBRID_GPU_CHOICE == ACCEL_NVIDIA) gpuPart = " [NVIDIA]";
            else if (HYBRID_GPU_CHOICE == ACCEL_INTEL) gpuPart = " [Intel]";
            else if (HYBRID_GPU_CHOICE == ACCEL_AMD) gpuPart = " [AMD]";
            return tr("CPU Decoding + GPU Encoding", "Программный + Аппаратный (Декодирование CPU, Кодирование GPU)") + gpuPart;
        }
        case ACCEL_NVIDIA:
            return tr("Hardware NVIDIA (NVENC)", "Аппаратный NVIDIA (NVENC)");
        case ACCEL_INTEL:
            return tr("Hardware INTEL (QSV)", "Аппаратный INTEL (QSV)");
        case ACCEL_AMD:
            return tr("Hardware AMD (AMF)", "Аппаратный AMD (AMF)");
        default:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
    }
}

string getHWAccelArg(bool pureReencode = false) {
    if (ACCELERATION_MODE == ACCEL_CPU_ONLY || ACCELERATION_MODE == ACCEL_CPU_DEC_GPU_ENC) {
        return "";
    }
    if (pureReencode) {
        if (ACCELERATION_MODE == ACCEL_NVIDIA) return " -hwaccel cuda";
        if (ACCELERATION_MODE == ACCEL_INTEL) return " -hwaccel qsv";
    }
    return " -hwaccel auto";
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
    SetConsoleTitleW(L"MR CLI FOR FFMPEG v1.1.2");
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
    cout << "\n" << tr("Press any key...", "Нажмите любую клавишу...") << flush;
    (void)_getch();
    while (_kbhit()) (void)_getch();
    cout << "\n";
}

char getMenuChoice() {
    while (_kbhit()) {
        (void)_getch();
    }
    wint_t wc = _getwch();
    if (wc == 27) return 27; // ESC
    if (wc == 0 || wc == 0xE0) {
        (void)_getwch(); // consume extended scan code
        return 0;
    }
    char c = normalizeKeyToEnglish(wc);
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
bool fileExistsW(const wstring& wp) {
    if (wp.empty()) return false;
    DWORD attrs = GetFileAttributesW(wp.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool dirExistsW(const wstring& wp) {
    if (wp.empty()) return false;
    DWORD attrs = GetFileAttributesW(wp.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

bool fileExists(const string& p) {
    if (p.empty()) return false;
    if (fileExistsW(utf8ToWstring(p))) return true;
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(p), ec);
}

bool dirExists(const string& p) {
    if (p.empty()) return false;
    if (dirExistsW(utf8ToWstring(p))) return true;
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
    HINTERNET hSession = InternetOpenW(
        L"Mozilla/5.0 (compatible; MRCLI/1.0)",
        INTERNET_OPEN_TYPE_PRECONFIG,
        NULL, NULL, 0
    );
    if (!hSession) return false;

    wstring wUrl = utf8ToWstring(url);
    DWORD httpFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_NO_UI;
    HINTERNET hReq = InternetOpenUrlW(hSession, wUrl.c_str(), NULL, 0, httpFlags, 0);
    if (!hReq) {
        InternetCloseHandle(hSession);
        return false;
    }

    DWORD contentLength = 0;
    DWORD clLen = sizeof(contentLength);
    DWORD idx = 0;
    HttpQueryInfoW(hReq, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &clLen, &idx);

    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    string tmpFile = string(tmpPath) + "mrcli_dl.tmp";

    HANDLE hFile = CreateFileA(
        tmpFile.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hSession);
        return false;
    }

    char buf[32768];
    DWORD dwRead = 0;
    DWORD totalRead = 0;

    while (InternetReadFile(hReq, buf, sizeof(buf), &dwRead) && dwRead > 0) {
        DWORD dwWritten = 0;
        WriteFile(hFile, buf, dwRead, &dwWritten, NULL);
        totalRead += dwRead;
        if (contentLength > 0) {
            double pct = (double)totalRead / (double)contentLength * 100.0;
            double curMB = (double)totalRead / (1048576.0);
            double totMB = (double)contentLength / (1048576.0);
            char info[64];
            snprintf(info, sizeof(info), "(%.1f / %.1f MB)", curMB, totMB);
            printComponentProgress(label, pct, info);
        }
    }

    CloseHandle(hFile);
    InternetCloseHandle(hReq);
    InternetCloseHandle(hSession);

    // Move temp file to destination
    wstring wTmp = utf8ToWstring(tmpFile);
    wstring wDst = utf8ToWstring(destFile);
    MoveFileExW(wTmp.c_str(), wDst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);

    cout << "\n";
    return fileExists(destFile) && (totalRead > 1000);
}

bool extractZip(const string& zipPath, const string& destDir) {
    if (!fileExists(zipPath) || !dirExists(destDir)) return false;

    wchar_t sysDir[MAX_PATH];
    if (GetSystemDirectoryW(sysDir, MAX_PATH) <= 0) return false;

    wstring tarExe = wstring(sysDir) + L"\\tar.exe";
    if (GetFileAttributesW(tarExe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    wstring wDest = utf8ToWstring(destDir);
    while (!wDest.empty() && (wDest.back() == L'\\' || wDest.back() == L'/')) wDest.pop_back();

    wstring wZip = utf8ToWstring(zipPath);
    while (!wZip.empty() && (wZip.back() == L'\\' || wZip.back() == L'/')) wZip.pop_back();

    wstring cmd = L"\"" + tarExe + L"\" -xf \"" + wZip + L"\" -C \"" + wDest + L"\"";
    return (runProcessWait(cmd) == 0);
}

void organizeExtractedTool(const string& targetExe, const string& destDir) {
    std::error_code ec;
    fs::path targetDir = fs::weakly_canonical(fs::u8path(destDir), ec);
    if (ec || targetDir.empty()) targetDir = fs::u8path(destDir);

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

    if (!speed.empty()) line += "  " + tr("Speed: ", "Скорость: ") + speed;

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

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return 0;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    wstring wcmd = utf8ToWstring(cmd);
    if (!CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return 0;
    }
    CloseHandle(hWritePipe);

    string output;
    char buf[256];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();
    try { return stod(output); } catch (...) { return 0; }
}

// ========== GET MEDIA INFO VIA FFPROBE ==========
string getMediaInfo(const string& filePath) {
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return "[ffprobe not available]";

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_format -show_streams -of default \"" + filePath + "\"";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    wstring wcmd = utf8ToWstring(cmd);
    if (!CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return "";
    }
    CloseHandle(hWritePipe);

    string output;
    char buf[4096];
    DWORD bytesRead = 0;
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
    if (rawInfo.empty()) return tr("[No info available]", "[Информация недоступна]");

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
                string localizedType = codec_type;
                if (codec_type == "video") localizedType = tr("video", "видео");
                else if (codec_type == "audio") localizedType = tr("audio", "аудио");
                else if (codec_type == "subtitle") localizedType = tr("subtitle", "субтитры");

                result += "  " + tr("Stream #", "Поток #") + to_string(streamIdx) + " [" + localizedType + "]: " + codec_name;
                if (!width.empty() && !height.empty()) result += " " + width + "x" + height;
                if (!r_frame_rate.empty() && codec_type == "video") result += " @ " + r_frame_rate + tr(" fps", " кадр/с");
                if (!sample_rate.empty() && codec_type == "audio") result += " " + sample_rate + tr(" Hz", " Гц");
                if (!channels.empty() && codec_type == "audio") result += " " + channels + tr("ch", "кан");
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

    if (!fmt_name.empty()) result += "  " + tr("Format: ", "Формат: ") + fmt_name + "\n";
    if (!fmt_duration.empty()) {
        try {
            double dur = stod(fmt_duration);
            int h = (int)(dur / 3600);
            int m = (int)((dur - h * 3600) / 60);
            int s = (int)(dur) % 60;
            char buf[128];
            snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Длительность: %02d:%02d:%02d (%.1f сек)" : "  Duration: %02d:%02d:%02d (%.1f sec)", h, m, s, dur);
            result += buf;
            result += "\n";
        } catch (...) {}
    }
    if (!fmt_size.empty()) {
        try {
            double sz = stod(fmt_size);
            char buf[64];
            if (sz > 1024 * 1024 * 1024) snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Размер: %.2f ГБ" : "  Size: %.2f GB", sz / (1024.0 * 1024.0 * 1024.0));
            else if (sz > 1024 * 1024) snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Размер: %.2f МБ" : "  Size: %.2f MB", sz / (1024.0 * 1024.0));
            else snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Размер: %.2f КБ" : "  Size: %.2f KB", sz / 1024.0);
            result += buf;
            result += "\n";
        } catch (...) {}
    }
    if (!fmt_bit_rate.empty()) {
        try {
            double br = stod(fmt_bit_rate);
            char buf[64];
            snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Битрейт: %.0f кбит/с" : "  Bitrate: %.0f kbps", br / 1000.0);
            result += buf;
            result += "\n";
        } catch (...) {}
    }

    return result;
}

// ========== EXECUTE FFMPEG WITH LIVE PROGRESS ==========
bool execFFmpegWithProgress(const wstring& cmdLine, double totalDuration = 0) {
    g_ffmpegEscaped = false;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        printColor(tr("[ERROR] Failed to create process pipe!", "[ОШИБКА] Не удалось создать канал процесса!"), RED);
        return false;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;

    PROCESS_INFORMATION pi = {};
    wstring mutableCmd = cmdLine;

    BOOL success = CreateProcessW(
        NULL, &mutableCmd[0], NULL, NULL,
        TRUE, 0, NULL, NULL, &si, &pi
    );
    CloseHandle(hWritePipe);

    if (!success) {
        CloseHandle(hReadPipe);
        printColor(tr("[ERROR] Failed to launch FFmpeg!", "[ОШИБКА] Не удалось запустить FFmpeg!"), RED);
        return false;
    }

    string line;
    bool progressActive = false;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (true) {
        if (_kbhit()) {
            int key = _getch();
            if (key == 27) {
                if (progressActive) { cout << endl; progressActive = false; }
                g_ffmpegEscaped = true;
                TerminateProcess(pi.hProcess, 1);
                break;
            }
        }

        DWORD avail = 0;
        if (!PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) || avail == 0) {
            if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0) {
                PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL);
                if (avail == 0) break;
            }
            continue;
        }

        if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0) break;

        for (DWORD i = 0; i < bytesRead; i++) {
            char c = buffer[i];
            if (c == '\r' || c == '\n') {
                if (!line.empty()) {
                    string timeStr, speed;
                    if (parseFFmpegProgress(line, timeStr, speed)) {
                        double cur = timeToSeconds(timeStr);
                        printFFmpegProgressBar(cur, totalDuration, speed);
                        progressActive = true;
                    }
                    else if (line.find("Error") != string::npos || line.find("error") != string::npos) {
                        if (line.find("encoder") == string::npos) {
                            if (progressActive) { cout << endl; progressActive = false; }
                            printColor(tr("[ERROR] ", "[ОШИБКА] ") + line, RED);
                        }
                    }
                    line.clear();
                }
            }
            else {
                line += c;
            }
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
string openFolderDialog(const wchar_t* title = L"Select folder") {
    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) return "";

    DWORD dwOptions;
    pfd->GetOptions(&dwOptions);
    pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(title);

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) { pfd->Release(); return ""; }

    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    if (FAILED(hr)) { pfd->Release(); return ""; }

    wchar_t* pPath = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
    psi->Release();
    pfd->Release();

    if (FAILED(hr) || !pPath) return "";

    wstring result(pPath);
    CoTaskMemFree(pPath);
    if (result.back() != L'\\' && result.back() != L'/') result += L'\\';
    return wstringToUtf8(result);
}

string openFileDialogMedia(const wchar_t* title = nullptr) {
    OPENFILENAMEW ofn = { 0 };
    wchar_t fn[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = (CURRENT_LANG == LANG_RU) ?
                      L"Медиа файлы\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff\0"
                      L"Видео файлы\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v\0"
                      L"Аудио файлы\0*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma\0"
                      L"Изображения\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff\0"
                      L"Все файлы (*.*)\0*.*\0" :
                      L"Media Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff\0"
                      L"Video Files\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v\0"
                      L"Audio Files\0*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma\0"
                      L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff\0"
                      L"All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    wstring defaultTitle = (CURRENT_LANG == LANG_RU) ? L"Выберите медиафайл" : L"Select media file";
    ofn.lpstrTitle = title ? title : defaultTitle.c_str();

    if (GetOpenFileNameW(&ofn)) {
        return wstringToUtf8(wstring(fn));
    }
    return "";
}

string openMultiFileDialog(vector<string>& files) {
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
      << "KEEP_METADATA=" << (KEEP_METADATA ? "true" : "false") << "\n"
      << "VIDEO_CODEC_ASK=" << (VIDEO_CODEC_ASK ? "true" : "false") << "\n"
      << "AUDIO_CODEC_ASK=" << (AUDIO_CODEC_ASK ? "true" : "false") << "\n"
      << "LANGUAGE=" << (CURRENT_LANG == LANG_RU ? "ru" : "en") << "\n"
      << "WATERMARK_PATH=" << WATERMARK_PATH << "\n"
      << "WATERMARK_POSITION=" << WATERMARK_POSITION << "\n"
      << "ACCELERATION_MODE=" << (int)ACCELERATION_MODE << "\n"
      << "HYBRID_GPU_CHOICE=" << (int)HYBRID_GPU_CHOICE << "\n";
    f.close();
}

void loadConfig() {
    string configPath = CONFIG_PATH + "mr-config.txt";

    if (fileExists(configPath)) {
        ifstream f(configPath);
        if (f.is_open()) {
            CONFIG_LOADED = true;
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
                else if (l.find("KEEP_METADATA=") == 0) KEEP_METADATA = (l.substr(14) == "true");
                else if (l.find("VIDEO_CODEC_ASK=") == 0) VIDEO_CODEC_ASK = (l.substr(16) == "true");
                else if (l.find("AUDIO_CODEC_ASK=") == 0) AUDIO_CODEC_ASK = (l.substr(16) == "true");
                else if (l.find("LANGUAGE=") == 0) CURRENT_LANG = (l.substr(9) == "ru") ? LANG_RU : LANG_EN;
                else if (l.find("WATERMARK_PATH=") == 0) WATERMARK_PATH = l.substr(15);
                else if (l.find("WATERMARK_POSITION=") == 0) WATERMARK_POSITION = l.substr(19);
                else if (l.find("ACCELERATION_MODE=") == 0) { try { ACCELERATION_MODE = (AccelMode)stoi(l.substr(18)); } catch (...) { ACCELERATION_MODE = ACCEL_CPU_ONLY; } }
                else if (l.find("HYBRID_GPU_CHOICE=") == 0) { try { HYBRID_GPU_CHOICE = (AccelMode)stoi(l.substr(18)); } catch (...) { HYBRID_GPU_CHOICE = ACCEL_CPU_ONLY; } }
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
    AccelMode activeGpu = getActiveGpuMode();
    string hwEncoder;
    if (activeGpu == ACCEL_NVIDIA) {
        hwEncoder = "nvenc";
    } else if (activeGpu == ACCEL_AMD) {
        hwEncoder = "amf";
    } else if (activeGpu == ACCEL_INTEL) {
        hwEncoder = "qsv";
    } else {
        hwEncoder = "";
    }

    if (OUTPUT_FORMAT.find("H.264") != string::npos) {
        if (hwEncoder == "nvenc") return "-c:v h264_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v h264_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v h264_qsv";
        return "-c:v libx264";
    }
    if (OUTPUT_FORMAT.find("H.265") != string::npos || OUTPUT_FORMAT.find("HEVC") != string::npos) {
        if (hwEncoder == "nvenc") return "-c:v hevc_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v hevc_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v hevc_qsv";
        return "-c:v libx265";
    }
    if (OUTPUT_FORMAT.find("AV1") != string::npos) {
        if (hwEncoder == "nvenc") return "-c:v av1_nvenc -rc:v constqp";
        if (hwEncoder == "qsv") return "-c:v av1_qsv";
        return "-c:v libaom-av1";
    }
    if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-c:v libvpx-vp9";
    if (OUTPUT_FORMAT.find("MPEG4") != string::npos) return "-c:v mpeg4";

    if (!hwEncoder.empty()) {
        if (hwEncoder == "nvenc") return "-c:v h264_nvenc -rc:v constqp";
        if (hwEncoder == "amf") return "-c:v h264_amf -quality:v balanced";
        if (hwEncoder == "qsv") return "-c:v h264_qsv";
    }
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

string getVideoQualityArgs(const string& crfVal) {
    AccelMode activeGpu = getActiveGpuMode();
    bool isHW = (activeGpu == ACCEL_NVIDIA || activeGpu == ACCEL_AMD || activeGpu == ACCEL_INTEL);
    if (isHW) return "-qp " + crfVal;
    return "-crf " + crfVal;
}

string getVideoPresetArgs(const string& overridePreset = "") {
    const string& p = overridePreset.empty() ? PRESET : overridePreset;
    AccelMode activeGpu = getActiveGpuMode();

    if (activeGpu == ACCEL_NVIDIA) {
        if (p == "ultrafast" || p == "superfast" || p == "veryfast") return "-preset p1";
        if (p == "faster" || p == "fast") return "-preset p4";
        if (p == "medium") return "-preset p5";
        if (p == "slow" || p == "slower" || p == "veryslow") return "-preset p7";
        return "-preset p5";
    }
    if (activeGpu == ACCEL_AMD) {
        if (p == "ultrafast" || p == "superfast" || p == "veryfast" || p == "faster" || p == "fast") return "-preset speed";
        if (p == "medium") return "-preset balanced";
        if (p == "slow" || p == "slower" || p == "veryslow") return "-preset quality";
        return "-preset balanced";
    }
    if (activeGpu == ACCEL_INTEL) {
        if (p == "ultrafast" || p == "superfast") return "-preset veryfast";
        if (p == "veryfast" || p == "faster") return "-preset faster";
        if (p == "fast") return "-preset fast";
        if (p == "medium") return "-preset medium";
        if (p == "slow") return "-preset slow";
        if (p == "slower" || p == "veryslow") return "-preset slower";
        return "-preset medium";
    }
    return "-preset " + p;
}

string buildOutputPath(const string& inputPath, const string& suffix = "", const string& forceExt = "") {
    fs::path inPath = fs::u8path(inputPath);
    string stem = inPath.stem().u8string();
    string ext = forceExt.empty() ? getOutputExtension() : forceExt;
    string usedSuffix = (!OVERWRITE_FILES) ? suffix : "";
    string outName = stem + usedSuffix + "." + ext;
    string outPath = OUTPUT_PATH + outName;

    if (!OVERWRITE_FILES) {
        std::error_code ec;
        auto absOut = fs::weakly_canonical(fs::absolute(fs::u8path(outPath), ec), ec);
        auto absIn = fs::weakly_canonical(fs::absolute(fs::u8path(inputPath), ec), ec);
        if (!ec && absOut == absIn) {
            outName = stem + (suffix.empty() ? "_converted" : suffix) + "." + ext;
            outPath = OUTPUT_PATH + outName;
        }

        int counter = 1;
        while (fileExists(outPath)) {
            outPath = OUTPUT_PATH + stem + usedSuffix + "_" + to_string(counter) + "." + ext;
            counter++;
        }
    }
    return outPath;
}

struct FFmpegTarget {
    string targetPath;
    string writePath;
    bool isTemp = false;
};

FFmpegTarget prepareFFmpegTarget(const string& targetPath, const vector<string>& inputPaths) {
    FFmpegTarget ft;
    ft.targetPath = targetPath;
    ft.writePath = targetPath;
    ft.isTemp = false;

    wstring wTarget = utf8ToWstring(targetPath);
    if (wTarget.empty()) return ft;

    bool conflictsWithInput = false;
    for (const auto& inp : inputPaths) {
        if (inp.empty()) continue;
        wstring wInp = utf8ToWstring(inp);
        if (_wcsicmp(wTarget.c_str(), wInp.c_str()) == 0) {
            conflictsWithInput = true;
            break;
        }

        std::error_code ec;
        auto absTarget = fs::weakly_canonical(fs::u8path(targetPath), ec);
        auto absIn = fs::weakly_canonical(fs::u8path(inp), ec);
        if (!ec && absTarget == absIn) {
            conflictsWithInput = true;
            break;
        }
    }

    if (conflictsWithInput) {
        // Target is identical to one of the input files (in-place overwrite).
        // Write to a temporary file first in the same directory, then replace atomically on success.
        fs::path p = fs::u8path(targetPath);
        wstring stemW = p.stem().wstring();
        wstring extW = p.extension().wstring();
        fs::path tempP = p.parent_path() / (stemW + L".~mr_tmp" + extW);
        ft.writePath = wstringToUtf8(tempP.wstring());
        ft.isTemp = true;
    }

    return ft;
}

bool finalizeFFmpegTarget(const FFmpegTarget& ft, bool success) {
    if (!ft.isTemp) return success;

    wstring wTemp = utf8ToWstring(ft.writePath);
    wstring wTarget = utf8ToWstring(ft.targetPath);

    if (success && fileExistsW(wTemp)) {
        // Remove read-only attributes if present
        DWORD targetAttrs = GetFileAttributesW(wTarget.c_str());
        if (targetAttrs != INVALID_FILE_ATTRIBUTES && (targetAttrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wTarget.c_str(), targetAttrs & ~FILE_ATTRIBUTE_READONLY);
        }
        DWORD tempAttrs = GetFileAttributesW(wTemp.c_str());
        if (tempAttrs != INVALID_FILE_ATTRIBUTES && (tempAttrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wTemp.c_str(), tempAttrs & ~FILE_ATTRIBUTE_READONLY);
        }

        bool replaced = false;

        // Stage 1: MoveFileExW with MOVEFILE_REPLACE_EXISTING
        for (int retry = 0; retry < 15; retry++) {
            if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                replaced = true;
                break;
            }
            Sleep(100);
        }

        // Stage 2: ReplaceFileW (standard Windows file replacement API)
        if (!replaced) {
            for (int retry = 0; retry < 10; retry++) {
                if (ReplaceFileW(wTarget.c_str(), wTemp.c_str(), NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
                    replaced = true;
                    break;
                }
                Sleep(100);
            }
        }

        // Stage 3: Delete target then MoveFileW
        if (!replaced) {
            DeleteFileW(wTarget.c_str());
            for (int retry = 0; retry < 10; retry++) {
                if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
                    replaced = true;
                    break;
                }
                Sleep(100);
            }
        }

        // Stage 4: CopyFileW fallback (guaranteed to work across network SMB shares / FAT32)
        if (!replaced) {
            for (int retry = 0; retry < 10; retry++) {
                if (CopyFileW(wTemp.c_str(), wTarget.c_str(), FALSE)) {
                    DeleteFileW(wTemp.c_str());
                    replaced = true;
                    break;
                }
                Sleep(150);
            }
        }

        if (!replaced) {
            DWORD err = GetLastError();
            printColor("\n[ERROR] Failed to replace target file (Win32 error code: " + to_string(err) + ")", RED);
            DeleteFileW(wTemp.c_str());
            return false;
        }

        return fileExistsW(wTarget);
    } else {
        DeleteFileW(wTemp.c_str());
        return false;
    }
}

// ========== ARROW-KEY SELECTION MENU ==========
int arrowSelect(const string& title, const string& description, const vector<string>& options, int currentIdx, const vector<string>& hints = {}) {
    int selected = (currentIdx >= 0 && currentIdx < (int)options.size()) ? currentIdx : 0;
    while (true) {
        clearScreen();
        printColor("========================================", CYAN);
        printColor(" " + title, CYAN);
        printColor("========================================", CYAN);
        if (!description.empty()) {
            cout << "\n" << description << "\n";
        }
        cout << "\n";
        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) {
                setColor(GREEN);
                cout << " > " << options[i] << endl;
                setColor(WHITE);
            } else {
                cout << "   " << options[i] << endl;
            }
        }
        if (!hints.empty() && selected >= 0 && selected < (int)hints.size() && !hints[selected].empty()) {
            cout << "\n";
            printColor("----------------------------------------------------------------------", CYAN);
            setColor(YELLOW);
            cout << " [i] " << hints[selected] << "\n";
            setColor(WHITE);
            printColor("----------------------------------------------------------------------", CYAN);
        }
        cout << "\n" << tr("Arrow keys to select, Enter to confirm, ESC to cancel",
                           "Стрелки для выбора, Enter для подтверждения, ESC для отмены") << endl;
        int key = _getch();
        if (key == 27) return -1;
        if (key == 13) return selected;
        if (key == 0 || key == 0xE0) {
            int scan = _getch();
            if (scan == 72) selected = (selected > 0) ? selected - 1 : (int)options.size() - 1;
            else if (scan == 80) selected = (selected < (int)options.size() - 1) ? selected + 1 : 0;
        }
    }
}

// ========== ALWAYS-ASK CODEC PROMPTS ==========
bool promptVideoCodecSettings() {
    if (!VIDEO_CODEC_ASK) return true;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" VIDEO CODEC SETTINGS REVIEW", " ПРОВЕРКА НАСТРОЕК ВИДЕОКОДЕКА"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Current video settings:", "Текущие настройки видео:") << "\n"
         << "  " << tr("Format: ", "Формат: ") << OUTPUT_FORMAT << "\n"
         << "  " << tr("CRF: ", "CRF: ") << CRF_VALUE << "\n"
         << "  " << tr("Preset: ", "Пресет: ") << PRESET << "\n"
         << "  " << tr("Resolution: ", "Разрешение: ") << OUTPUT_RESOLUTION << "\n"
         << "  " << tr("Program acceleration: ", "Программное ускорение: ") << getAccelerationModeName() << "\n"
         << "\n" << tr("Proceed with these settings? [Y/N]: ", "Продолжить с этими настройками? [Y/N]: ");
    char ch = getMenuChoice();
    if (ch == 27) return false;
    if (ch == 'n') {
        cout << "N\n";
        printColor(tr("[INFO] Change settings in the Settings menu and try again.",
                      "[ИНФО] Измените настройки в меню Настроек и повторите."), YELLOW);
        waitForKey();
        return false;
    }
    cout << "Y\n";
    return true;
}

bool promptAudioCodecSettings() {
    if (!AUDIO_CODEC_ASK) return true;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" AUDIO CODEC SETTINGS REVIEW", " ПРОВЕРКА НАСТРОЕК АУДИОКОДЕКА"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Current audio settings:", "Текущие настройки аудио:") << "\n"
         << "  " << tr("Format: ", "Формат: ") << OUTPUT_FORMAT << "\n"
         << "  " << tr("Bitrate: ", "Битрейт: ") << AUDIO_BITRATE << " kbps\n"
         << "\n" << tr("Proceed with these settings? [Y/N]: ", "Продолжить с этими настройками? [Y/N]: ");
    char ch = getMenuChoice();
    if (ch == 27) return false;
    if (ch == 'n') {
        cout << "N\n";
        printColor(tr("[INFO] Change settings in the Settings menu and try again.",
                      "[ИНФО] Измените настройки в меню Настроек и повторите."), YELLOW);
        waitForKey();
        return false;
    }
    cout << "Y\n";
    return true;
}

// ========== MEDIA PROPERTIES STRUCT (FOR COMPARISON) ==========
struct MediaProperties {
    string path;
    string format;
    double durationSec = 0;
    string durationStr;
    string sizeStr;
    double sizeBytes = 0;
    string bitrateStr;
    double bitrateVal = 0;
    string videoCodec;
    string width, height;
    string fps;
    string audioCodec;
    string sampleRate;
    string channels;
};

MediaProperties parseMediaProperties(const string& filePath) {
    MediaProperties mp;
    mp.path = filePath;

    std::error_code ec;
    auto fsize = fs::file_size(fs::u8path(filePath), ec);
    if (!ec) {
        mp.sizeBytes = (double)fsize;
        char buf[64];
        if (fsize > 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f GB", (double)fsize / (1024.0*1024.0*1024.0));
        else if (fsize > 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f MB", (double)fsize / (1024.0*1024.0));
        else snprintf(buf, sizeof(buf), "%.2f KB", (double)fsize / 1024.0);
        mp.sizeStr = buf;
    }

    string rawInfo = getMediaInfo(filePath);
    if (rawInfo.empty()) return mp;

    istringstream iss(rawInfo);
    string line;
    bool inStream = false;

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line == "[STREAM]") { inStream = true; continue; }
        if (line == "[/STREAM]") { inStream = false; continue; }
        if (line == "[FORMAT]" || line == "[/FORMAT]") continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (inStream) {
            if (key == "codec_name") {
                if (mp.videoCodec.empty() && mp.width.empty()) mp.videoCodec = val;
                else if (mp.audioCodec.empty()) mp.audioCodec = val;
            }
            else if (key == "codec_type") {
                if (val == "video" && !mp.videoCodec.empty() && mp.audioCodec.empty()) { /* already set */ }
                else if (val == "audio") { /* will be set on next codec_name */ }
            }
            else if (key == "width" && mp.width.empty()) mp.width = val;
            else if (key == "height" && mp.height.empty()) mp.height = val;
            else if (key == "r_frame_rate" && mp.fps.empty()) mp.fps = val;
            else if (key == "sample_rate" && mp.sampleRate.empty()) mp.sampleRate = val;
            else if (key == "channels" && mp.channels.empty()) mp.channels = val;
        } else {
            if (key == "format_name") mp.format = val;
            else if (key == "duration") {
                try {
                    double dur = stod(val);
                    mp.durationSec = dur;
                    int h = (int)(dur / 3600);
                    int m = (int)((dur - h * 3600) / 60);
                    int s = (int)(dur) % 60;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
                    mp.durationStr = buf;
                } catch (...) {}
            }
            else if (key == "bit_rate") {
                try {
                    double br = stod(val);
                    mp.bitrateVal = br;
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                    mp.bitrateStr = buf;
                } catch (...) {}
            }
        }
    }
    return mp;
}

// ========== COMPARE TWO FILES ==========
void compareFiles() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" COMPARE TWO MEDIA FILES", " СРАВНЕНИЕ ДВУХ МЕДИАФАЙЛОВ"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select FIRST file...", "Выберите ПЕРВЫЙ файл...") << "\n";
    string file1 = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите первый файл" : L"Select first file");
    if (file1.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("File 1: ", "Файл 1: ") + file1, GREEN);

    cout << "\n" << tr("Select SECOND file...", "Выберите ВТОРОЙ файл...") << "\n";
    string file2 = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите второй файл" : L"Select second file");
    if (file2.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("File 2: ", "Файл 2: ") + file2, GREEN);

    cout << "\n" << tr("Analyzing files...", "Анализ файлов...") << "\n";
    MediaProperties mp1 = parseMediaProperties(file1);
    MediaProperties mp2 = parseMediaProperties(file2);

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" COMPARISON TABLE", " ТАБЛИЦА СРАВНЕНИЯ"), CYAN);
    printColor("========================================", CYAN);

    // Column widths
    string col = "%-14s %-24s %-24s\n";
    cout << "\n";
    printf(col.c_str(), "", tr("File 1", "Файл 1").c_str(), tr("File 2", "Файл 2").c_str());
    printf("%-14s %-24s %-24s\n", "----------", "------------------------", "------------------------");

    auto row = [&](const string& label, const string& v1, const string& v2) {
        printf("%-14s %-24s %-24s\n", label.c_str(), (v1.empty() ? "N/A" : v1.c_str()), (v2.empty() ? "N/A" : v2.c_str()));
    };

    row(tr("Format", "Формат"), mp1.format, mp2.format);
    row(tr("Duration", "Длительность"), mp1.durationStr, mp2.durationStr);
    string res1 = (!mp1.width.empty() && !mp1.height.empty()) ? mp1.width + "x" + mp1.height : "";
    string res2 = (!mp2.width.empty() && !mp2.height.empty()) ? mp2.width + "x" + mp2.height : "";
    row(tr("Resolution", "Разрешение"), res1, res2);
    row(tr("Video codec", "Видеокодек"), mp1.videoCodec, mp2.videoCodec);
    row(tr("Audio codec", "Аудиокодек"), mp1.audioCodec, mp2.audioCodec);
    row(tr("Bitrate", "Битрейт"), mp1.bitrateStr, mp2.bitrateStr);
    row(tr("Size", "Размер"), mp1.sizeStr, mp2.sizeStr);
    row(tr("FPS", "FPS"), mp1.fps, mp2.fps);
    row(tr("Sample rate", "Частота"), mp1.sampleRate.empty() ? "" : mp1.sampleRate + " Hz",
                                      mp2.sampleRate.empty() ? "" : mp2.sampleRate + " Hz");
    row(tr("Channels", "Каналы"), mp1.channels, mp2.channels);

    // Differences section
    cout << "\n";
    printColor("========================================", YELLOW);
    printColor(tr(" DIFFERENCES", " РАЗЛИЧИЯ"), YELLOW);
    printColor("========================================", YELLOW);
    cout << "\n";

    bool hasDiff = false;
    auto diff = [&](const string& label, const string& v1, const string& v2, double n1 = 0, double n2 = 0, bool higherBetter = true) {
        if (v1 == v2 && (v1.empty() || !v1.empty())) {
            if (v1 == v2) return;
        }
        if (v1.empty() && v2.empty()) return;
        if (v1 == v2) return;
        hasDiff = true;
        cout << " " << label << ": ";
        if (n1 > 0 && n2 > 0) {
            bool firstBetter = higherBetter ? (n1 >= n2) : (n1 <= n2);
            setColor(firstBetter ? GREEN : RED);
            cout << v1;
            setColor(WHITE);
            cout << " vs ";
            setColor(firstBetter ? RED : GREEN);
            cout << v2;
            setColor(WHITE);
        } else {
            setColor(CYAN);
            cout << v1;
            setColor(WHITE);
            cout << " vs ";
            setColor(CYAN);
            cout << v2;
            setColor(WHITE);
        }
        cout << "\n";
    };

    diff(tr("Format", "Формат"), mp1.format, mp2.format);
    diff(tr("Duration", "Длительность"), mp1.durationStr, mp2.durationStr, mp1.durationSec, mp2.durationSec, true);
    diff(tr("Resolution", "Разрешение"), res1, res2);
    diff(tr("Video codec", "Видеокодек"), mp1.videoCodec, mp2.videoCodec);
    diff(tr("Audio codec", "Аудиокодек"), mp1.audioCodec, mp2.audioCodec);
    diff(tr("Bitrate", "Битрейт"), mp1.bitrateStr, mp2.bitrateStr, mp1.bitrateVal, mp2.bitrateVal, true);
    diff(tr("Size", "Размер"), mp1.sizeStr, mp2.sizeStr, mp1.sizeBytes, mp2.sizeBytes, false);
    diff(tr("FPS", "FPS"), mp1.fps, mp2.fps);
    diff(tr("Sample rate", "Частота"), mp1.sampleRate, mp2.sampleRate);
    diff(tr("Channels", "Каналы"), mp1.channels, mp2.channels);

    if (!hasDiff) {
        printColor(tr("  No significant differences found.", "  Значительных различий не найдено."), GREEN);
    }

    waitForKey();
}

// ========== BATCH VIDEO COMPRESSION ==========
void batchCompressVideo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH VIDEO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select folder with video files...", "Выберите папку с видеофайлами...") << "\n";
    string folder = openFolderDialog(utf8ToWstring(tr("Select folder with video files", "Выберите папку с видеофайлами")).c_str());
    if (folder.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    vector<string> videoExts = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".ts", ".m4v"};
    vector<string> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        string ext = entry.path().extension().u8string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& ve : videoExts) {
            if (ext == ve) { files.push_back(entry.path().u8string()); break; }
        }
    }

    if (files.empty()) {
        printColor(tr("[ERROR] No video files found in the selected folder!",
                      "[ОШИБКА] Видеофайлы в выбранной папке не найдены!"), RED);
        waitForKey();
        return;
    }

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH VIDEO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << tr("Settings:", "Настройки:") << "\n"
         << "  " << tr("Format: ", "Формат: ") << OUTPUT_FORMAT << "\n"
         << "  CRF: " << CRF_VALUE << "\n"
         << "  " << tr("Preset: ", "Пресет: ") << PRESET << "\n"
         << "  " << tr("Program acceleration: ", "Программное ускорение: ") << getAccelerationModeName() << "\n";

    cout << "\n" << tr("Would you like to change any settings before proceeding? [Y/N]: ",
                       "Хотите изменить настройки перед началом? [Y/N]: ");
    char ch = getMenuChoice();
    if (ch == 27) return;
    if (ch == 'y') {
        cout << "Y\n";
        printColor(tr("[INFO] Please change settings in the Settings menu and restart batch operation.",
                      "[ИНФО] Измените настройки в меню Настроек и перезапустите пакетную операцию."), YELLOW);
        waitForKey();
        return;
    }
    cout << "N\n";

    bool deleteOrig = false;
    if (!OVERWRITE_FILES) {
        cout << "\n" << tr("Delete original files after conversion? [Y/N]: ",
                           "Удалить оригиналы после преобразования? [Y/N]: ");
        char chDel = getMenuChoice();
        if (chDel == 27) return;
        deleteOrig = (chDel == 'y');
        cout << (deleteOrig ? "Y\n" : "N\n");
    }

    int success = 0, fail = 0;
    size_t i = 0;
    while (i < files.size()) {
        cout << "\n";
        printColor("========================================", CYAN);
        char label[128];
        snprintf(label, sizeof(label), " %s %zu / %zu - %s",
                 tr("Processing", "Обработка").c_str(), i + 1, files.size(),
                 fs::u8path(files[i]).filename().u8string().c_str());
        printColor(label, CYAN);
        printColor("========================================", CYAN);

        double duration = getMediaDuration(files[i]);
        string outPath = buildOutputPath(files[i], "_compressed");
        auto ft = prepareFFmpegTarget(outPath, {files[i]});

        wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        cmd += utf8ToWstring(getHWAccelArg(true));
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(files[i])) + L"\"";
        cmd += L" " + utf8ToWstring(getVideoCodecArgs());
        cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
        cmd += L" " + utf8ToWstring(getVideoPresetArgs());
        cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a 128k";
        if (OVERWRITE_FILES) cmd += L" -y";
        cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (g_ffmpegEscaped) {
            finalizeFFmpegTarget(ft, false);
            cout << "\n";
            printColor("========================================", YELLOW);
            printColor(tr(" PAUSED - Batch Processing Interrupted", " ПАУЗА - Пакетная обработка прервана"), YELLOW);
            printColor("========================================", YELLOW);
            cout << "\n" << tr("File: ", "Файл: ") << fs::u8path(files[i]).filename().u8string()
                 << " (" << (i + 1) << "/" << files.size() << ")\n";
            cout << "\n 1. " << tr("Skip this file", "Пропустить этот файл")
                 << "\n 2. " << tr("Cancel entire batch", "Отменить весь пакет")
                 << "\n 3. " << tr("Retry this file", "Повторить этот файл")
                 << "\n\n" << tr("Your choice: ", "Ваш выбор: ");

            char pauseCh = getMenuChoice();
            if (pauseCh == '2' || pauseCh == 27) {
                cout << (pauseCh == 27 ? "ESC" : "2") << "\n";
                printColor(tr("[INFO] Batch processing cancelled.", "[ИНФО] Пакетная обработка отменена."), YELLOW);
                fail++;
                break;
            }
            else if (pauseCh == '3') {
                cout << "3\n";
                continue;
            }
            else {
                cout << "1\n";
                printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                fail++;
                i++;
                continue;
            }
        }

        if (ok) {
            success++;
            printColor(tr("[OK] Done", "[OK] Готово"), GREEN);
            if (deleteOrig && !ft.isTemp) {
                std::error_code dec;
                fs::remove(fs::u8path(files[i]), dec);
            }
        }
        else { fail++; printColor(tr("[ERROR] Failed", "[ОШИБКА] Не удалось"), RED); }
        i++;
    }

    cout << "\n";
    printColor("========================================", GREEN);
    char summary[128];
    snprintf(summary, sizeof(summary), " %s: %d %s, %d %s",
             tr("Result", "Результат").c_str(), success,
             tr("success", "успешно").c_str(), fail,
             tr("failed", "ошибок").c_str());
    printColor(summary, fail > 0 ? YELLOW : GREEN);
    printColor("========================================", GREEN);
    cout << "\n";
    printColor("========================================", CYAN);
    printColor(tr(" All files saved to \"", " Все файлы сохранены по пути \"") + OUTPUT_PATH + "\"", CYAN);
    printColor("========================================", CYAN);
    waitForKey();
}

// ========== BATCH AUDIO COMPRESSION ==========
void batchCompressAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH AUDIO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ АУДИО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select folder with audio files...", "Выберите папку с аудиофайлами...") << "\n";
    string folder = openFolderDialog(utf8ToWstring(tr("Select folder with audio files", "Выберите папку с аудиофайлами")).c_str());
    if (folder.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    vector<string> audioExts = {".mp3", ".m4a", ".aac", ".wav", ".ogg", ".flac", ".opus", ".wma"};
    vector<string> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::u8path(folder), ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        string ext = entry.path().extension().u8string();
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& ae : audioExts) {
            if (ext == ae) { files.push_back(entry.path().u8string()); break; }
        }
    }

    if (files.empty()) {
        printColor(tr("[ERROR] No audio files found in the selected folder!",
                      "[ОШИБКА] Аудиофайлы в выбранной папке не найдены!"), RED);
        waitForKey();
        return;
    }

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH AUDIO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ АУДИО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << tr("Settings:", "Настройки:") << "\n"
         << "  " << tr("Audio bitrate: ", "Битрейт аудио: ") << AUDIO_BITRATE << " kbps\n";

    cout << "\n" << tr("Would you like to change any settings before proceeding? [Y/N]: ",
                       "Хотите изменить настройки перед началом? [Y/N]: ");
    char ch = getMenuChoice();
    if (ch == 27) return;
    if (ch == 'y') {
        cout << "Y\n";
        printColor(tr("[INFO] Please change settings in the Settings menu and restart batch operation.",
                      "[ИНФО] Измените настройки в меню Настроек и перезапустите пакетную операцию."), YELLOW);
        waitForKey();
        return;
    }
    cout << "N\n";

    bool deleteOrig = false;
    if (!OVERWRITE_FILES) {
        cout << "\n" << tr("Delete original files after conversion? [Y/N]: ",
                           "Удалить оригиналы после преобразования? [Y/N]: ");
        char chDel = getMenuChoice();
        if (chDel == 27) return;
        deleteOrig = (chDel == 'y');
        cout << (deleteOrig ? "Y\n" : "N\n");
    }

    int success = 0, fail = 0;
    size_t i = 0;
    while (i < files.size()) {
        cout << "\n";
        printColor("========================================", CYAN);
        char label[128];
        snprintf(label, sizeof(label), " %s %zu / %zu - %s",
                 tr("Processing", "Обработка").c_str(), i + 1, files.size(),
                 fs::u8path(files[i]).filename().u8string().c_str());
        printColor(label, CYAN);
        printColor("========================================", CYAN);

        double duration = getMediaDuration(files[i]);
        string outPath = buildOutputPath(files[i], "_compressed", "mp3");
        auto ft = prepareFFmpegTarget(outPath, {files[i]});

        wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(files[i])) + L"\"";
        cmd += L" -c:a libmp3lame -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
        if (OVERWRITE_FILES) cmd += L" -y";
        cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (g_ffmpegEscaped) {
            finalizeFFmpegTarget(ft, false);
            cout << "\n";
            printColor("========================================", YELLOW);
            printColor(tr(" PAUSED - Batch Processing Interrupted", " ПАУЗА - Пакетная обработка прервана"), YELLOW);
            printColor("========================================", YELLOW);
            cout << "\n" << tr("File: ", "Файл: ") << fs::u8path(files[i]).filename().u8string()
                 << " (" << (i + 1) << "/" << files.size() << ")\n";
            cout << "\n 1. " << tr("Skip this file", "Пропустить этот файл")
                 << "\n 2. " << tr("Cancel entire batch", "Отменить весь пакет")
                 << "\n 3. " << tr("Retry this file", "Повторить этот файл")
                 << "\n\n" << tr("Your choice: ", "Ваш выбор: ");

            char pauseCh = getMenuChoice();
            if (pauseCh == '2' || pauseCh == 27) {
                cout << (pauseCh == 27 ? "ESC" : "2") << "\n";
                printColor(tr("[INFO] Batch processing cancelled.", "[ИНФО] Пакетная обработка отменена."), YELLOW);
                fail++;
                break;
            }
            else if (pauseCh == '3') {
                cout << "3\n";
                continue;
            }
            else {
                cout << "1\n";
                printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                fail++;
                i++;
                continue;
            }
        }

        if (ok) {
            success++;
            printColor(tr("[OK] Done", "[OK] Готово"), GREEN);
            if (deleteOrig && !ft.isTemp) {
                std::error_code dec;
                fs::remove(fs::u8path(files[i]), dec);
            }
        }
        else { fail++; printColor(tr("[ERROR] Failed", "[ОШИБКА] Не удалось"), RED); }
        i++;
    }

    cout << "\n";
    printColor("========================================", GREEN);
    char summary[128];
    snprintf(summary, sizeof(summary), " %s: %d %s, %d %s",
             tr("Result", "Результат").c_str(), success,
             tr("success", "успешно").c_str(), fail,
             tr("failed", "ошибок").c_str());
    printColor(summary, fail > 0 ? YELLOW : GREEN);
    printColor("========================================", GREEN);
    cout << "\n";
    printColor("========================================", CYAN);
    printColor(tr(" All files saved to \"", " Все файлы сохранены по пути \"") + OUTPUT_PATH + "\"", CYAN);
    printColor("========================================", CYAN);
    waitForKey();
}

// ========== OPERATION 1: CONVERT FORMAT ==========
void convertFormat() {
    if (!promptVideoCodecSettings()) return;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CONVERT VIDEO/AUDIO FORMAT", " КОНВЕРТАЦИЯ ФОРМАТА"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Select input file (or press ESC to cancel)...", "Выберите файл (или нажмите ESC для отмены)...") << "\n";

    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите файл для конвертации" : L"Select file to convert");
    if (inputFile.empty()) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    // Show file info
    string info = getMediaInfo(inputFile);
    if (!info.empty()) {
        printColor("\n" + tr("--- File Info ---", "--- Информация о файле ---"), CYAN);
        cout << formatMediaInfoDisplay(info);
        printColor("-----------------", CYAN);
    }

    double duration = getMediaDuration(inputFile);
    string outPath = buildOutputPath(inputFile, "_converted");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    printColor("\n" + tr("Output: ", "Выход: ") + outPath, GREEN);
    printColor(tr("Format: ", "Формат: ") + OUTPUT_FORMAT, GREEN);

    cout << "\n" << tr("1. Start conversion\n0. Cancel (ESC)\n\nYour choice: ", "1. Начать конвертацию\n0. Отмена (ESC)\n\nВаш выбор: ");
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += utf8ToWstring(getHWAccelArg(true));
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs());
    cmd += L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";

    if (VIDEO_BITRATE != "auto") {
        cmd += L" -b:v " + utf8ToWstring(VIDEO_BITRATE) + L"k";
    } else {
        cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    }

    cmd += L" " + utf8ToWstring(getVideoPresetArgs());

    if (OUTPUT_RESOLUTION != "original") {
        cmd += L" -vf scale=-2:" + utf8ToWstring(OUTPUT_RESOLUTION);
    }
    if (OUTPUT_FPS != "original") {
        cmd += L" -r " + utf8ToWstring(OUTPUT_FPS);
    }
    if (KEEP_METADATA) cmd += L" -map_metadata 0";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CONVERTING...", " КОНВЕРТАЦИЯ..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Conversion completed successfully!", "[OK] Конвертация успешно завершена!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Conversion failed!", "[ОШИБКА] Ошибка конвертации!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 2: TRIM / CUT VIDEO ==========
void trimVideo() {
    if (!promptVideoCodecSettings()) return;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" TRIM / CUT VIDEO", " ОБРЕЗКА ВИДЕО"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Select input file...", "Выберите файл...") << "\n";

    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите файл для обрезки" : L"Select file to trim");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);
    double duration = getMediaDuration(inputFile);
    if (duration > 0) {
        int m = (int)(duration / 60), s = (int)duration % 60;
        char buf[64];
        snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Длительность: %02d:%02d (%.1f сек)" : "Duration: %02d:%02d (%.1f sec)", m, s, duration);
        printColor(string(buf), CYAN);
    }

    string startTime, endTime;
    cout << "\n" << tr("Start time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n", "Время начала (ЧЧ:ММ:СС или ММ:СС или секунды, ESC для отмены):\n");
    if (!inputLineWithEscape(startTime, "> ")) { return; }
    cout << tr("End time (HH:MM:SS or MM:SS or seconds, ESC to cancel):\n", "Время окончания (ЧЧ:ММ:СС или ММ:СС или секунды, ESC для отмены):\n");
    if (!inputLineWithEscape(endTime, "> ")) { return; }

    if (startTime.empty() || endTime.empty()) {
        printColor(tr("[ERROR] Both start and end times are required!", "[ОШИБКА] Требуется указать время начала и окончания!"), RED);
        waitForKey();
        return;
    }

    string outPath = buildOutputPath(inputFile, "_trimmed");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});
    printColor("\n" + tr("Output: ", "Выход: ") + outPath, GREEN);

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -ss " + utf8ToWstring(startTime);
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -to " + utf8ToWstring(endTime);
    cmd += L" -c copy";  // Stream copy for speed
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" TRIMMING...", " ОБРЕЗКА..."), CYAN);
    printColor("========================================", CYAN);
    printColor(tr("From ", "С ") + startTime + tr(" to ", " по ") + endTime, CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, 0);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Trim completed successfully!", "[OK] Обрезка успешно завершена!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Trim failed!", "[ОШИБКА] Ошибка обрезки!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 3: EXTRACT AUDIO ==========
void extractAudio() {
    if (!promptAudioCodecSettings()) return;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACT AUDIO FROM VIDEO", " ИЗВЛЕЧЕНИЕ АУДИО ИЗ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Select video file...", "Выберите видеофайл...") << "\n";

    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл для извлечения звука" : L"Select video to extract audio from");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);
    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Output audio format:\n1) MP3\n2) M4A (AAC)\n3) WAV\n4) FLAC\n5) OGG (Vorbis)\n0) Cancel (ESC)\n\nYour choice: ",
                       "Формат аудио на выходе:\n1) MP3\n2) M4A (AAC)\n3) WAV\n4) FLAC\n5) OGG (Vorbis)\n0) Отмена (ESC)\n\nВаш выбор: ");
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
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, "_audio", ext);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vn " + utf8ToWstring(codecArgs);
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACTING AUDIO...", " ИЗВЛЕЧЕНИЕ АУДИО..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Audio extraction completed!", "[OK] Аудио успешно извлечено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Audio extraction failed!", "[ОШИБКА] Ошибка извлечения аудио!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 4: MERGE VIDEO + AUDIO ==========
void mergeVideoAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" MERGE VIDEO + AUDIO", " ОБЪЕДИНЕНИЕ ВИДЕО И АУДИО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select VIDEO file...\n", "Выберите ВИДЕОФАЙЛ...\n");
    string videoFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (videoFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Video: ", "Видео: ") + videoFile, GREEN);

    cout << "\n" << tr("Select AUDIO file...\n", "Выберите АУДИОФАЙЛ...\n");
    string audioFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите аудиофайл" : L"Select audio file");
    if (audioFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Audio: ", "Аудио: ") + audioFile, GREEN);

    double duration = getMediaDuration(videoFile);
    string outPath = buildOutputPath(videoFile, "_merged");
    auto ft = prepareFFmpegTarget(outPath, {videoFile, audioFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(videoFile)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(audioFile)) + L"\"";
    cmd += L" -c:v copy -c:a aac -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    cmd += L" -map 0:v:0 -map 1:a:0 -shortest";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" MERGING VIDEO + AUDIO...", " ОБЪЕДИНЕНИЕ ВИДЕО И АУДИО..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Merge completed successfully!", "[OK] Объединение успешно завершено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Merge failed!", "[ОШИБКА] Ошибка объединения!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 5: CHANGE RESOLUTION ==========
void changeResolution() {
    if (!promptVideoCodecSettings()) return;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CHANGE VIDEO RESOLUTION", " ИЗМЕНЕНИЕ РАЗРЕШЕНИЯ"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...", "Выберите видеофайл...") << "\n";
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Target resolution:\n1) 3840x2160 (4K)\n2) 2560x1440 (2K)\n3) 1920x1080 (FullHD)\n4) 1280x720 (HD)\n5) 854x480\n6) 640x360\n7) Custom\n0) Cancel (ESC)\n\nYour choice: ",
                       "Целевое разрешение:\n1) 3840x2160 (4K)\n2) 2560x1440 (2K)\n3) 1920x1080 (FullHD)\n4) 1280x720 (HD)\n5) 854x480\n6) 640x360\n7) Своё разрешение\n0) Отмена (ESC)\n\nВаш выбор: ");
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
            cout << tr("Width: ", "Ширина: ");
            if (!inputLineWithEscape(w, "")) return;
            cout << tr("Height: ", "Высота: ");
            if (!inputLineWithEscape(h, "")) return;
            scale = w + ":" + h;
            break;
        }
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    string safeSuffix = "_resized";
    string outPath = buildOutputPath(inputFile, safeSuffix);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += utf8ToWstring(getHWAccelArg());
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vf \"scale=" + utf8ToWstring(scale) + L":flags=lanczos\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CHANGING RESOLUTION TO ", " ИЗМЕНЕНИЕ РАЗРЕШЕНИЯ НА ") + scale + "...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Resolution changed successfully!", "[OK] Разрешение успешно изменено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Resolution change failed!", "[ОШИБКА] Ошибка изменения разрешения!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 6: CHANGE SPEED ==========
void changeSpeed() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CHANGE VIDEO SPEED", " ИЗМЕНЕНИЕ СКОРОСТИ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Speed multiplier:\n1) 0.25x (very slow)\n2) 0.5x (slow)\n3) 0.75x\n4) 1.5x\n5) 2x (fast)\n6) 4x (very fast)\n7) Custom\n0) Cancel (ESC)\n\nYour choice: ",
                       "Коэффициент скорости:\n1) 0.25x (очень медленно)\n2) 0.5x (медленно)\n3) 0.75x\n4) 1.5x\n5) 2x (быстро)\n6) 4x (очень быстро)\n7) Своя скорость\n0) Отмена (ESC)\n\nВаш выбор: ");
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
            cout << tr("Enter speed multiplier (e.g. 1.5): ", "Введите коэффициент скорости (например 1.5): ");
            if (!inputLineWithEscape(s, "")) return;
            try { speed = stod(s); } catch (...) {
                printColor(tr("[ERROR] Invalid number!", "[ОШИБКА] Некорректное число!"), RED); waitForKey(); return;
            }
            break;
        }
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    if (speed <= 0 || speed > 100) {
        printColor(tr("[ERROR] Speed must be between 0.01 and 100!", "[ОШИБКА] Скорость должна быть от 0.01 до 100!"), RED);
        waitForKey();
        return;
    }

    char speedBuf[32];
    snprintf(speedBuf, sizeof(speedBuf), "%.2f", speed);
    string outPath = buildOutputPath(inputFile, "_speed" + string(speedBuf));
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

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

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vf \"" + utf8ToWstring(string(vfBuf)) + L"\"";
    cmd += L" -af \"" + utf8ToWstring(atempoChain) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    char speedLabel[64];
    snprintf(speedLabel, sizeof(speedLabel), (CURRENT_LANG == LANG_RU) ? " ИЗМЕНЕНИЕ СКОРОСТИ НА %.2fx..." : " CHANGING SPEED TO %.2fx...", speed);
    printColor(speedLabel, CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration / speed);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Speed change completed!", "[OK] Скорость успешно изменена!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Speed change failed!", "[ОШИБКА] Ошибка изменения скорости!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 7: ADD WATERMARK ==========
void addWatermark() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" ADD WATERMARK / OVERLAY", " ДОБАВЛЕНИЕ ВОДЯНОГО ЗНАКА"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Video: ", "Видео: ") + inputFile, GREEN);

    cout << "\n" << tr("Select watermark image (PNG recommended)...\n", "Выберите изображение водяного знака (рекомендуется PNG)...\n");
    string wmFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите изображение водяного знака" : L"Select watermark image");
    if (wmFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Watermark: ", "Водяной знак: ") + wmFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Watermark position:\n1) Top-left\n2) Top-right\n3) Bottom-left\n4) Bottom-right\n5) Center\n0) Cancel (ESC)\n\nYour choice: ",
                       "Позиция водяного знака:\n1) Вверху слева\n2) Вверху справа\n3) Внизу слева\n4) Внизу справа\n5) По центру\n0) Отмена (ESC)\n\nВаш выбор: ");
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
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, "_watermarked");
    auto ft = prepareFFmpegTarget(outPath, {inputFile, wmFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(wmFile)) + L"\"";
    cmd += L" -filter_complex \"" + utf8ToWstring(overlay) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" ADDING WATERMARK...", " ДОБАВЛЕНИЕ ВОДЯНОГО ЗНАКА..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Watermark added successfully!", "[OK] Водяной знак успешно добавлен!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Watermark failed!", "[ОШИБКА] Ошибка добавления водяного знака!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 8: COMPRESS VIDEO ==========
void compressVideo() {
    if (!promptVideoCodecSettings()) return;
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" COMPRESS VIDEO", " СЖАТИЕ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...", "Выберите видеофайл...") << "\n";
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл для сжатия" : L"Select video file to compress");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Compression level:\n1) Light (CRF 20 - high quality, larger file)\n2) Medium (CRF 26 - balanced)\n3) Heavy (CRF 32 - smaller file, lower quality)\n4) Extreme (CRF 38 - minimum size)\n5) Custom CRF\n0) Cancel (ESC)\n\nYour choice: ",
                       "Степень сжатия:\n1) Легкое (CRF 20 - высокое качество, больший размер)\n2) Среднее (CRF 26 - баланс)\n3) Сильное (CRF 32 - меньший размер, ниже качество)\n4) Максимальное (CRF 38 - минимальный размер)\n5) Свой CRF\n0) Отмена (ESC)\n\nВаш выбор: ");
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
            cout << tr("Enter CRF value (0-51, lower = better quality): ", "Введите значение CRF (0-51, меньше = лучше качество): ");
            if (!inputLineWithEscape(s, "")) return;
            crf = s;
            break;
        }
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    std::error_code inSizeEc;
    auto inSize = fs::file_size(fs::u8path(inputFile), inSizeEc);

    string outPath = buildOutputPath(inputFile, "_compressed");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += utf8ToWstring(getHWAccelArg(true));
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(crf));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs("slower"));
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a 128k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" COMPRESSING (CRF ", " СЖАТИЕ (CRF ") + crf + ")...", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        // Show size comparison
        std::error_code ec;
        auto outSize = fs::file_size(fs::u8path(outPath), ec);
        if (inSize > 0 && outSize > 0) {
            double ratio = (1.0 - (double)outSize / (double)inSize) * 100.0;
            char buf[128];
            snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Размер: %.2f МБ -> %.2f МБ (на %.1f%% меньше)" : "Size: %.2f MB -> %.2f MB (%.1f%% smaller)",
                (double)inSize / (1024.0*1024.0), (double)outSize / (1024.0*1024.0), ratio);
            printColor(string(buf), GREEN);
        }
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Compression completed!", "[OK] Сжатие успешно завершено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Compression failed!", "[ОШИБКА] Ошибка сжатия!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 9: ROTATE / FLIP VIDEO ==========
void rotateVideo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" ROTATE / FLIP VIDEO", " ПОВОРОТ / ОТРАЖЕНИЕ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Transform:\n1) Rotate 90 clockwise\n2) Rotate 90 counter-clockwise\n3) Rotate 180\n4) Flip horizontal (mirror)\n5) Flip vertical\n0) Cancel (ESC)\n\nYour choice: ",
                       "Преобразование:\n1) Повернуть на 90 по часовой стрелке\n2) Повернуть на 90 против часовой стрелки\n3) Повернуть на 180\n4) Отразить по горизонтали (зеркало)\n5) Отразить по вертикали\n0) Отмена (ESC)\n\nВаш выбор: ");
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
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    string outPath = buildOutputPath(inputFile, suffix);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vf \"" + utf8ToWstring(vf) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" TRANSFORMING VIDEO...", " ПРЕОБРАЗОВАНИЕ ВИДЕО..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Transform completed!", "[OK] Преобразование завершено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Transform failed!", "[ОШИБКА] Ошибка преобразования!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 10: CREATE GIF ==========
void createGif() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CREATE GIF FROM VIDEO", " СОЗДАНИЕ GIF ИЗ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);
    if (duration > 0) {
        int m = (int)(duration / 60), s = (int)duration % 60;
        char buf[64];
        snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Длительность: %02d:%02d (%.1f сек)" : "Duration: %02d:%02d (%.1f sec)", m, s, duration);
        printColor(string(buf), CYAN);
    }

    string startTime, gifDuration;
    cout << "\n" << tr("Start time (HH:MM:SS or seconds, Enter=start):\n", "Время начала (ЧЧ:ММ:СС или секунды, Enter=с начала):\n");
    if (!inputLineWithEscape(startTime, "> ")) { startTime = "0"; }
    if (startTime.empty()) startTime = "0";
    cout << tr("Duration in seconds (Enter=5):\n", "Длительность в секундах (Enter=5):\n");
    if (!inputLineWithEscape(gifDuration, "> ")) { gifDuration = "5"; }
    if (gifDuration.empty()) gifDuration = "5";

    cout << "\n" << tr("GIF width (Enter=480):\n", "Ширина GIF (Enter=480):\n");
    string gifWidth;
    if (!inputLineWithEscape(gifWidth, "> ")) { gifWidth = "480"; }
    if (gifWidth.empty()) gifWidth = "480";

    cout << "\n" << tr("FPS (Enter=15):\n", "FPS (Enter=15):\n");
    string gifFps;
    if (!inputLineWithEscape(gifFps, "> ")) { gifFps = "15"; }
    if (gifFps.empty()) gifFps = "15";

    string outPath = buildOutputPath(inputFile, "", "gif");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    // Two-pass GIF creation for good quality
    string palettePath = OUTPUT_PATH + "palette_tmp.png";

    // Pass 1: Generate palette
    wstring cmd1 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd1 += L" -ss " + utf8ToWstring(startTime);
    cmd1 += L" -t " + utf8ToWstring(gifDuration);
    cmd1 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd1 += L" -vf \"fps=" + utf8ToWstring(gifFps) + L",scale=" + utf8ToWstring(gifWidth) + L":-1:flags=lanczos,palettegen\"";
    cmd1 += L" -y \"" + utf8ToWstring(getSafeFFmpegPath(palettePath)) + L"\"";

    // Pass 2: Create GIF with palette
    wstring cmd2 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd2 += L" -ss " + utf8ToWstring(startTime);
    cmd2 += L" -t " + utf8ToWstring(gifDuration);
    cmd2 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd2 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(palettePath)) + L"\"";
    cmd2 += L" -filter_complex \"fps=" + utf8ToWstring(gifFps) + L",scale=" + utf8ToWstring(gifWidth) + L":-1:flags=lanczos[x];[x][1:v]paletteuse\"";
    cmd2 += L" -y \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CREATING GIF...", " СОЗДАНИЕ GIF..."), CYAN);
    printColor("========================================", CYAN);

    printColor("\n" + tr("Pass 1: Generating palette...", "Проход 1: Генерация палитры..."), CYAN);
    bool ok = execFFmpegWithProgress(cmd1, 0);

    if (ok) {
        printColor("\n" + tr("Pass 2: Creating GIF...", "Проход 2: Создание GIF..."), CYAN);
        ok = execFFmpegWithProgress(cmd2, 0);
    }
    ok = finalizeFFmpegTarget(ft, ok);

    // Cleanup palette
    std::error_code ec;
    fs::remove(fs::u8path(palettePath), ec);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] GIF created successfully!", "[OK] GIF успешно создан!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] GIF creation failed!", "[ОШИБКА] Ошибка создания GIF!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 11: CONCATENATE FILES ==========
void concatenateFiles() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CONCATENATE (JOIN) FILES", " СКЛЕИВАНИЕ (ОБЪЕДИНЕНИЕ) ФАЙЛОВ"), CYAN);
    printColor("========================================", CYAN);

    vector<string> files;
    cout << "\n" << tr("Add files one by one. Press ESC when done.\n", "Добавляйте файлы по одному. Нажмите ESC по завершении.\n");

    int fileNum = 1;
    while (true) {
        cout << "\n" << tr("Select file #", "Выберите файл #") << fileNum << tr(" (ESC to finish)...\n", " (ESC для завершения)...\n");
        string f = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите файл для добавления" : L"Select file to add");
        if (f.empty()) break;
        files.push_back(f);
        printColor(tr("Added: ", "Добавлен: ") + f, GREEN);
        fileNum++;
    }

    if (files.size() < 2) {
        printColor(tr("[ERROR] Need at least 2 files to concatenate!", "[ОШИБКА] Требуется минимум 2 файла для склеивания!"), RED);
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
    auto ft = prepareFFmpegTarget(outPath, files);

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -f concat -safe 0 -i \"" + utf8ToWstring(getSafeFFmpegPath(listPath)) + L"\"";
    cmd += L" -c copy";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CONCATENATING ", " СКЛЕИВАНИЕ ") + to_string(files.size()) + tr(" FILES...", " ФАЙЛОВ..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, totalDuration);
    ok = finalizeFFmpegTarget(ft, ok);

    // Cleanup
    std::error_code ec;
    fs::remove(fs::u8path(listPath), ec);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Concatenation completed!", "[OK] Склеивание успешно завершено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Concatenation failed!", "[ОШИБКА] Ошибка склеивания!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 12: STRIP AUDIO (MUTE) ==========
void stripAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" STRIP AUDIO (REMOVE SOUND)", " УДАЛЕНИЕ ЗВУКА ИЗ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);
    string outPath = buildOutputPath(inputFile, "_nosound");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -c:v copy -an";  // Copy video, no audio
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" STRIPPING AUDIO...", " УДАЛЕНИЕ ЗВУКА..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Audio stripped successfully!", "[OK] Звук успешно удален!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Strip audio failed!", "[ОШИБКА] Ошибка удаления звука!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 13: FILE INFO ==========
void showFileInfo() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" MEDIA FILE INFORMATION", " ИНФОРМАЦИЯ О МЕДИАФАЙЛЕ"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select media file...\n", "Выберите медиафайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите медиафайл" : L"Select media file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    printColor("\n" + tr("File: ", "Файл: ") + inputFile, GREEN);

    // File size
    std::error_code ec;
    auto fsize = fs::file_size(fs::u8path(inputFile), ec);
    if (!ec) {
        char buf[64];
        if (fsize > 1024ULL * 1024 * 1024) snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Размер: %.2f ГБ" : "Size: %.2f GB", (double)fsize / (1024.0*1024.0*1024.0));
        else if (fsize > 1024 * 1024) snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Размер: %.2f МБ" : "Size: %.2f MB", (double)fsize / (1024.0*1024.0));
        else snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "Размер: %.2f КБ" : "Size: %.2f KB", (double)fsize / 1024.0);
        printColor(string(buf), WHITE);
    }

    string info = getMediaInfo(inputFile);
    if (!info.empty()) {
        printColor("\n" + tr("--- Detailed Info ---", "--- Подробная информация ---"), CYAN);
        cout << formatMediaInfoDisplay(info);
        printColor("---------------------", CYAN);
    } else {
        printColor(tr("[WARNING] Could not retrieve media info", "[ВНИМАНИЕ] Не удалось получить информацию о медиа"), YELLOW);
    }

    waitForKey();
}

// ========== OPERATION 14: EXTRACT FRAMES (SCREENSHOTS) ==========
void extractFrames() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACT FRAMES (SCREENSHOTS)", " ИЗВЛЕЧЕНИЕ КАДРОВ (СКРИНШОТЫ)"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);

    double duration = getMediaDuration(inputFile);

    cout << "\n" << tr("Extraction mode:\n1) Single frame at time position\n2) One frame per second\n3) One frame per N seconds\n4) Every Nth frame\n0) Cancel (ESC)\n\nYour choice: ",
                       "Режим извлечения:\n1) Один кадр по времени\n2) Один кадр в секунду\n3) Один кадр каждые N секунд\n4) Каждый N-й кадр\n0) Отмена (ESC)\n\nВаш выбор: ");
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    // Create output subfolder
    fs::path inPath = fs::u8path(inputFile);
    string framesDir = OUTPUT_PATH + inPath.stem().u8string() + "_frames\\";
    createDirRecursive(framesDir);

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";

    switch (ch) {
        case '1': {
            string timePos;
            cout << tr("Time position (HH:MM:SS or seconds): ", "Позиция по времени (ЧЧ:ММ:СС или секунды): ");
            if (!inputLineWithEscape(timePos, "")) return;
            cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            cmd += L" -ss " + utf8ToWstring(timePos);
            cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
            cmd += L" -vframes 1 -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(framesDir + "frame.png")) + L"\"";
            break;
        }
        case '2': {
            cmd += L" -vf fps=1 -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(framesDir + "frame_%04d.png")) + L"\"";
            break;
        }
        case '3': {
            string interval;
            cout << tr("Interval in seconds: ", "Интервал в секундах: ");
            if (!inputLineWithEscape(interval, "")) return;
            cmd += L" -vf \"fps=1/" + utf8ToWstring(interval) + L"\" -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(framesDir + "frame_%04d.png")) + L"\"";
            break;
        }
        case '4': {
            string nth;
            cout << tr("Extract every Nth frame (e.g. 30): ", "Извлекать каждый N-й кадр (напр. 30): ");
            if (!inputLineWithEscape(nth, "")) return;
            cmd += L" -vf \"select=not(mod(n\\," + utf8ToWstring(nth) + L"))\" -vsync vfr -q:v 2";
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(framesDir + "frame_%04d.png")) + L"\"";
            break;
        }
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACTING FRAMES...", " ИЗВЛЕЧЕНИЕ КАДРОВ..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Frames extracted!", "[OK] Кадры успешно извлечены!"), GREEN);
        printColor(tr("Output folder: ", "Папка вывода: ") + framesDir, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Frame extraction failed!", "[ОШИБКА] Ошибка извлечения кадров!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 15: ADD SUBTITLES ==========
void addSubtitles() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" ADD SUBTITLES (BURN-IN)", " ВШИВАНИЕ СУБТИТРОВ (ХАРДСАБ)"), CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Select video file...\n", "Выберите видеофайл...\n");
    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл" : L"Select video file");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Video: ", "Видео: ") + inputFile, GREEN);

    cout << "\n" << tr("Select subtitle file (SRT/ASS/SSA)...\n", "Выберите файл субтитров (SRT/ASS/SSA)...\n");
    // Use a custom file dialog for subtitles
    OPENFILENAMEW ofn = { 0 };
    wchar_t fn[MAX_PATH] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = (CURRENT_LANG == LANG_RU) ?
                      L"Файлы субтитров\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0Все файлы (*.*)\0*.*\0" :
                      L"Subtitle Files\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    wstring subTitle = (CURRENT_LANG == LANG_RU) ? L"Выберите файл субтитров" : L"Select subtitle file";
    ofn.lpstrTitle = subTitle.c_str();

    string subFile;
    if (GetOpenFileNameW(&ofn)) {
        subFile = wstringToUtf8(wstring(fn));
    }
    if (subFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }
    printColor(tr("Subtitles: ", "Субтитры: ") + subFile, GREEN);

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
    auto ft = prepareFFmpegTarget(outPath, {inputFile, subFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -vf \"subtitles='" + utf8ToWstring(escaped) + L"'\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs()) + L" -b:a " + utf8ToWstring(AUDIO_BITRATE) + L"k";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BURNING SUBTITLES...", " ВШИВАНИЕ СУБТИТРОВ..."), CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Subtitles added successfully!", "[OK] Субтитры успешно добавлены!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Adding subtitles failed!", "[ОШИБКА] Ошибка вшивания субтитров!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== SETTINGS MENUS ==========
void selectOutputFormat() {
    vector<string> formatKeys = {
        "MP4(H.264)", "MP4(H.265/HEVC)", "MP4(AV1)",
        "MKV(H.264)", "MKV(H.265/HEVC)",
        "WEBM(VP9)", "WEBM(AV1)", "MOV(H.264)", "AVI(MPEG4)",
        "MP3", "M4A(AAC)", "WAV", "FLAC", "OGG(Vorbis)"
    };

    vector<string> options = {
        tr("[Video] MP4 (H.264 / AVC)       - Maximum compatibility",
           "[Видео] MP4 (H.264 / AVC)       - Максимальная совместимость"),
        tr("[Video] MP4 (H.265 / HEVC)      - High efficiency (1080p / 4K)",
           "[Видео] MP4 (H.265 / HEVC)      - Высокая эффективность (1080p / 4K)"),
        tr("[Video] MP4 (AV1)               - Next-gen best compression",
           "[Видео] MP4 (AV1)               - Новейшее ультра-сжатие"),
        tr("[Video] MKV (H.264)             - Universal film container",
           "[Видео] MKV (H.264)             - Универсальный контейнер для кино"),
        tr("[Video] MKV (H.265 / HEVC)      - Modern container with HEVC",
           "[Видео] MKV (H.265 / HEVC)      - Современный контейнер с HEVC"),
        tr("[Video] WEBM (VP9)              - Web video (YouTube standard)",
           "[Видео] WEBM (VP9)              - Веб-видео (стандарт YouTube)"),
        tr("[Video] WEBM (AV1)              - Ultra-efficient web video",
           "[Видео] WEBM (AV1)              - Ультра-сжатие для веб-видео"),
        tr("[Video] MOV (H.264)             - Apple QuickTime & editing",
           "[Видео] MOV (H.264)             - Apple QuickTime и видеомонтаж"),
        tr("[Video] AVI (MPEG-4)            - Legacy car / DVD players",
           "[Видео] AVI (MPEG-4)            - Старые DVD-плееры и магнитолы"),
        tr("[Audio] MP3                     - Universal audio format",
           "[Аудио] MP3                     - Универсальный аудиоформат"),
        tr("[Audio] M4A (AAC)               - High quality (Apple / YouTube)",
           "[Аудио] M4A (AAC)               - Качественный звук (Apple / YouTube)"),
        tr("[Audio] WAV                     - Uncompressed studio PCM",
           "[Аудио] WAV                     - Несжатый студийный звук (PCM)"),
        tr("[Audio] FLAC                    - Lossless compression (100% quality)",
           "[Аудио] FLAC                    - Сжатие без потерь (100% качество)"),
        tr("[Audio] OGG (Vorbis)            - Open-source audio format",
           "[Аудио] OGG (Vorbis)            - Свободный аудиоформат")
    };

    vector<string> hints = {
        tr("Maximum compatibility. Plays on all PCs, smartphones, smart TVs, consoles and browsers. Best default choice.",
           "Максимальная совместимость. Воспроизводится на любых смартфонах, ТВ, плеерах и в браузерах. Рекомендуется по умолчанию."),
        tr("Modern high-efficiency codec. Files are 30-50% smaller than H.264 with identical visual quality. Best for 1080p, 2K and 4K.",
           "Современный кодек высокого сжатия. Файлы на 30-50% меньше H.264 при том же качестве. Идеально для 1080p, 2K и 4K."),
        tr("Next-generation royalty-free codec. Delivers best compression ratio, but takes longer to encode. Supported by modern devices.",
           "Открытый кодек нового поколения. Максимальное сжатие, но кодируется медленнее. Поддерживается современными устройствами."),
        tr("Matroska container. Supports multiple audio tracks, embedded subtitles and chapters. Perfect for storing movies and TV series.",
           "Контейнер Matroska. Поддерживает множество аудиодорожек, встроенные субтитры и главы. Идеально для хранения фильмов."),
        tr("Matroska container with HEVC codec. Compact file size for heavy movies with multi-track audio and subtitle support.",
           "Контейнер Matroska с кодеком HEVC. Компактный размер для тяжелых фильмов с поддержкой нескольких дорожек и субтитров."),
        tr("Google open web video format. Natively supported by all web browsers and YouTube. Good alternative to MP4.",
           "Открытый формат Google для веб. Нативно поддерживается всеми браузерами и YouTube. Хорошая альтернатива MP4."),
        tr("Next-gen web format with ultra-compression AV1. Future of internet video and streaming media.",
           "Новейший веб-формат со сверхвысоким сжатием AV1. Будущее интернет-видео и онлайн-стриминга."),
        tr("Apple QuickTime container. Native for Apple ecosystem (macOS, iPhone, iPad) and video editors (Final Cut, Premiere, DaVinci).",
           "Контейнер Apple QuickTime. Родной формат для macOS, iPhone/iPad и видеоредакторов (Final Cut, Premiere, DaVinci)."),
        tr("Legacy AVI container. Use only if needed for playback on older DVD players, TV sets or car stereos.",
           "Классический устаревший формат. Используйте только для совместимости со старыми DVD-плеерами и автомагнитолами."),
        tr("Audio only. World's most popular audio format. Plays on virtually any device, portable speaker or car radio.",
           "Только звук. Самый популярный в мире аудиоформат. Воспроизводится на любых устройствах, колонках и магнитолах."),
        tr("Audio only. Advanced Audio Coding. Noticeably better clarity and detail than MP3 at the same bitrate. Standard for Apple & YouTube.",
           "Только звук. Современный формат AAC. Звучит чище и детальнее, чем MP3 при том же битрейте. Стандарт для Apple и YouTube."),
        tr("Audio only. Uncompressed PCM audio. Exact bit-for-bit studio master copy without loss, but files are very large.",
           "Только звук. Несжатый студийный звук (PCM). Точная побитовая копия без потерь, но файлы занимают много места."),
        tr("Audio only. Lossless audio compression. Cuts file size roughly in half while preserving 100% of the original studio quality.",
           "Только звук. Сжатие без потерь (Lossless). Уменьшает размер примерно в 2 раза с сохранением 100% студийного качества."),
        tr("Audio only. Open-source patent-free format Ogg Vorbis with great sound quality. Widely used in games and Linux.",
           "Только звук. Свободный формат Ogg Vorbis с отличным качеством звучания. Популярен в играх и на Linux.")
    };

    int cur = 0;
    for (int i = 0; i < (int)formatKeys.size(); i++) {
        if (formatKeys[i] == OUTPUT_FORMAT) { cur = i; break; }
    }

    string desc = tr(
        "Choose an output container and codec for video or audio processing.\n"
        "Use arrow keys to navigate and view detailed explanations of each format below.",
        "Выберите контейнер и кодек для обработки видео или аудио.\n"
        "Используйте стрелки для навигации и чтения подробного описания каждого формата внизу.");

    int sel = arrowSelect(tr("OUTPUT FORMAT", "ФОРМАТ ВЫВОДА"), desc, options, cur, hints);
    if (sel >= 0) {
        OUTPUT_FORMAT = formatKeys[sel];
        saveConfig();
        printColor(tr("[OK] Output format set to ", "[OK] Формат установлен: ") + OUTPUT_FORMAT, GREEN);
        waitForKey();
    }
}

void selectResolution() {
    vector<string> options = {
        tr("Original (no change)", "Оригинал (без изменений)"),
        "2160p (4K)", "1440p (2K)", "1080p (FullHD)",
        "720p (HD)", "480p", "360p"
    };
    vector<string> values = {"original", "2160", "1440", "1080", "720", "480", "360"};
    int cur = 0;
    for (int i = 0; i < (int)values.size(); i++) {
        if (values[i] == OUTPUT_RESOLUTION) { cur = i; break; }
    }
    string desc = tr(
        "The output video resolution.\n"
        "'Original' keeps the source resolution unchanged.\n"
        "Downscaling reduces file size but lowers visual quality.",
        "Разрешение выходного видео.\n"
        "'Оригинал' сохраняет исходное разрешение.\n"
        "Уменьшение снижает размер файла, но ухудшает качество.");
    int sel = arrowSelect(tr("RESOLUTION", "РАЗРЕШЕНИЕ"), desc, options, cur);
    if (sel >= 0) {
        OUTPUT_RESOLUTION = values[sel];
        saveConfig();
        printColor(tr("[OK] Resolution set to ", "[OK] Разрешение: ") + OUTPUT_RESOLUTION, GREEN);
        waitForKey();
    }
}

void selectFPS() {
    vector<string> options = {
        tr("Original (no change)", "Оригинал (без изменений)"),
        "60fps", "30fps", "24fps", "15fps"
    };
    vector<string> values = {"original", "60", "30", "24", "15"};
    int cur = 0;
    for (int i = 0; i < (int)values.size(); i++) {
        if (values[i] == OUTPUT_FPS) { cur = i; break; }
    }
    string desc = tr(
        "Frames per second. 'Original' keeps the source framerate.\n"
        "Lower FPS reduces file size.\n"
        "24fps = cinema, 30fps = TV, 60fps = smooth.",
        "Кадров в секунду. 'Оригинал' сохраняет исходный FPS.\n"
        "Меньше FPS — меньше размер файла.\n"
        "24fps = кино, 30fps = ТВ, 60fps = плавное.");
    int sel = arrowSelect(tr("FPS", "FPS"), desc, options, cur);
    if (sel >= 0) {
        OUTPUT_FPS = values[sel];
        saveConfig();
        printColor(tr("[OK] FPS set to ", "[OK] FPS: ") + OUTPUT_FPS, GREEN);
        waitForKey();
    }
}

void selectPreset() {
    vector<string> options = {
        "ultrafast", "superfast", "veryfast", "faster", "fast",
        "medium", "slow", "slower", "veryslow"
    };
    int cur = 0;
    for (int i = 0; i < (int)options.size(); i++) {
        if (options[i] == PRESET) { cur = i; break; }
    }
    string desc = tr(
        "Encoding speed vs compression trade-off.\n"
        "Faster presets encode quickly but produce larger files.\n"
        "Slower presets take longer but produce smaller files with same quality.",
        "Баланс скорости кодирования и сжатия.\n"
        "Быстрые пресеты кодируют быстрее, но файлы больше.\n"
        "Медленные пресеты дольше, но файлы меньше при том же качестве.");
    int sel = arrowSelect(tr("ENCODING PRESET", "ПРЕСЕТ КОДИРОВАНИЯ"), desc, options, cur);
    if (sel >= 0) {
        PRESET = options[sel];
        saveConfig();
        printColor(tr("[OK] Preset set to ", "[OK] Пресет: ") + PRESET, GREEN);
        waitForKey();
    }
}

void selectCRF() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CRF VALUE", " ЗНАЧЕНИЕ CRF"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr(
        "CRF (Constant Rate Factor): 0-51\n"
        "Lower = better quality, larger file\n"
        "0 = lossless, 18 = visually lossless, 23 = default, 28 = small, 51 = worst",
        "CRF (Constant Rate Factor): 0-51\n"
        "Ниже = лучше качество, больше файл\n"
        "0 = без потерь, 18 = визуально без потерь, 23 = стандарт, 28 = маленький, 51 = худший") << "\n";
    cout << "\n" << tr("Current CRF: ", "Текущий CRF: ") << CRF_VALUE
         << "\n\n" << tr("Enter new CRF value (0-51, ESC to cancel): ",
                         "Введите CRF (0-51, ESC для отмены): ");
    string val;
    if (!inputLineWithEscape(val, "")) return;
    try {
        int v = stoi(val);
        if (v < 0 || v > 51) { printColor(tr("[ERROR] CRF must be 0-51!", "[ОШИБКА] CRF должен быть 0-51!"), RED); waitForKey(); return; }
        CRF_VALUE = val;
        saveConfig();
        printColor(tr("[OK] CRF set to ", "[OK] CRF: ") + CRF_VALUE, GREEN);
    } catch (...) {
        printColor(tr("[ERROR] Invalid number!", "[ОШИБКА] Неверное число!"), RED);
    }
    waitForKey();
}

void selectAudioBitrate() {
    vector<string> options = {
        "96 kbps", "128 kbps", "192 kbps", "256 kbps", "320 kbps"
    };
    vector<string> values = {"96", "128", "192", "256", "320"};
    int cur = 0;
    for (int i = 0; i < (int)values.size(); i++) {
        if (values[i] == AUDIO_BITRATE) { cur = i; break; }
    }
    string desc = tr(
        "Audio encoding bitrate in kilobits per second.\n"
        "Higher values = better quality, larger files.\n"
        "128kbps = speech, 192kbps = balanced, 320kbps = high quality.",
        "Битрейт аудиокодирования в кбит/с.\n"
        "Больше = лучше качество, больше файлы.\n"
        "128kbps = речь, 192kbps = баланс, 320kbps = высокое качество.");
    int sel = arrowSelect(tr("AUDIO BITRATE", "БИТРЕЙТ АУДИО"), desc, options, cur);
    if (sel >= 0) {
        AUDIO_BITRATE = values[sel];
        saveConfig();
        printColor(tr("[OK] Audio bitrate set to ", "[OK] Битрейт аудио: ") + AUDIO_BITRATE + " kbps", GREEN);
        waitForKey();
    }
}

// ========== UPDATE COMPONENTS ==========
void updateComponentsMenu() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" COMPONENT UPDATER", " ОБНОВЛЕНИЕ КОМПОНЕНТОВ"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n1. " << tr("Re-download FFmpeg + FFprobe", "Переустановить FFmpeg + FFprobe")
         << "\n0. " << tr("Return (ESC)", "Назад (ESC)") << "\n\n" << tr("Your choice: ", "Ваш выбор: ");
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') {
        return;
    }
    if (ch == '1') {
        clearScreen();
        printColor("========================================", CYAN);
        printColor(tr(" Downloading latest FFmpeg (~160MB)...", " Загрузка последней версии FFmpeg (~160МБ)..."), CYAN);
        printColor("========================================", CYAN);
        string zipFile = CONFIG_PATH + "ffmpeg.zip";
        if (downloadFile("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip", zipFile, "FFmpeg")) {
            printComponentProgress("FFmpeg", 100.0, tr("Extracting components...", "Извлечение компонентов..."));
            if (extractZip(zipFile, CONFIG_PATH)) {
                organizeExtractedTool("ffmpeg.exe", CONFIG_PATH);
                FFMPEG_FOUND = fileExists(CONFIG_PATH + "ffmpeg.exe");
                FFPROBE_FOUND = fileExists(CONFIG_PATH + "ffprobe.exe");
                if (FFMPEG_FOUND) {
                    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
                    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";
                    cout << "\n";
                    printColor(tr("[OK] FFmpeg updated successfully!", "[OK] FFmpeg успешно обновлён!"), GREEN);
                }
                else {
                    cout << "\n";
                    printColor(tr("[ERROR] Failed to locate ffmpeg.exe after extraction!", "[ОШИБКА] ffmpeg.exe не найден после извлечения!"), RED);
                }
            }
            else {
                cout << "\n";
                printColor(tr("[ERROR] Failed to extract FFmpeg archive!", "[ОШИБКА] Не удалось извлечь архив FFmpeg!"), RED);
            }
            std::error_code ec;
            fs::remove(fs::u8path(zipFile), ec);
        }
        else {
            printColor(tr("[ERROR] Failed to download FFmpeg!", "[ОШИБКА] Не удалось скачать FFmpeg!"), RED);
        }
        waitForKey();
    }
}

// ========== SETTINGS ==========
void settingsMenu() {
    while (true) {
        clearScreen();
        printColor("========================================", CYAN);
        printColor(tr(" SETTINGS", " НАСТРОЙКИ"), CYAN);
        printColor("========================================", CYAN);
        cout << "\n1. " << tr("Output location: [", "Папка вывода: [") << OUTPUT_PATH << "]"
             << "\n2. " << tr("Output format: [", "Формат вывода: [") << OUTPUT_FORMAT << "]"
             << "\n3. " << tr("Resolution: [", "Разрешение: [") << OUTPUT_RESOLUTION << "]"
             << "\n4. " << tr("FPS: [", "FPS: [") << OUTPUT_FPS << "]"
             << "\n5. " << tr("Encoding preset: [", "Пресет кодирования: [") << PRESET << "]"
             << "\n6. " << tr("CRF value: [", "Значение CRF: [") << CRF_VALUE << "]"
             << "\n7. " << tr("Audio bitrate: [", "Битрейт аудио: [") << AUDIO_BITRATE << " kbps]"
             << "\n8. " << tr("Program acceleration: [", "Программное ускорение: [") << getAccelerationModeName() << "]"
             << "\n9. " << tr("Add suffixes to files: [", "Добавлять суффиксы в конец файлов: [") << (!OVERWRITE_FILES ? "ON" : "OFF") << "]"
             << "\nm. " << tr("Keep metadata: [", "Сохранять метаданные: [") << (KEEP_METADATA ? "ON" : "OFF") << "]"
             << "\nv. " << tr("Video codec prompt: [", "Запрос видеокодека: [") << (VIDEO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")) << "]"
             << "\na. " << tr("Audio codec prompt: [", "Запрос аудиокодека: [") << (AUDIO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")) << "]"
             << "\nu. " << tr("Update FFmpeg & components", "Обновить FFmpeg")
             << "\n0. " << tr("Return (ESC)", "Назад (ESC)") << "\n\n" << tr("Your choice: ", "Ваш выбор: ");
        char ch = getMenuChoice();
        if (ch == 27 || ch == '0') {
            return;
        }
        cout << ch << endl;
        switch (ch) {
        case '1': {
            string p = openFolderDialog(utf8ToWstring(tr("Select output folder", "Выберите папку вывода")).c_str());
            if (!p.empty()) {
                OUTPUT_PATH = p;
                if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
                saveConfig();
                printColor(tr("[OK] Output location updated!", "[OK] Папка вывода обновлена!"), GREEN);
            }
            else {
                printColor(tr("[INFO] Not changed", "[ИНФО] Не изменено"), YELLOW);
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
        case '8': {
            vector<string> opts;
            vector<AccelMode> modes;

            // 1. Без ускорения (программный libx264)
            opts.push_back(tr("Software CPU (libx264)", "Программный CPU (libx264)"));
            modes.push_back(ACCEL_CPU_ONLY);

            int hwCount = (HAS_NVIDIA_DEVICE ? 1 : 0) + (HAS_INTEL_DEVICE ? 1 : 0) + (HAS_AMD_DEVICE ? 1 : 0);

            // 2. Программный + Аппаратный (только если найден хотя бы один GPU)
            if (hwCount > 0) {
                opts.push_back(tr("Software + Hardware (CPU Decoding, GPU Encoding)",
                                  "Программный + Аппаратный (Декодирование CPU, Кодирование GPU)"));
                modes.push_back(ACCEL_CPU_DEC_GPU_ENC);
            }

            // 3. Аппаратные варианты (только обнаруженные)
            if (HAS_NVIDIA_DEVICE) {
                opts.push_back(tr("Hardware NVIDIA (NVENC)", "Аппаратный NVIDIA (NVENC)"));
                modes.push_back(ACCEL_NVIDIA);
            }
            if (HAS_INTEL_DEVICE) {
                opts.push_back(tr("Hardware INTEL (QSV)", "Аппаратный INTEL (QSV)"));
                modes.push_back(ACCEL_INTEL);
            }
            if (HAS_AMD_DEVICE) {
                opts.push_back(tr("Hardware AMD (AMF)", "Аппаратный AMD (AMF)"));
                modes.push_back(ACCEL_AMD);
            }

            int curIdx = 0;
            for (size_t j = 0; j < modes.size(); j++) {
                if (modes[j] == ACCELERATION_MODE) {
                    curIdx = (int)j;
                    break;
                }
            }

            string desc = tr(
                "Select acceleration and encoding method.\n"
                "Software: CPU-only processing.\n"
                "Software + Hardware: CPU decoding, GPU encoding.\n"
                "Hardware: full GPU acceleration where supported.",
                "Выберите метод ускорения и кодирования.\n"
                "Программный: обработка только на CPU.\n"
                "Программный + Аппаратный: декодирование CPU, кодирование GPU.\n"
                "Аппаратный: полное ускорение на поддерживаемом GPU.");

            int sel = arrowSelect(tr("PROGRAM ACCELERATION", "ПРОГРАММНОЕ УСКОРЕНИЕ"), desc, opts, curIdx);
            if (sel >= 0) {
                AccelMode chosen = modes[sel];
                if (chosen == ACCEL_CPU_DEC_GPU_ENC) {
                    // Check if more than one GPU device is present
                    if (hwCount > 1) {
                        vector<string> gpuOpts;
                        vector<AccelMode> gpuModes;
                        if (HAS_NVIDIA_DEVICE) {
                            gpuOpts.push_back("NVIDIA (NVENC)");
                            gpuModes.push_back(ACCEL_NVIDIA);
                        }
                        if (HAS_INTEL_DEVICE) {
                            gpuOpts.push_back("INTEL (QSV)");
                            gpuModes.push_back(ACCEL_INTEL);
                        }
                        if (HAS_AMD_DEVICE) {
                            gpuOpts.push_back("AMD (AMF)");
                            gpuModes.push_back(ACCEL_AMD);
                        }

                        int curGpuIdx = 0;
                        for (size_t k = 0; k < gpuModes.size(); k++) {
                            if (gpuModes[k] == HYBRID_GPU_CHOICE) {
                                curGpuIdx = (int)k;
                                break;
                            }
                        }

                        int selGpu = arrowSelect(
                            tr("WHAT TO CHOOSE AS GPU?", "ЧТО ВЫБРАТЬ КАК GPU?"),
                            tr("Select which GPU to use for encoding:", "Выберите, какой GPU использовать для кодирования:"),
                            gpuOpts,
                            curGpuIdx
                        );
                        if (selGpu >= 0) {
                            HYBRID_GPU_CHOICE = gpuModes[selGpu];
                        }
                    } else {
                        if (HAS_NVIDIA_DEVICE) HYBRID_GPU_CHOICE = ACCEL_NVIDIA;
                        else if (HAS_INTEL_DEVICE) HYBRID_GPU_CHOICE = ACCEL_INTEL;
                        else if (HAS_AMD_DEVICE) HYBRID_GPU_CHOICE = ACCEL_AMD;
                    }
                }
                ACCELERATION_MODE = chosen;
                saveConfig();
                printColor(tr("[OK] Acceleration mode: ", "[OK] Режим ускорения: ") + getAccelerationModeName(), GREEN);
                waitForKey();
            }
            break;
        }
        case '9': {
            vector<string> opts = {"OFF", "ON"};
            string desc = tr(
                "When OFF, existing output files are overwritten without asking.\n"
                "When ON, a suffix (_1, _compressed, etc.) is added to protect the original.",
                "Когда ВЫКЛ, существующие файлы перезаписываются без запроса, удаляя оригинал!\n"
                "Когда ВКЛ, добавляется суффикс (_1, _compressed, и т.д.) для защиты от перезаписи оригинала!");
            int sel = arrowSelect(tr("ADD SUFFIXES TO FILES", "ДОБАВЛЯТЬ СУФФИКСЫ В КОНЕЦ ФАЙЛОВ"), desc, opts, (!OVERWRITE_FILES) ? 1 : 0);
            if (sel >= 0) {
                OVERWRITE_FILES = (sel == 0); // 0 -> OFF (overwrite), 1 -> ON (do not overwrite, add suffix)
                saveConfig();
                printColor(string(tr("[OK] Add suffixes to files ", "[OK] Добавлять суффиксы в конец файлов ")) + (!OVERWRITE_FILES ? "ON" : "OFF"), GREEN);
                waitForKey();
            }
            break;
        }
        case 'm': {
            vector<string> opts = {"ON", "OFF"};
            string desc = tr(
                "When ON, metadata (title, artist, date, etc.) from source is copied to output.\n"
                "When OFF, all metadata is stripped from the output.",
                "Когда ВКЛ, метаданные (название, артист, дата) копируются в выходной файл.\n"
                "Когда ВЫКЛ, все метаданные удаляются.");
            int sel = arrowSelect(tr("KEEP METADATA", "СОХРАНЕНИЕ МЕТАДАННЫХ"), desc, opts, KEEP_METADATA ? 0 : 1);
            if (sel >= 0) {
                KEEP_METADATA = (sel == 0);
                saveConfig();
                printColor(string(tr("[OK] Keep metadata ", "[OK] Сохранение метаданных ")) + (KEEP_METADATA ? "ON" : "OFF"), GREEN);
                waitForKey();
            }
            break;
        }
        case 'v': {
            vector<string> opts = {tr("Always Ask", "Всегда спрашивать"), tr("Always use configured settings", "Всегда как задано моими настройками")};
            string desc = tr(
                "When 'Always Ask' is active, you will be prompted to review\n"
                "video settings (format, CRF, preset, resolution) before each operation.\n"
                "When 'Always use configured settings' is selected, your saved preferences are used automatically.",
                "Когда 'Всегда спрашивать' активно, перед каждой операцией\n"
                "вам будет предложено проверить настройки видео (формат, CRF, пресет, разрешение).\n"
                "Когда выбрано 'Всегда как задано моими настройками', сразу применяются сохранённые параметры.");
            int sel = arrowSelect(tr("VIDEO CODEC PROMPT", "ЗАПРОС ВИДЕОКОДЕКА"), desc, opts, VIDEO_CODEC_ASK ? 0 : 1);
            if (sel >= 0) {
                VIDEO_CODEC_ASK = (sel == 0);
                saveConfig();
                printColor(tr("[OK] Video codec prompt: ", "[OK] Запрос видеокодека: ") + (VIDEO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")), GREEN);
                waitForKey();
            }
            break;
        }
        case 'a': {
            vector<string> opts = {tr("Always Ask", "Всегда спрашивать"), tr("Always use configured settings", "Всегда как задано моими настройками")};
            string desc = tr(
                "When 'Always Ask' is active, you will be prompted to review\n"
                "audio settings (format, bitrate) before each audio operation.\n"
                "When 'Always use configured settings' is selected, your saved preferences are used automatically.",
                "Когда 'Всегда спрашивать' активно, перед каждой аудиооперацией\n"
                "вам будет предложено проверить настройки аудио (формат, битрейт).\n"
                "Когда выбрано 'Всегда как задано моими настройками', сразу применяются сохранённые параметры.");
            int sel = arrowSelect(tr("AUDIO CODEC PROMPT", "ЗАПРОС АУДИОКОДЕКА"), desc, opts, AUDIO_CODEC_ASK ? 0 : 1);
            if (sel >= 0) {
                AUDIO_CODEC_ASK = (sel == 0);
                saveConfig();
                printColor(tr("[OK] Audio codec prompt: ", "[OK] Запрос аудиокодека: ") + (AUDIO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")), GREEN);
                waitForKey();
            }
            break;
        }
        case 'u': updateComponentsMenu(); break;
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey();
        }
    }
}

// ========== PEER FFMPEG SHARING ==========
bool checkAndCopyFromPeerFFmpeg() {
    wchar_t docPath[MAX_PATH];
    string peerConfigs = "";
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docPath))) {
        wstring p = wstring(docPath) + L"\\MR-CLI-FOR-YT-DLP\\configs\\";
        peerConfigs = wstringToUtf8(p);
    }
    if (peerConfigs.empty() || !fileExists(peerConfigs + "ffmpeg.exe")) {
        string fallback = "C:\\MR-CLI-FOR-YT-DLP\\configs\\";
        if (fileExists(fallback + "ffmpeg.exe")) {
            peerConfigs = fallback;
        }
    }
    if (peerConfigs.empty() || !fileExists(peerConfigs + "ffmpeg.exe")) {
        return false;
    }

    string otherAppName = "MR CLI FOR YT-DLP";
    string title = tr("FOUND FFMPEG", "ОБНАРУЖЕН FFMPEG");
    string desc = tr(
        "The program just detected that you also use " + otherAppName + ".\n"
        "It already has FFmpeg installed.\n"
        "Would you like to use a shared copy to avoid downloading again?",
        "Программа только что обнаружила, что вы также используете " + otherAppName + ".\n"
        "У неё уже установлен FFmpeg.\n"
        "Хотите использовать общую копию, чтобы не скачивать повторно?"
    );
    vector<string> opts = {
        tr("Yes, use copy (fast and offline)", "Да, использовать копию (быстро и без интернета)"),
        tr("No, download anew (download speed depends on your network)", "Нет, скачать заново (скорость загрузки зависит от вашей сети)")
    };

    int sel = arrowSelect(title, desc, opts, 0);
    if (sel != 0) {
        return false;
    }

    if (!dirExists(CONFIG_PATH)) {
        createDirRecursive(CONFIG_PATH);
    }

    clearScreen();
    printColor("========================================", CYAN);
    printColor(" " + tr("COPYING FFMPEG FROM ", "КОПИРОВАНИЕ FFMPEG ИЗ ") + otherAppName, CYAN);
    printColor("========================================", CYAN);
    cout << "\n";
    printColor(tr("[INFO] Copying FFmpeg components...", "[ИНФО] Копирование компонентов FFmpeg..."), CYAN);

    vector<string> filesToCopy = {"ffmpeg.exe", "ffprobe.exe", "ffplay.exe"};
    bool copiedAny = false;
    for (const auto& f : filesToCopy) {
        string src = peerConfigs + f;
        string dst = CONFIG_PATH + f;
        if (fileExists(src)) {
            wstring wSrc = utf8ToWstring(src);
            wstring wDst = utf8ToWstring(dst);
            if (CopyFileW(wSrc.c_str(), wDst.c_str(), FALSE)) {
                copiedAny = true;
                printColor(tr("[OK] Copied: ", "[OK] Скопирован: ") + f, GREEN);
            }
            else {
                printColor(tr("[ERROR] Failed to copy: ", "[ОШИБКА] Не удалось скопировать: ") + f, RED);
            }
        }
    }

    if (copiedAny && fileExists(CONFIG_PATH + "ffmpeg.exe")) {
        FFMPEG_FOUND = true;
        FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
        FFPROBE_FOUND = fileExists(CONFIG_PATH + "ffprobe.exe");
        if (FFPROBE_FOUND) {
            FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";
        }
        cout << "\n";
        printColor(tr("[OK] FFmpeg successfully copied!", "[OK] FFmpeg успешно скопирован!"), GREEN);
        Sleep(1500);
        return true;
    }

    return false;
}

// ========== DEPENDENCY CHECKS & AUTO INSTALLER ==========
bool checkDependencies() {
    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";

    FFMPEG_FOUND = fileExists(FFMPEG_PATH);
    FFPROBE_FOUND = fileExists(FFPROBE_PATH);

    if (!FFMPEG_FOUND) {
        if (checkAndCopyFromPeerFFmpeg()) {
            FFMPEG_FOUND = fileExists(FFMPEG_PATH);
            FFPROBE_FOUND = fileExists(FFPROBE_PATH);
        }
    }

    if (!FFMPEG_FOUND) {
        printColor("========================================", RED);
        printColor(tr("[ERROR] FFmpeg not found!", "[ОШИБКА] FFmpeg не найден!"), RED);
        printColor("========================================", RED);

        printColor("\n========================================", CYAN);
        printColor(tr(" AUTO INSTALLER", " АВТОУСТАНОВЩИК"), CYAN);
        printColor("========================================", CYAN);

        while (true) {
            printColor("\n" + tr("Install FFmpeg automatically? (y/n): \n", "Установить FFmpeg автоматически? (y/n): \n"), CYAN, false);

            char ch = getMenuChoice();

            if (ch == 'y' || ch == 'Y') {
                cout << "y" << endl;
                break;
            }
            else if (ch == 'n' || ch == 'N' || ch == 27) {
                cout << "n" << endl;
                printColor("\n========================================", YELLOW);
                printColor(tr(" [ERROR] FFmpeg is required to run the application!", " [ОШИБКА] FFmpeg необходим для работы программы!"), RED);
                printColor(tr(" [INFO] Download from: https://github.com/BtbN/FFmpeg-Builds/releases", " [ИНФО] Скачайте с: https://github.com/BtbN/FFmpeg-Builds/releases"), YELLOW);
                printColor(tr(" [INFO] Place 'ffmpeg.exe' and 'ffprobe.exe' in: ", " [ИНФО] Поместите 'ffmpeg.exe' и 'ffprobe.exe' в: ") + CONFIG_PATH, YELLOW);
                printColor("========================================", YELLOW);
                waitForKey();
                return false;
            }
        }

        // ========== INSTALLATION ==========
        if (!dirExists(CONFIG_PATH)) {
            createDirRecursive(CONFIG_PATH);
        }

        printColor("\n" + tr("[INFO] Installing FFmpeg (~160MB)...", "[ИНФО] Установка FFmpeg (~160MB)..."), CYAN);
        string zipFile = CONFIG_PATH + "ffmpeg.zip";
        if (downloadFile("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip", zipFile, "FFmpeg")) {
            printComponentProgress("FFmpeg", 100.0, tr("Extracting components...", "Распаковка компонентов..."));
            if (extractZip(zipFile, CONFIG_PATH)) {
                organizeExtractedTool("ffmpeg.exe", CONFIG_PATH);
                FFMPEG_FOUND = fileExists(CONFIG_PATH + "ffmpeg.exe");
                FFPROBE_FOUND = fileExists(CONFIG_PATH + "ffprobe.exe");
                if (FFMPEG_FOUND) {
                    FFMPEG_PATH = CONFIG_PATH + "ffmpeg.exe";
                    FFPROBE_PATH = CONFIG_PATH + "ffprobe.exe";
                    cout << "\n";
                    printColor(tr("[OK] FFmpeg and FFprobe installed successfully!", "[OK] FFmpeg и FFprobe успешно установлены!"), GREEN);
                }
                else {
                    cout << "\n";
                    printColor(tr("[ERROR] Failed to locate ffmpeg.exe after extraction!", "[ОШИБКА] Не удалось найти ffmpeg.exe после распаковки!"), RED);
                }
            }
            else {
                cout << "\n";
                printColor(tr("[ERROR] Failed to extract FFmpeg archive!", "[ОШИБКА] Ошибка распаковки архива FFmpeg!"), RED);
            }

            std::error_code ec;
            fs::remove(fs::u8path(zipFile), ec);
        }
        else {
            printColor(tr("[ERROR] Failed to download FFmpeg!", "[ОШИБКА] Ошибка загрузки FFmpeg!"), RED);
        }

        // Final verification
        FFMPEG_FOUND = fileExists(FFMPEG_PATH);
        FFPROBE_FOUND = fileExists(FFPROBE_PATH);

        if (FFMPEG_FOUND) {
            printColor("\n" + tr("[OK] Dependencies check completed!", "[OK] Проверка зависимостей завершена!"), GREEN);
            Sleep(1000);
            return true;
        }
        else {
            printColor("\n" + tr("[ERROR] FFmpeg is missing and could not be installed!", "[ОШИБКА] FFmpeg отсутствует и не может быть установлен!"), RED);
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
    printColor(" MR CLI FOR FFMPEG v1.1.2", CYAN);
    printColor("========================================", CYAN);
    printColor("========================================", GREEN);
    printColor(" FFMPEG:  " + string(FFMPEG_FOUND ? tr("[OK] installed", "[OK] установлен") : tr("[ERROR] not found", "[ОШИБКА] не найден")), FFMPEG_FOUND ? GREEN : RED);
    printColor(" FFPROBE: " + string(FFPROBE_FOUND ? tr("[OK] installed", "[OK] установлен") : tr("[WARNING] not installed", "[ВНИМАНИЕ] не установлен")), FFPROBE_FOUND ? GREEN : YELLOW);
    string hwTag = "";
    string hwDevice = "";
    switch (ACCELERATION_MODE) {
        case ACCEL_CPU_ONLY:
            hwTag = "[CPU(libx264)]";
            hwDevice = !DETECTED_CPU_NAME.empty() ? DETECTED_CPU_NAME : "CPU";
            break;
        case ACCEL_NVIDIA:
            hwTag = "[GPU(NVENC)]";
            hwDevice = !DETECTED_NVIDIA_NAME.empty() ? DETECTED_NVIDIA_NAME : DETECTED_GPU_NAME;
            break;
        case ACCEL_INTEL:
            hwTag = "[GPU(QSV)]";
            hwDevice = !DETECTED_INTEL_NAME.empty() ? DETECTED_INTEL_NAME : DETECTED_GPU_NAME;
            break;
        case ACCEL_AMD:
            hwTag = "[GPU(AMF)]";
            hwDevice = !DETECTED_AMD_NAME.empty() ? DETECTED_AMD_NAME : DETECTED_GPU_NAME;
            break;
        case ACCEL_CPU_DEC_GPU_ENC: {
            string sub = (HYBRID_GPU_CHOICE == ACCEL_NVIDIA) ? "NVENC" :
                         (HYBRID_GPU_CHOICE == ACCEL_INTEL)  ? "QSV" :
                         (HYBRID_GPU_CHOICE == ACCEL_AMD)    ? "AMF" : "GPU";
            hwTag = "[CPU+GPU(" + sub + ")]";
            string gName = (HYBRID_GPU_CHOICE == ACCEL_NVIDIA) ? DETECTED_NVIDIA_NAME :
                           (HYBRID_GPU_CHOICE == ACCEL_INTEL)  ? DETECTED_INTEL_NAME :
                           (HYBRID_GPU_CHOICE == ACCEL_AMD)    ? DETECTED_AMD_NAME : DETECTED_GPU_NAME;
            if (gName.empty()) gName = DETECTED_GPU_NAME;
            hwDevice = gName;
            break;
        }
        default:
            hwTag = "[CPU(libx264)]";
            hwDevice = !DETECTED_CPU_NAME.empty() ? DETECTED_CPU_NAME : "CPU";
            break;
    }
    string hwLine = " " + tr("Hardware: ", "Железо: ") + hwTag;
    if (!hwDevice.empty()) hwLine += " (" + hwDevice + ")";
    printColor(hwLine, (ACCELERATION_MODE == ACCEL_CPU_ONLY) ? WHITE : GREEN);
    printColor("========================================", GREEN);
    cout << "========================================\n"
         << "--- " << tr("VIDEO OPERATIONS", "ВИДЕО ОПЕРАЦИИ") << " ---\n"
         << " 1. " << tr("Convert video", "Конвертировать видео") << "\n"
         << " 2. " << tr("Trim / Cut video", "Обрезать видео") << "\n"
         << " 3. " << tr("Change resolution", "Изменить разрешение") << "\n"
         << " 4. " << tr("Change speed", "Изменить скорость") << "\n"
         << " 5. " << tr("Rotate / Flip", "Повернуть / Отразить") << "\n"
         << " 6. " << tr("Compress video", "Сжать видео") << "\n"
         << " 7. " << tr("Add watermark", "Добавить водяной знак") << "\n"
         << " 8. " << tr("Add subtitles (burn-in)", "Добавить субтитры (вшить)") << "\n"
         << "--- " << tr("AUDIO OPERATIONS", "АУДИО ОПЕРАЦИИ") << " ---\n"
         << " 9. " << tr("Extract audio", "Извлечь аудио") << "\n"
         << " a. " << tr("Merge video + audio", "Объединить видео + аудио") << "\n"
         << " b. " << tr("Strip audio (mute)", "Удалить звук") << "\n"
         << "--- " << tr("OTHER", "ДРУГОЕ") << " ---\n"
         << " c. " << tr("Concatenate (join) files", "Склеить файлы") << "\n"
         << " d. " << tr("Create GIF", "Создать GIF") << "\n"
         << " e. " << tr("Extract frames", "Извлечь кадры") << "\n"
         << " f. " << tr("File information", "Информация о файле") << "\n"
         << " g. " << tr("Compare two files", "Сравнить два файла") << "\n"
         << " h. " << tr("Batch compress video", "Пакетное сжатие видео") << "\n"
         << " i. " << tr("Batch compress audio", "Пакетное сжатие аудио") << "\n"
         << "--- " << tr("PROGRAM", "ПРОГРАММА") << " ---\n"
         << " s. " << tr("Settings", "Настройки") << "\n"
         << " l. " << (CURRENT_LANG == LANG_EN ? "Language: English" : "Язык: Русский") << "\n"
         << " 0. " << tr("Exit (ESC)", "Выход (ESC)") << "\n"
         << "========================================\n"
         << "\n" << tr("Your choice: ", "Ваш выбор: ");
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

    initDefaultLanguage();
    loadConfig();

    if (!checkDependencies()) {
        CoUninitialize();
        return 1;
    }

    detectGPU();

    while (true) {
        displayMenu();
        char ch = getMenuChoice();
        if (ch == 27 || ch == '0') {
            cout << tr("Exiting...", "Выход...") << "\n";
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
        case 'g': compareFiles(); break;
        case 'h': batchCompressVideo(); break;
        case 'i': batchCompressAudio(); break;
        case 's': settingsMenu(); break;
        case 'l':
            CURRENT_LANG = (CURRENT_LANG == LANG_EN) ? LANG_RU : LANG_EN;
            saveConfig();
            break;
        default:
            printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED);
            waitForKey();
        }
    }

    CoUninitialize();
    return 0;
}
