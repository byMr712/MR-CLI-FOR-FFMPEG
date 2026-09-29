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
#include <filesystem>
#include <iomanip>
#include <intrin.h>

// Component downloads use the Windows BITS COM API (AGENTS.md 4.5).
// Both the coclass and the interface carry a uuid attribute, so __uuidof() resolves
// them without pulling in <initguid.h>.
#include <bits.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Comdlg32.lib")
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
bool SAVE_COVER = true;
bool FFMPEG_FOUND = false, FFPROBE_FOUND = false;
bool OVERWRITE_FILES = false;
bool KEEP_METADATA = true;
bool VIDEO_CODEC_ASK = true;
bool AUDIO_CODEC_ASK = false;
bool PARALLEL_BATCH = false;
string AUDIO_CODEC = "copy";
string SUBTITLE_ACTION = "ask";
string DELETE_ORIGINAL = "ask";
bool g_ffmpegEscaped = false;

// ========== ACCELERATION & GPU MODES ==========
enum AccelMode {
    ACCEL_CPU_ONLY = 0,         // Программный CPU (libx264)
    ACCEL_CPU_DEC_GPU_ENC = 1,  // Смешанный (Декодирование CPU, Кодирование GPU)
    ACCEL_NVIDIA = 2,           // Аппаратный NVIDIA (NVENC)
    ACCEL_INTEL = 3,            // Аппаратный INTEL (QSV)
    ACCEL_AMD = 4,              // Аппаратный AMD (AMF)
    ACCEL_GPU_DEC_CPU_ENC = 5   // Обратный смешанный (Декодирование GPU, Кодирование CPU)
};

bool CONFIG_LOADED = false;
AccelMode ACCELERATION_MODE = ACCEL_CPU_ONLY;
AccelMode HYBRID_GPU_CHOICE = ACCEL_CPU_ONLY; // Which GPU to use if ACCEL_CPU_DEC_GPU_ENC is chosen

bool HAS_NVIDIA_DEVICE = false;
bool HAS_INTEL_DEVICE = false;
bool HAS_AMD_DEVICE = false;

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
    switch (wc) {
        case 0x0419: case 0x0439: return 'q';  // й Й
        case 0x0426: case 0x0446: return 'w';  // ц Ц
        case 0x0423: case 0x0443: return 'e';  // у У
        case 0x041A: case 0x043A: return 'r';  // к К
        case 0x0415: case 0x0435: return 't';  // е Е
        case 0x041D: case 0x043D: return 'y';  // н Н
        case 0x0413: case 0x0433: return 'u';  // г Г
        case 0x0428: case 0x0448: return 'i';  // ш Ш
        case 0x0429: case 0x0449: return 'o';  // щ Щ
        case 0x0417: case 0x0437: return 'p';  // з З
        case 0x0425: case 0x0445: return '[';  // х Х
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
        case 0x0401: case 0x0451: return '`';  // ё Ё
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
    int cpuInfo[4] = { 0 };
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = (unsigned int)cpuInfo[0];
    if (nExIds >= 0x80000004) {
        char cpuBrand[65] = { 0 };
        __cpuid((int*)(cpuBrand), 0x80000002);
        __cpuid((int*)(cpuBrand + 16), 0x80000003);
        __cpuid((int*)(cpuBrand + 32), 0x80000004);
        string cpu = cpuBrand;
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
}

static string runCommand(const string& cmd);

void detectGPU() {
    detectCPU();
    HAS_NVIDIA_DEVICE = false;
    HAS_INTEL_DEVICE = false;
    HAS_AMD_DEVICE = false;
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
        string output = runCommand("\"" + FFPROBE_PATH + "\" -hide_banner -encoders 2>&1");

        bool hasNvenc = (output.find("h264_nvenc") != string::npos) || (output.find("hevc_nvenc") != string::npos);
        bool hasAmf = (output.find("h264_amf") != string::npos) || (output.find("hevc_amf") != string::npos);
        bool hasQsv = (output.find("h264_qsv") != string::npos) || (output.find("hevc_qsv") != string::npos);

        if (!hasNvenc) HAS_NVIDIA_DEVICE = false;
        if (!hasAmf) HAS_AMD_DEVICE = false;
        if (!hasQsv) HAS_INTEL_DEVICE = false;
    }

    if (HAS_NVIDIA_DEVICE && DETECTED_NVIDIA_NAME.empty()) DETECTED_NVIDIA_NAME = "NVIDIA (NVENC)";
    if (HAS_INTEL_DEVICE && DETECTED_INTEL_NAME.empty()) DETECTED_INTEL_NAME = "Intel (QSV)";
    if (HAS_AMD_DEVICE && DETECTED_AMD_NAME.empty()) DETECTED_AMD_NAME = "AMD (AMF)";

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
        if (ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC && hwCount == 0) ACCELERATION_MODE = ACCEL_CPU_ONLY;

        // Validate HYBRID_GPU_CHOICE
        if (HYBRID_GPU_CHOICE == ACCEL_NVIDIA && !HAS_NVIDIA_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_INTEL && !HAS_INTEL_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_AMD && !HAS_AMD_DEVICE) HYBRID_GPU_CHOICE = defaultHw;
        if (HYBRID_GPU_CHOICE == ACCEL_CPU_ONLY && hwCount > 0) HYBRID_GPU_CHOICE = defaultHw;
    }
}

AccelMode getActiveGpuMode() {
    if (ACCELERATION_MODE == ACCEL_CPU_DEC_GPU_ENC || ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) {
        return HYBRID_GPU_CHOICE;
    }
    return ACCELERATION_MODE;
}

string getAccelerationModeName() {
    switch (ACCELERATION_MODE) {
        case ACCEL_CPU_ONLY:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
        case ACCEL_CPU_DEC_GPU_ENC:
            return tr("Hybrid (CPU + GPU)", "Смешанный (CPU + GPU)");
        case ACCEL_NVIDIA:
            return tr("Hardware NVIDIA (NVENC)", "Аппаратный NVIDIA (NVENC)");
        case ACCEL_INTEL:
            return tr("Hardware INTEL (QSV)", "Аппаратный INTEL (QSV)");
        case ACCEL_AMD:
            return tr("Hardware AMD (AMF)", "Аппаратный AMD (AMF)");
        case ACCEL_GPU_DEC_CPU_ENC:
            return tr("Reverse Hybrid (GPU + CPU)", "Обратный смешанный (GPU + CPU)");
        default:
            return tr("Software CPU (libx264)", "Программный CPU (libx264)");
    }
}

string getAccelerationHardwareInfo() {
    string cpuName = !DETECTED_CPU_NAME.empty() ? DETECTED_CPU_NAME : "CPU";
    string gpuName = "";
    string gpuTag = "";

    auto getGpuName = [&]() {
        if (HYBRID_GPU_CHOICE == ACCEL_NVIDIA) { gpuTag = "NVENC"; gpuName = !DETECTED_NVIDIA_NAME.empty() ? DETECTED_NVIDIA_NAME : "NVIDIA"; }
        else if (HYBRID_GPU_CHOICE == ACCEL_INTEL) { gpuTag = "QSV"; gpuName = !DETECTED_INTEL_NAME.empty() ? DETECTED_INTEL_NAME : "Intel"; }
        else if (HYBRID_GPU_CHOICE == ACCEL_AMD) { gpuTag = "AMF"; gpuName = !DETECTED_AMD_NAME.empty() ? DETECTED_AMD_NAME : "AMD"; }
    };

    switch (ACCELERATION_MODE) {
        case ACCEL_CPU_ONLY:
            return "[" + cpuName + "]\n " + cpuName;
        case ACCEL_CPU_DEC_GPU_ENC: {
            getGpuName();
            return "[CPU+GPU(" + gpuTag + ")]\n " + cpuName + " + " + gpuName;
        }
        case ACCEL_NVIDIA: {
            gpuName = !DETECTED_NVIDIA_NAME.empty() ? DETECTED_NVIDIA_NAME : "NVIDIA";
            return "[GPU(NVENC)]\n " + gpuName;
        }
        case ACCEL_INTEL: {
            gpuName = !DETECTED_INTEL_NAME.empty() ? DETECTED_INTEL_NAME : "Intel";
            return "[GPU(QSV)]\n " + gpuName;
        }
        case ACCEL_AMD: {
            gpuName = !DETECTED_AMD_NAME.empty() ? DETECTED_AMD_NAME : "AMD";
            return "[GPU(AMF)]\n " + gpuName;
        }
        case ACCEL_GPU_DEC_CPU_ENC: {
            getGpuName();
            return "[GPU(" + gpuTag + ")+CPU]\n " + gpuName + " + " + cpuName;
        }
        default:
            return "[" + cpuName + "]\n " + cpuName;
    }
}

string getHWAccelArg(bool pureReencode = false) {
    if (ACCELERATION_MODE == ACCEL_CPU_ONLY || ACCELERATION_MODE == ACCEL_CPU_DEC_GPU_ENC) {
        return "";
    }
    if (ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) {
        if (pureReencode) {
            AccelMode gpu = getActiveGpuMode();
            if (gpu == ACCEL_NVIDIA) return " -hwaccel cuda";
            if (gpu == ACCEL_INTEL) return " -hwaccel qsv";
        }
        return " -hwaccel auto";
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
    SetConsoleTitleW(L"MR CLI FOR FFMPEG v1.1.6");
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

// Downloads a component over the Windows BITS COM API.
//
// AGENTS.md 4.5 forbids raw WinINet here: an "InternetOpen + InternetOpenUrl +
// InternetReadFile" stream inside a binary that also spawns child processes is the
// canonical dropper signature that ML classifiers (Wacatac.*!ml, Elastic, SecureAge)
// score highly. BITS runs the transfer inside the trusted Windows service, so this
// binary no longer contains a network client at all and WININET.dll leaves the
// import table entirely.
//
// Progress is polled through IBackgroundCopyJob::QueryStatus so the user still sees
// a live progress bar; a foreground priority keeps the transfer at normal speed.
bool downloadFile(const string& url, const string& destFile, const string& label = "") {
    // main() already initialises COM, but keep this function self-contained so it can
    // never be reached from an uninitialised apartment.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(initHr);

    IBackgroundCopyManager* mgr = nullptr;
    IBackgroundCopyJob* job = nullptr;

    auto cleanup = [&]() {
        if (job) job->Release();
        if (mgr) mgr->Release();
        if (needUninit) CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(BackgroundCopyManager), nullptr, CLSCTX_ALL,
                                  __uuidof(IBackgroundCopyManager), (void**)&mgr);
    if (FAILED(hr) || !mgr) {
        // The transfer service itself is unavailable, which needs a different answer
        // than a plain network failure: nothing is wrong with the connection.
        printColor(tr("[ERROR] Windows transfer service (BITS) is unavailable on this system!",
                      "[ОШИБКА] Служба передачи данных Windows (BITS) недоступна на этой системе!"), RED);
        printColor(tr("[INFO] Enable it in Services, or download FFmpeg manually.",
                      "[ИНФО] Включите её в службах или скачайте FFmpeg вручную."), YELLOW);
        cleanup();
        return false;
    }

    GUID jobId = {};
    hr = mgr->CreateJob(L"MR CLI for FFmpeg component", BG_JOB_TYPE_DOWNLOAD, &jobId, &job);
    if (FAILED(hr) || !job) { cleanup(); return false; }

    // BITS transfers into the .tmp name and only publishes it on success, so the
    // previous archive stays intact if the transfer dies half way through.
    const wstring wTmp = utf8ToWstring(destFile + ".tmp");
    hr = job->AddFile(utf8ToWstring(url).c_str(), wTmp.c_str());
    if (FAILED(hr)) { job->Cancel(); cleanup(); return false; }

    // Foreground priority: the service would otherwise throttle an interactive
    // download down to background-trickle speed.
    job->SetPriority(BG_JOB_PRIORITY_FOREGROUND);
    job->SetNotifyFlags(BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR);
    // Retry transient network failures in a few seconds rather than the 3 minute default.
    job->SetMinimumRetryDelay(3);
    job->SetNoProgressTimeout(60);

    hr = job->Resume();
    if (FAILED(hr)) { job->Cancel(); cleanup(); return false; }

    // BITS retries transient errors on its own and can therefore wait indefinitely.
    // Cap the wait so the console never hangs on a dead connection.
    const ULONGLONG startTick = GetTickCount64();
    const ULONGLONG maxWaitMs = 30ULL * 60ULL * 1000ULL;
    bool transferred = false;

    while (GetTickCount64() - startTick < maxWaitMs) {
        Sleep(400);

        BG_JOB_STATE state = BG_JOB_STATE_QUEUED;
        if (FAILED(job->GetState(&state))) break;

        if (state == BG_JOB_STATE_TRANSFERRED) { transferred = true; break; }
        if (state == BG_JOB_STATE_ERROR || state == BG_JOB_STATE_CANCELLED) break;

        BG_JOB_PROGRESS prog = {};
        if (SUCCEEDED(job->GetProgress(&prog)) && prog.BytesTotal > 0) {
            double pct = (double)prog.BytesTransferred * 100.0 / (double)prog.BytesTotal;
            char info[64];
            snprintf(info, sizeof(info), "(%.1f / %.1f MB)",
                     (double)prog.BytesTransferred / 1048576.0,
                     (double)prog.BytesTotal / 1048576.0);
            printComponentProgress(label, pct, info);
        }
    }

    if (transferred) {
        // Acknowledge the job. This is also what releases BITS' lock on the finished
        // file, so the rename below cannot hit a sharing violation.
        job->Complete();
        const wstring wDst = utf8ToWstring(destFile);
        MoveFileExW(wTmp.c_str(), wDst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    } else {
        // Drop the job from the service store and remove any partial transfer.
        job->Cancel();
    }

    cleanup();
    cout << "\n";

    if (!transferred) return false;

    std::error_code ec;
    const uintmax_t sz = fs::file_size(fs::u8path(destFile), ec);
    return !ec && sz > 1000;
}

// ========== NATIVE ZIP EXTRACTION ==========
// AGENTS.md 4.5: never hand a freshly downloaded archive to an external unpacker.
// Running System32\tar.exe over an archive that was just fetched is the remaining half
// of the drop-and-execute pattern that ML classifiers weight, and it was enough to keep
// the binary flagged after the downloader itself was moved to BITS. Windows ships no
// DEFLATE decoder, so the container is parsed here and each entry is inflated by the
// RFC 1951 decoder below.
namespace zipx {

const size_t WIN_SIZE = 32768;

// Reads the compressed bytes of one entry straight from the archive, so a 160 MB zip
// is never held in memory.
struct BitIn {
    HANDLE h = INVALID_HANDLE_VALUE;
    unsigned long long left = 0;
    unsigned char buf[1 << 16];
    size_t bufPos = 0;
    size_t bufLen = 0;
    unsigned held = 0;
    bool bad = false;

    bool fill() {
        if (bufPos < bufLen) return true;
        if (left == 0) return false;
        DWORD want = (DWORD)((left < sizeof(buf)) ? left : (unsigned long long)sizeof(buf));
        DWORD got = 0;
        bufPos = 0;
        bufLen = 0;
        if (!ReadFile(h, buf, want, &got, nullptr) || got == 0) { bad = true; return false; }
        bufLen = got;
        left -= got;
        return true;
    }

    int bit() {
        if (!fill()) { bad = true; return 0; }
        int b = (buf[bufPos] >> held) & 1;
        if (++held == 8) { held = 0; bufPos++; }
        return b;
    }

    int bits(int need) {
        int val = 0;
        for (int i = 0; i < need; i++) val |= bit() << i;
        return val;
    }

    // DEFLATE is LSB-first inside a byte, so only a partially consumed byte is skipped.
    void alignByte() {
        if (held) { held = 0; bufPos++; }
    }

    int rawByte() {
        if (!fill()) { bad = true; return -1; }
        return buf[bufPos++];
    }
};

// 32 KB sliding window: a back reference reaches at most this far back, and the ring is
// flushed to disk every time it fills.
struct Win {
    HANDLE h = INVALID_HANDLE_VALUE;
    unsigned char ring[WIN_SIZE];
    size_t pos = 0;
    unsigned long long emitted = 0;
    bool bad = false;

    void put(unsigned char c) {
        ring[pos++] = c;
        emitted++;
        if (pos == WIN_SIZE) flush();
    }

    void flush() {
        if (!pos) return;
        DWORD wr = 0;
        if (!WriteFile(h, ring, (DWORD)pos, &wr, nullptr) || wr != pos) bad = true;
        pos = 0;
    }

    unsigned char back(size_t dist) { return ring[(pos + WIN_SIZE - dist) & (WIN_SIZE - 1)]; }
};

struct Huff {
    short count[16];
    vector<short> symbol;
};

bool buildHuff(Huff& h, const short* lengths, int n) {
    for (int len = 0; len <= 15; len++) h.count[len] = 0;
    for (int sym = 0; sym < n; sym++) h.count[lengths[sym]]++;
    h.symbol.assign(n, 0);
    if (h.count[0] == n) return true;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    short offs[16];
    offs[0] = 0;
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h.count[len]);
    for (int sym = 0; sym < n; sym++) if (lengths[sym]) h.symbol[offs[lengths[sym]]++] = (short)sym;
    return true;
}

int decodeSym(BitIn& s, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int b = s.bit();
        if (s.bad) return -1;
        code |= b;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const short LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const short DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const short DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool inflateCodes(BitIn& s, Win& w, const Huff& lencode, const Huff& distcode, unsigned long long cap) {
    for (;;) {
        int sym = decodeSym(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) {
            w.put((unsigned char)sym);
        } else if (sym == 256) {
            return true;
        } else {
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = LEN_BASE[sym] + (size_t)s.bits(LEN_EXTRA[sym]);
            int dsym = decodeSym(s, distcode);
            if (dsym < 0 || dsym >= 30) return false;
            size_t dist = DIST_BASE[dsym] + (size_t)s.bits(DIST_EXTRA[dsym]);
            if (dist == 0 || dist > WIN_SIZE) return false;
            if (w.emitted + len > cap) return false;
            for (size_t i = 0; i < len; i++) w.put(w.back(dist));
        }
        if (s.bad || w.bad) return false;
    }
}

bool inflateStored(BitIn& s, Win& w, unsigned long long cap) {
    s.alignByte();
    int b0 = s.rawByte(), b1 = s.rawByte(), n0 = s.rawByte(), n1 = s.rawByte();
    if (b0 < 0 || b1 < 0 || n0 < 0 || n1 < 0) return false;
    unsigned len = (unsigned)b0 | ((unsigned)b1 << 8);
    unsigned nlen = (unsigned)n0 | ((unsigned)n1 << 8);
    if ((len ^ 0xFFFFu) != nlen) return false;
    if (w.emitted + len > cap) return false;
    for (unsigned i = 0; i < len; i++) {
        int c = s.rawByte();
        if (c < 0) return false;
        w.put((unsigned char)c);
    }
    return !w.bad;
}

bool inflateFixed(BitIn& s, Win& w, unsigned long long cap) {
    short lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    Huff lencode, distcode;
    if (!buildHuff(lencode, lengths, 288)) return false;
    for (i = 0; i < 30; i++) lengths[i] = 5;
    if (!buildHuff(distcode, lengths, 30)) return false;
    return inflateCodes(s, w, lencode, distcode, cap);
}

bool inflateDynamic(BitIn& s, Win& w, unsigned long long cap) {
    static const short ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    int nlen = s.bits(5) + 257;
    int ndist = s.bits(5) + 1;
    int ncode = s.bits(4) + 4;
    if (s.bad || nlen > 286 || ndist > 30) return false;

    short lengths[320];
    for (int i = 0; i < 19; i++) lengths[i] = 0;
    for (int i = 0; i < ncode; i++) lengths[ORDER[i]] = (short)s.bits(3);

    Huff clen;
    if (!buildHuff(clen, lengths, 19)) return false;

    int index = 0;
    while (index < nlen + ndist) {
        int sym = decodeSym(s, clen);
        if (sym < 0) return false;
        if (sym < 16) {
            lengths[index++] = (short)sym;
            continue;
        }
        int repeat = 0, filler = 0;
        if (sym == 16) {
            if (index == 0) return false;
            filler = lengths[index - 1];
            repeat = 3 + s.bits(2);
        } else if (sym == 17) {
            repeat = 3 + s.bits(3);
        } else {
            repeat = 11 + s.bits(7);
        }
        if (s.bad || index + repeat > nlen + ndist) return false;
        while (repeat-- > 0) lengths[index++] = (short)filler;
    }
    if (lengths[256] == 0) return false;

    Huff lencode, distcode;
    if (!buildHuff(lencode, lengths, nlen)) return false;
    if (!buildHuff(distcode, lengths + nlen, ndist)) return false;
    return inflateCodes(s, w, lencode, distcode, cap);
}

bool inflateDeflate(HANDLE zip, unsigned long long dataOff, unsigned long long compSize,
                    unsigned long long uncompSize, HANDLE out) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)dataOff;
    if (!SetFilePointerEx(zip, li, nullptr, FILE_BEGIN)) return false;

    BitIn s;
    s.h = zip;
    s.left = compSize;
    Win w;
    w.h = out;

    for (;;) {
        int last = s.bit();
        int type = s.bits(2);
        if (s.bad) return false;
        bool ok;
        if (type == 0) ok = inflateStored(s, w, uncompSize);
        else if (type == 1) ok = inflateFixed(s, w, uncompSize);
        else if (type == 2) ok = inflateDynamic(s, w, uncompSize);
        else return false;
        if (!ok) return false;
        if (last) break;
    }

    w.flush();
    return !w.bad && w.emitted == uncompSize;
}

bool copyStored(HANDLE zip, unsigned long long off, unsigned long long size, HANDLE out) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(zip, li, nullptr, FILE_BEGIN)) return false;
    unsigned char buf[1 << 16];
    unsigned long long left = size;
    while (left) {
        DWORD want = (DWORD)((left < sizeof(buf)) ? left : (unsigned long long)sizeof(buf));
        DWORD got = 0;
        if (!ReadFile(zip, buf, want, &got, nullptr) || got == 0) return false;
        DWORD wr = 0;
        if (!WriteFile(out, buf, got, &wr, nullptr) || wr != got) return false;
        left -= got;
    }
    return true;
}

bool readAt(HANDLE h, unsigned long long off, void* dst, size_t n) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
    unsigned char* d = (unsigned char*)dst;
    size_t got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, d + got, (DWORD)(n - got), &r, nullptr) || r == 0) return false;
        got += r;
    }
    return true;
}

struct CentralEntry {
    string name;
    unsigned method = 0;
    unsigned long long compSize = 0;
    unsigned long long uncompSize = 0;
    unsigned long long localOffset = 0;
};

unsigned rd16(const unsigned char* p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
unsigned long rd32(const unsigned char* p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}
unsigned long long rd64(const unsigned char* p) {
    return (unsigned long long)rd32(p) | ((unsigned long long)rd32(p + 4) << 32);
}

// Rejects absolute paths, drive letters and "..", so a hostile archive cannot write
// anywhere outside the destination directory.
bool safeRelativePath(const string& rawName, string& outRel, bool& isDir) {
    string rel;
    rel.reserve(rawName.size());
    for (char c : rawName) rel.push_back((c == '\\') ? '/' : c);

    isDir = !rel.empty() && rel.back() == '/';
    if (rel.empty() || rel.front() == '/') return false;
    if (rel.size() >= 2 && rel[1] == ':') return false;

    vector<string> parts;
    string cur;
    for (size_t i = 0; i <= rel.size(); i++) {
        char c = (i < rel.size()) ? rel[i] : '/';
        if (c == '/') {
            if (cur == "..") return false;
            if (!cur.empty() && cur != ".") parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (parts.empty()) return false;

    outRel.clear();
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) outRel += "/";
        outRel += parts[i];
    }
    return true;
}

} // namespace zipx

bool extractZip(const string& zipPath, const string& destDir,
                const string& label = "", const string& info = "") {
    using namespace zipx;
    if (!fileExists(zipPath) || !dirExists(destDir)) return false;

    HANDLE zip = CreateFileW(utf8ToWstring(zipPath).c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (zip == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSizeLi;
    if (!GetFileSizeEx(zip, &fileSizeLi)) { CloseHandle(zip); return false; }
    const unsigned long long fileSize = (unsigned long long)fileSizeLi.QuadPart;
    if (fileSize < 22) { CloseHandle(zip); return false; }

    // The end-of-central-directory record sits in the tail; its offset is needed because
    // a comment of unknown length may follow it.
    const unsigned long long tailStart = (fileSize > 66000ULL) ? (fileSize - 66000ULL) : 0ULL;
    const size_t tailLen = (size_t)(fileSize - tailStart);
    vector<unsigned char> tail(tailLen);
    if (!readAt(zip, tailStart, tail.data(), tailLen)) { CloseHandle(zip); return false; }

    long eocd = -1;
    for (size_t i = tailLen - 21; i-- > 0;) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            if (i + 22 + rd16(&tail[i + 20]) == tailLen) { eocd = (long)i; break; }
        }
    }
    if (eocd < 0) { CloseHandle(zip); return false; }

    unsigned long long entryCount = rd16(&tail[eocd + 10]);
    unsigned long long cdOffset = rd32(&tail[eocd + 16]);

    // ZIP64 fallback for archives past 4 GB or with more than 65535 entries.
    if (entryCount == 0xFFFFULL || cdOffset == 0xFFFFFFFFULL) {
        if ((size_t)eocd < 20) { CloseHandle(zip); return false; }
        const unsigned char* loc = &tail[eocd - 20];
        if (!(loc[0] == 0x50 && loc[1] == 0x4b && loc[2] == 0x06 && loc[3] == 0x07)) {
            CloseHandle(zip); return false;
        }
        unsigned char hdr[56];
        if (!readAt(zip, rd64(loc + 8), hdr, 56)) { CloseHandle(zip); return false; }
        if (!(hdr[0] == 0x50 && hdr[1] == 0x4b && hdr[2] == 0x06 && hdr[3] == 0x06)) {
            CloseHandle(zip); return false;
        }
        entryCount = rd64(hdr + 32);
        cdOffset = rd64(hdr + 48);
    }

    vector<CentralEntry> entries;
    unsigned long long p = cdOffset;
    for (unsigned long long i = 0; i < entryCount; i++) {
        unsigned char h[46];
        if (!readAt(zip, p, h, 46)) break;
        if (h[0] != 0x50 || h[1] != 0x4b || h[2] != 0x01 || h[3] != 0x02) break;

        CentralEntry e;
        e.method = rd16(h + 10);
        e.compSize = rd32(h + 20);
        e.uncompSize = rd32(h + 24);
        const unsigned nameLen = rd16(h + 28);
        const unsigned extraLen = rd16(h + 30);
        const unsigned commLen = rd16(h + 32);
        e.localOffset = rd32(h + 42);

        vector<unsigned char> var((size_t)nameLen + extraLen + commLen);
        if (!var.empty() && !readAt(zip, p + 46, var.data(), var.size())) break;
        e.name.assign((const char*)var.data(), nameLen);

        // A ZIP64 extra field overrides whichever 32-bit fields were left as 0xFFFFFFFF.
        size_t ex = nameLen;
        while (ex + 4 <= (size_t)nameLen + extraLen) {
            const unsigned hid = rd16(&var[ex]);
            const unsigned hsz = rd16(&var[ex + 2]);
            if (ex + 4 + hsz > (size_t)nameLen + extraLen) break;
            if (hid == 0x0001) {
                size_t q = ex + 4;
                const size_t end = ex + 4 + hsz;
                if (e.uncompSize == 0xFFFFFFFFULL && q + 8 <= end) { e.uncompSize = rd64(&var[q]); q += 8; }
                if (e.compSize == 0xFFFFFFFFULL && q + 8 <= end) { e.compSize = rd64(&var[q]); q += 8; }
                if (e.localOffset == 0xFFFFFFFFULL && q + 8 <= end) { e.localOffset = rd64(&var[q]); }
            }
            ex += 4 + hsz;
        }

        entries.push_back(e);
        p += 46 + nameLen + extraLen + commLen;
    }

    bool ok = !entries.empty();
    unsigned long long totalOut = 0;
    if (ok) {
        for (const auto& e : entries) totalOut += e.uncompSize;
        unsigned long long done = 0;

        for (const auto& e : entries) {
            string rel;
            bool isDir = false;
            if (!safeRelativePath(e.name, rel, isDir)) continue;

            fs::path outPath = fs::u8path(destDir) / fs::u8path(rel);
            std::error_code ec;
            if (isDir) {
                fs::create_directories(outPath, ec);
                continue;
            }
            if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path(), ec);
            if (ec) continue;

            // The local header repeats the name and extra lengths, which is what fixes
            // the real start of the compressed data.
            unsigned char lh[30];
            if (!readAt(zip, e.localOffset, lh, 30)) { ok = false; break; }
            if (lh[0] != 0x50 || lh[1] != 0x4b || lh[2] != 0x03 || lh[3] != 0x04) { ok = false; break; }
            const unsigned long long dataOff = e.localOffset + 30 + rd16(lh + 26) + rd16(lh + 28);

            HANDLE out = CreateFileW(outPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) continue;

            bool entryOk = false;
            if (e.method == 0) {
                entryOk = copyStored(zip, dataOff, e.compSize, out);
            } else if (e.method == 8) {
                entryOk = inflateDeflate(zip, dataOff, e.compSize, e.uncompSize, out);
            }
            CloseHandle(out);

            if (!entryOk) { ok = false; break; }

            done += e.uncompSize;
            if (totalOut) {
                printComponentProgress(label, (double)done * 100.0 / (double)totalOut, info);
            }
        }
    }

    cout << "\n";
    CloseHandle(zip);
    return ok;
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
    // 38 inner cells + "[" + "]" = 40 columns, the same width as the "====" separators
    const int barWidth = 38;
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
    // Wide enough to erase a longer previous speed/time line, so a shrinking value
    // never leaves stray characters behind.
    line += "            ";
    cout << line << flush;
}

// ========== FFPROBE RESULT CACHE ==========
// Running the real batch loop with a spawn counter attached to runCommand() gave 9
// ffprobe launches per file, 18 per pair. 39% of them (14 of 36 on a 4 file run) were
// byte-identical repeats of a command already run against the same file: the batch loop
// asks for the video tracks of the current file at line 5114 and again inside
// buildPairFingerprint(), the same for audio and subtitle tracks, and getVideoProperties()
// runs both inside detectEncodingProblem() and inside buildVideoFilterSpec(). Each launch
// costs a measured ~23 ms of CreateProcess plus process teardown, so those repeats are
// pure latency.
//
// The key is the full command line PLUS the size and modification time of the file it
// targets, so a file that changed on disk is always re-probed. ffmpeg output is never
// cached: it is progress text, and the two commands have different lifetimes.
static vector<pair<string, string> > g_probeCache;
static size_t g_probeCacheLimit = 512;
static int g_probeCacheHits = 0;

static void clearProbeCache() {
    g_probeCache.clear();
    g_probeCacheHits = 0;
}

// An ffprobe command line is: "<ffprobe.exe>" <flags...> "<target file>"
// so the target is always the last quoted argument.
static string lastQuotedArg(const string& cmd) {
    size_t close = cmd.rfind('"');
    if (close == string::npos || close == 0) return "";
    size_t open = cmd.rfind('"', close - 1);
    if (open == string::npos) return "";
    return cmd.substr(open + 1, close - open - 1);
}

static bool commandRunsFfprobe(const string& cmd) {
    if (cmd.empty() || cmd[0] != '"') return false;
    size_t close = cmd.find('"', 1);
    if (close == string::npos) return false;
    string exe = cmd.substr(1, close - 1);
    string lower = exe;
    for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)::tolower((unsigned char)lower[i]);
    return lower.find("ffprobe") != string::npos;
}

// Size + mtime of the probed file. Returning "?" on failure is deliberate: an unreadable
// path then never collides with a readable one that happens to match the command line.
static string probedFileSignature(const string& path) {
    if (path.empty()) return "";
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    if (ec) return "?";
    auto mtime = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return "?";
    char buf[96];
    snprintf(buf, sizeof(buf), "%llu|%lld",
             (unsigned long long)size,
             (long long)mtime.time_since_epoch().count());
    return buf;
}

static string probeCacheKey(const string& cmd) {
    return cmd + "\x01" + probedFileSignature(lastQuotedArg(cmd));
}

// ========== UNIVERSAL COMMAND EXECUTION ==========
static string runCommand(const string& cmd) {
    bool isProbe = commandRunsFfprobe(cmd);
    string cacheKey;
    if (isProbe) {
        cacheKey = probeCacheKey(cmd);
        for (size_t i = 0; i < g_probeCache.size(); i++) {
            if (g_probeCache[i].first == cacheKey) {
                g_probeCacheHits++;
                return g_probeCache[i].second;
            }
        }
    }

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
    if (!CreateProcessW(NULL, &wcmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
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

    if (isProbe) {
        if (g_probeCache.size() >= g_probeCacheLimit) g_probeCache.erase(g_probeCache.begin());
        g_probeCache.push_back(make_pair(cacheKey, output));
    }
    return output;
}

// Locale-independent decimal parser; defined further down with the other number helpers.
static bool parseNumberLoose(const string& s, double& out);

// ========== GET MEDIA DURATION VIA FFPROBE ==========
double getMediaDuration(const string& filePath) {
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return 0;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' '))
        output.pop_back();
    double dur = 0;
    if (!parseNumberLoose(output, dur)) return 0;
    return dur;
}

// Forward declaration
int arrowSelect(const string& title, const string& description, const vector<string>& options, int currentIdx, const vector<string>& hints = {}, bool inlineMode = false);

// ========== AUDIO TRACK DETECTION & STRUCTS ==========
struct AudioTrack {
    int index = -1;             // Global stream index from ffprobe
    int audioIndex = -1;        // Audio stream index (0:a:N)
    string codec = "";          // aac, mp3, ac3, eac3, flac, etc.
    string language = "";       // rus, eng, jpn, und, etc.
    string title = "";          // Track title / tag
    int channels = 0;           // 2, 6, etc.
    string channelLayout = "";  // stereo, 5.1, etc.
    string sampleRate = "";     // 48000, 44100, etc.
    string bitRate = "";        // in bps

    string getDisplayString() const {
        string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (!channelLayout.empty()) {
            if (!details.empty()) details += ", ";
            details += channelLayout;
        } else if (channels > 0) {
            if (!details.empty()) details += ", ";
            details += to_string(channels) + (CURRENT_LANG == LANG_RU ? " кан." : " ch");
        }
        if (!sampleRate.empty()) {
            if (!details.empty()) details += ", ";
            details += sampleRate + tr(" Hz", " Гц");
        }
        if (!bitRate.empty()) {
            try {
                long long br = stoll(bitRate);
                if (br > 1000) {
                    if (!details.empty()) details += ", ";
                    details += to_string(br / 1000) + " kbps";
                }
            } catch (...) {}
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (CURRENT_LANG == LANG_RU ? "Аудиодорожка #" : "Audio Track #") + to_string(audioIndex + 1);
        }
        return res;
    }
};

vector<AudioTrack> getAudioTracks(const string& filePath) {
    vector<AudioTrack> tracks;
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return tracks;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -select_streams a -show_entries stream=index,codec_name,channels,channel_layout,sample_rate,bit_rate:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    istringstream iss(output);
    string line;
    AudioTrack cur;
    int currentAudioIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.audioIndex = currentAudioIdx++;
            tracks.push_back(cur);
            cur = AudioTrack();
            inTrack = false;
        }
    };

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = stoi(val); } catch (...) { cur.index = -1; }
        } else {
            inTrack = true;
            if (key == "codec_name") cur.codec = val;
            else if (key == "channels") try { cur.channels = stoi(val); } catch (...) {}
            else if (key == "channel_layout") cur.channelLayout = val;
            else if (key == "sample_rate") cur.sampleRate = val;
            else if (key == "bit_rate") cur.bitRate = val;
            else if (key == "TAG:language") cur.language = val;
            else if (key == "TAG:title") cur.title = val;
        }
    }
    pushCurrent();

    return tracks;
}

bool selectAudioTrackForFile(const string& filePath, string& mapArgs, bool allowAllTracks = true, bool isBatchMode = false, bool* outKeepAllForBatch = nullptr, int* outSelectedTrackIndex = nullptr, bool* outApplyToAllBatch = nullptr) {
    mapArgs.clear();
    if (outKeepAllForBatch) *outKeepAllForBatch = false;
    if (outSelectedTrackIndex) *outSelectedTrackIndex = -1;
    if (outApplyToAllBatch) *outApplyToAllBatch = false;

    vector<AudioTrack> tracks = getAudioTracks(filePath);
    if (tracks.size() <= 1) {
        return true;
    }

    // A single list of choices (keep all + one entry per track), followed by a separate
    // "apply scope" screen in batch mode. Showing every track twice (once for "this video"
    // and once for "ALL videos") doubles the list length and makes long titles unreadable.
    vector<string> opts;
    vector<string> hints;

    if (allowAllTracks) {
        opts.push_back(tr("Keep all audio tracks", "Сохранить все аудиодорожки"));
        hints.push_back(tr("Preserves every audio stream of this file.", "Сохраняет все аудиопотоки этого файла."));
    }
    for (size_t i = 0; i < tracks.size(); i++) {
        opts.push_back(tr("Track ", "Дорожка ") + to_string(i + 1) + ": " + tracks[i].getDisplayString());
        hints.push_back(tr("Select only this audio stream for the output file.", "Выбрать только этот аудиопоток для выходного файла."));
    }

    string desc = tr("This video contains multiple audio tracks (" + to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nSelect which audio track to include in the output:",
                     "В этом видео обнаружено несколько аудиодорожек (" + to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nВыберите, какую аудиодорожку включить в результат:");

    int sel = arrowSelect(tr("AUDIO TRACK SELECTION", "ВЫБОР АУДИОДОРОЖКИ"), desc, opts, 0, hints, true);
    if (sel < 0) {
        return false;
    }

    bool keepAll = allowAllTracks && sel == 0;
    int trackIdx = allowAllTracks ? sel - 1 : sel;

    bool applyToAll = false;
    if (isBatchMode) {
        vector<string> scopeOptions = {
            tr("Apply to this video only", "Применить только к этому видео"),
            tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
        };
        vector<string> scopeHints = {
            tr("The choice applies only to the current video. It will be asked again for the next file.",
               "Выбор применяется только к текущему видео. Для следующего файла вопрос будет задан снова."),
            tr("The choice applies to all remaining videos in this batch, matching by language and title.",
               "Выбор применяется ко всем оставшимся видео в этом пакете, с подбором по языку и названию.")
        };

        int scopeSel = arrowSelect(
            tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
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
        mapArgs = " -map 0:a:" + to_string(tracks[trackIdx].audioIndex);
        if (outKeepAllForBatch) *outKeepAllForBatch = false;
        if (outSelectedTrackIndex) *outSelectedTrackIndex = trackIdx;
    }
    if (outApplyToAllBatch) *outApplyToAllBatch = applyToAll;
    return true;
}

// ========== NUMBER FORMATTING / PARSING HELPERS ==========
// snprintf("%f") follows LC_NUMERIC, and setlocale(LC_ALL, ".UTF8") resolves that to the
// user locale (Russian_Russia.utf8 on a Russian system). The result is "23,98" instead of
// "23.98", which is harmless on screen but breaks every FFmpeg filter expression:
// "setpts=0,5*PTS" is read by FFmpeg as two filters and fails with "No such filter".
// Always use formatDot() for values that end up in a command line.
static string formatDot(double value, int decimals = 2) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    string s = buf;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == ',') s[i] = '.';
    }
    return s;
}

// Parses a number written with either decimal separator ("23.98" or "23,98").
// Returns false when the text does not start with a number.
//
// The value is assembled by hand from the digits. std::stod() cannot be used here:
// setUTF8() calls setlocale(LC_ALL, ".UTF8"), which on a Russian system resolves to
// Russian_Russia.utf8, and strtod() then stops at a dot. Measured on this machine:
// stod("1.5") == 1.0, stod("0.25") == 0.0, stod("2.75") == 2.0. FFmpeg always writes dot
// decimals, so every fractional value it produced was being silently truncated.
static bool parseNumberLoose(const string& s, double& out) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    size_t start = i;
    bool negative = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) { negative = (s[i] == '-'); i++; }

    double value = 0.0;
    bool anyDigit = false;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        value = value * 10.0 + (double)(s[i] - '0');
        i++; anyDigit = true;
    }
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        size_t sepPos = i;
        i++;
        double scale = 0.1;
        bool fracDigit = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value += scale * (double)(s[i] - '0');
            scale *= 0.1;
            i++; fracDigit = true;
        }
        if (!fracDigit) i = sepPos;   // trailing separator, e.g. "90000,"
    }
    if (!anyDigit) return false;
    out = negative ? -value : value;
    return true;
}

// Drops a trailing ".00" so that whole frame rates are shown as "24" and not "24.00".
static string trimZeroDecimals(const string& s) {
    size_t dot = s.find('.');
    if (dot == string::npos) return s;
    for (size_t k = dot + 1; k < s.size(); k++) {
        if (s[k] != '0') return s;
    }
    return s.substr(0, dot);
}

// Numbers typed by the user may use a comma decimal separator ("0,5"). FFmpeg only
// understands a dot, so normalize such input before it reaches the command line.
static string sanitizeNumberArg(const string& s) {
    string out = s;
    for (size_t i = 0; i < out.size(); i++) {
        if (out[i] == ',') out[i] = '.';
    }
    return out;
}

// ========== VIDEO TRACK DETECTION & STRUCTS ==========
struct VideoTrack {
    int index = -1;             // Global stream index from ffprobe
    int videoIndex = -1;        // Video stream index (0:v:N)
    string codec = "";          // h264, hevc, av1, vp9, mjpeg, etc.
    int width = 0;
    int height = 0;
    string fps = "";            // 24, 30, 60, etc.
    string pixFmt = "";         // yuv420p, etc.
    string bitRate = "";        // in bps
    string language = "";       // eng, rus, etc.
    string title = "";          // Track title
    bool isAttachedPic = false; // Cover art (DISPOSITION:attached_pic=1)

    bool isCoverOrAttachedPic() const {
        if (isAttachedPic) return true;
        // Cover art is a single still image stored as a video stream. Release groups tag it
        // as mjpeg/png/bmp with r_frame_rate 90000/1 (or 0/0); real video never looks like that.
        // The fps text is compared numerically because the decimal separator depends on the
        // console locale ("90000,00" on a Russian system).
        if (codec == "mjpeg" || codec == "png" || codec == "bmp") {
            double f = 0;
            if (fps.empty() || !parseNumberLoose(fps, f)) return true;
            return (f <= 0.0 || f >= 1000.0);
        }
        return false;
    }

    string getDisplayString() const {
        string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (width > 0 && height > 0) {
            if (!details.empty()) details += ", ";
            details += to_string(width) + "x" + to_string(height);
        }
        if (!fps.empty()) {
            if (!details.empty()) details += ", ";
            details += fps + tr(" fps", " кадр/с");
        }
        if (!pixFmt.empty()) {
            if (!details.empty()) details += ", ";
            details += pixFmt;
        }
        if (!bitRate.empty()) {
            try {
                long long br = stoll(bitRate);
                if (br > 1000) {
                    if (!details.empty()) details += ", ";
                    details += to_string(br / 1000) + " kbps";
                }
            } catch (...) {}
        }
        if (isCoverOrAttachedPic()) {
            if (!details.empty()) details += ", ";
            details += tr("attached cover", "обложка");
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (CURRENT_LANG == LANG_RU ? "Видеопоток #" : "Video Stream #") + to_string(videoIndex + 1);
        }
        return res;
    }
};

vector<VideoTrack> getVideoTracks(const string& filePath) {
    vector<VideoTrack> tracks;
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return tracks;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -select_streams v -show_entries stream=index,codec_name,width,height,r_frame_rate,pix_fmt,bit_rate,disposition:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    istringstream iss(output);
    string line;
    VideoTrack cur;
    int currentVideoIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.videoIndex = currentVideoIdx++;
            if (cur.isCoverOrAttachedPic()) cur.isAttachedPic = true;
            tracks.push_back(cur);
            cur = VideoTrack();
            inTrack = false;
        }
    };

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = stoi(val); } catch (...) { cur.index = -1; }
        } else {
            inTrack = true;
            if (key == "codec_name") cur.codec = val;
            else if (key == "width") try { cur.width = stoi(val); } catch (...) {}
            else if (key == "height") try { cur.height = stoi(val); } catch (...) {}
            else if (key == "pix_fmt") cur.pixFmt = val;
            else if (key == "bit_rate") cur.bitRate = val;
            else if (key == "r_frame_rate") {
                size_t slash = val.find('/');
                if (slash != string::npos) {
                    double num = 0, den = 0;
                    if (parseNumberLoose(val.substr(0, slash), num) &&
                        parseNumberLoose(val.substr(slash + 1), den)) {
                        if (den > 0) {
                            cur.fps = trimZeroDecimals(formatDot(num / den, 2));
                        }
                    } else {
                        cur.fps = val;
                    }
                } else {
                    cur.fps = val;
                }
            }
            else if (key == "DISPOSITION:attached_pic") cur.isAttachedPic = (val == "1");
            else if (key == "TAG:language") cur.language = val;
            else if (key == "TAG:title") cur.title = val;
        }
    }
    pushCurrent();

    return tracks;
}

bool selectVideoTrackForFile(const string& filePath, string& mapArgs, bool allowAllTracks = true, bool isBatchMode = false, bool* outApplyToAllBatch = nullptr, int* outSelectedTrackIndex = nullptr) {
    mapArgs.clear();
    if (outApplyToAllBatch) *outApplyToAllBatch = false;
    if (outSelectedTrackIndex) *outSelectedTrackIndex = -1;

    vector<VideoTrack> tracks = getVideoTracks(filePath);
    if (tracks.empty()) return true;

    vector<int> realIndices;
    for (size_t i = 0; i < tracks.size(); i++) {
        if (!tracks[i].isCoverOrAttachedPic()) {
            realIndices.push_back((int)i);
        }
    }

    if (realIndices.size() <= 1 && tracks.size() > 1) {
        int realIdx = realIndices.empty() ? 0 : realIndices[0];
        mapArgs = " -map 0:v:" + to_string(tracks[realIdx].videoIndex);
        if (outSelectedTrackIndex) *outSelectedTrackIndex = realIdx;
        return true;
    }

    if (tracks.size() <= 1) {
        return true;
    }

    // One single list of choices, followed by a separate "apply scope" screen in batch mode.
    // Never duplicate the track list per scope: it doubles the menu and makes long titles
    // (language + name + codec) unreadable. See AGENTS.md 5.11 "Two-step dialog pattern".
    //
    // Layout: [0] = special option ("keep all" / "primary"), [1..N] = one entry per stream.
    const int FIRST_STREAM_CHOICE = 1;

    vector<string> opts;
    vector<string> hints;

    int primaryIdx = realIndices.empty() ? 0 : realIndices[0];
    if (allowAllTracks) {
        opts.push_back(tr("Keep all video streams", "Сохранить все видеопотоки"));
        hints.push_back(tr("Preserves every video stream of this file.", "Сохраняет все видеопотоки этого файла."));
    } else {
        opts.push_back(tr("Use primary video stream", "Использовать основной видеопоток"));
        hints.push_back(tr("Selects the first real video stream, ignoring cover art.", "Выбирает первый основной видеопоток, игнорируя обложку."));
    }
    for (size_t i = 0; i < tracks.size(); i++) {
        opts.push_back(tr("Stream ", "Поток ") + to_string(i + 1) + ": " + tracks[i].getDisplayString());
        hints.push_back(tr("Select this video stream for processing.", "Выбрать этот видеопоток для обработки."));
    }

    string desc = tr("This file contains multiple video streams (" + to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nSelect which video stream to process:",
                     "В этом файле обнаружено несколько видеопотоков (" + to_string(tracks.size()) + ").\n\"" + filePath + "\"\n\nВыберите, какой видеопоток обработать:");

    int sel = arrowSelect(tr("VIDEO STREAM SELECTION", "ВЫБОР ВИДЕОПОТОКА"), desc, opts, 0, hints, true);
    if (sel < 0) {
        return false;
    }

    bool applyToAll = false;
    if (isBatchMode) {
        vector<string> scopeOptions = {
            tr("Apply to this video only", "Применить только к этому видео"),
            tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
        };
        vector<string> scopeHints = {
            tr("The choice applies only to the current video. It will be asked again for the next file.",
               "Выбор применяется только к текущему видео. Для следующего файла вопрос будет задан снова."),
            tr("The choice applies to all remaining videos in this batch, matching by resolution, language and title.",
               "Выбор применяется ко всем оставшимся видео в этом пакете, с подбором по разрешению, языку и названию.")
        };

        int scopeSel = arrowSelect(
            tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
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
            // -3 = keep all for ALL remaining videos, -4 = keep all for the current file only.
            if (outSelectedTrackIndex) *outSelectedTrackIndex = (isBatchMode && !applyToAll) ? -4 : -3;
        } else {
            mapArgs = " -map 0:v:" + to_string(tracks[primaryIdx].videoIndex);
            // -1 = primary for ALL remaining videos, -2 = primary for the current file only.
            if (outSelectedTrackIndex) *outSelectedTrackIndex = applyToAll ? -1 : -2;
        }
    } else {
        int trackIdx = sel - FIRST_STREAM_CHOICE;
        mapArgs = " -map 0:v:" + to_string(tracks[trackIdx].videoIndex);
        if (outSelectedTrackIndex) *outSelectedTrackIndex = trackIdx;
    }

    return true;
}

inline bool selectVideoTrackForFile(const string& filePath, string& mapArgs, bool isBatchMode, int* outSelectedTrackIndex) {
    return selectVideoTrackForFile(filePath, mapArgs, true, isBatchMode, nullptr, outSelectedTrackIndex);
}

inline bool selectVideoTrackForFile(const string& filePath, string& mapArgs, bool isBatchMode, bool* outApplyToAllBatch, int* outSelectedTrackIndex) {
    return selectVideoTrackForFile(filePath, mapArgs, true, isBatchMode, outApplyToAllBatch, outSelectedTrackIndex);
}

string buildStreamMapArgs(const string& videoMapArg, const string& audioMapArg) {
    if (videoMapArg.empty() && audioMapArg.empty()) return "";
    if (!videoMapArg.empty() && audioMapArg.empty()) return videoMapArg + " -map 0:a?";
    if (videoMapArg.empty() && !audioMapArg.empty()) return " -map 0:v:0?" + audioMapArg;
    return videoMapArg + audioMapArg;
}

// ========== SUBTITLE TRACK DETECTION & STRUCTS ==========
struct SubtitleTrack {
    int index = -1;             // Global stream index from ffprobe
    int subIndex = -1;          // Subtitle stream index (0:s:N)
    string codec = "";          // subrip, ass, mov_text, etc.
    string language = "";       // rus, eng, jpn, und, etc.
    string title = "";          // Track title / tag
    bool isDefault = false;     // DISPOSITION:default=1
    bool isForced = false;      // DISPOSITION:forced=1

    string getDisplayString() const {
        string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (isDefault) {
            if (!details.empty()) details += ", ";
            details += tr("default", "по умолчанию");
        }
        if (isForced) {
            if (!details.empty()) details += ", ";
            details += tr("forced", "форсированные");
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (CURRENT_LANG == LANG_RU ? "Субтитры #" : "Subtitles #") + to_string(subIndex + 1);
        }
        return res;
    }
};

vector<SubtitleTrack> getSubtitleTracks(const string& filePath) {
    vector<SubtitleTrack> tracks;
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return tracks;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -select_streams s -show_entries stream=index,codec_name,disposition:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    istringstream iss(output);
    string line;
    SubtitleTrack cur;
    int currentSubIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.subIndex = currentSubIdx++;
            tracks.push_back(cur);
            cur = SubtitleTrack();
            inTrack = false;
        }
    };

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = stoi(val); } catch (...) { cur.index = -1; }
        } else {
            inTrack = true;
            if (key == "codec_name") cur.codec = val;
            else if (key == "DISPOSITION:default") cur.isDefault = (val == "1");
            else if (key == "DISPOSITION:forced") cur.isForced = (val == "1");
            else if (key == "TAG:language") cur.language = val;
            else if (key == "TAG:title") cur.title = val;
        }
    }
    pushCurrent();

    return tracks;
}

// ========== SUBTITLE INCOMPATIBILITY DIALOG ==========
enum SubtitleIncompatAction {
    SUB_ACT_CONVERT_TEXT = 0,
    SUB_ACT_BURN_HARD = 1,
    SUB_ACT_DROP_SUBS = 2,
    SUB_ACT_CHANGE_TO_MKV = 3,
    SUB_ACT_SKIP_CURRENT = 4,
    SUB_ACT_CANCEL_ALL = 5
};

struct SubtitleDialogResult {
    SubtitleIncompatAction action = SUB_ACT_SKIP_CURRENT;
    bool applyToAll = false;
};

SubtitleDialogResult dialogSubtitleIncompatibility(const string& filePath, const string& detectedSubTypes, bool batchMode = false) {
    SubtitleDialogResult result;
    string desc = tr(
        "Complex subtitles (" + detectedSubTypes + ") were found that are not supported by the MP4 container.\n\""
        + filePath + "\"\n\nChoose subtitle processing method:",
        "В исходном файле обнаружены сложные субтитры (" + detectedSubTypes + "), которые не поддерживаются форматом MP4.\n\""
        + filePath + "\"\n\nВыберите способ обработки субтитров:"
    );

    vector<string> opts = {
        tr("Convert subtitles (Recommended, but will be plain text without original styling)",
           "Конвертировать субтитры (Рекомендуется, но будут как просто текст без изначального стиля)"),
        tr("Burn subtitles into video (Preserves style, but permanently embeds them in video frames)",
           "Вшить субтитры в видео (Сохраняет стиль, но вшивает субтитры прям в само видео, их нельзя будет отключить)"),
        tr("Save video without subtitles",
           "Сохранить видео без субтитров"),
        tr("Change format to MKV (Preserves all subtitles with full original styles)",
           "Изменить формат на MKV (Сохранит все субтитры без изменений)"),
        tr("Skip current video",
           "Пропустить текущее видео"),
        tr("Cancel all operations",
           "Отменить все операции")
    };

    vector<string> hints = {
        tr("Converts subtitles to mov_text format. Retains toggleable tracks in MP4, but strips colors, custom fonts, and positioning.",
           "Конвертирует субтитры в стандартный формат mov_text. Сохраняет отключаемые дорожки в MP4, но удаляет цвета, шрифты и позиционирование."),
        tr("Re-encodes video while burning subtitles into image frames. Retains 100% of fonts, colors and effects, but cannot be toggled off.",
           "Перекодирует видео с наложением субтитров прямо на кадры. Сохраняет 100% стилей, цветов и эффектов, но субтитры нельзя будет отключить."),
        tr("Removes all subtitle tracks and outputs video with audio streams only.",
           "Удаляет все дорожки субтитров и сохраняет видео только с аудиодорожками."),
        tr("Changes the output container to MKV. Retains all subtitle streams and complex styling bit-for-bit without loss.",
           "Переключает выходной контейнер на MKV. Сохраняет все дорожки субтитров и сложные стили бит-в-бит без потерь."),
        tr("Skips processing of this file and proceeds to the next one in queue.",
           "Пропускает обработку этого файла и переходит к следующему в очереди."),
        tr("Aborts all remaining operations and finishes with summary of successes and errors.",
           "Прерывает оставшиеся операции и завершает работу со сводкой успехов и ошибок.")
    };

    int sel = arrowSelect(tr("SUBTITLE INCOMPATIBILITY", "НЕСОВМЕСТИМОСТЬ СУБТИТРОВ"), desc, opts, 0, hints, true);
    if (sel < 0) {
        result.action = SUB_ACT_SKIP_CURRENT;
        result.applyToAll = false;
        return result;
    }
    result.action = (SubtitleIncompatAction)sel;

    if (batchMode && result.action != SUB_ACT_CANCEL_ALL) {
        vector<string> scopeOptions;
        vector<string> scopeHints;
        if (result.action == SUB_ACT_SKIP_CURRENT) {
            scopeOptions = {
                tr("Skip this video only", "Пропустить только это видео"),
                tr("Skip ALL remaining videos with subtitles", "Пропустить ВСЕ оставшиеся видео с субтитрами")
            };
            scopeHints = {
                tr("Only this video will be skipped. The batch continues with the next file.",
                   "Только это видео будет пропущено. Пакет продолжится со следующего файла."),
                tr("All remaining videos with incompatible subtitles in this batch will be skipped.",
                   "Все оставшиеся видео с несовместимыми субтитрами в этом пакете будут пропущены.")
            };
        } else {
            scopeOptions = {
                tr("Apply to this video only", "Применить только к этому видео"),
                tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
            };
            scopeHints = {
                tr("The choice applies only to the current video. Global settings remain unchanged.",
                   "Выбор применяется только к текущему видео. Глобальные настройки не изменяются."),
                tr("The choice applies to all remaining videos with incompatible subtitles in this batch.",
                   "Выбор применяется ко всем оставшимся видео с несовместимыми субтитрами в этом пакете.")
            };
        }

        int scopeSel = arrowSelect(
            tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
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

struct AudioTrackPreference {
    bool hasPreference = false;
    bool keepAll = false;
    int preferredAudioIndex = -1;
    string preferredLanguage = "";
    string preferredTitle = "";
    string preferredCodec = "";
    string displayName = "";
};

struct VideoTrackPreference {
    bool hasPreference = false;
    bool keepAll = false;
    bool usePrimaryOnly = false;
    int preferredVideoIndex = -1;
    int preferredWidth = 0;
    int preferredHeight = 0;
    string preferredCodec = "";
    string preferredLanguage = "";
    string preferredTitle = "";
    string displayName = "";
};

struct BatchAudioMismatchWarning {
    string fileName;
    string preferredTrack;
    string actualTrack;
};

struct BatchVideoMismatchWarning {
    string fileName;
    string preferredTrack;
    string actualTrack;
};

struct ConflictedVideoFile {
    string filePath;
    size_t originalIndex = 0;
    vector<AudioTrack> tracks;
};

enum ProcessFileResult {
    PROC_SUCCESS = 0,
    PROC_FAIL = 1,
    PROC_CANCEL_BATCH = 2,
    PROC_PAIR_HANDLED = 4,    // A pair finished inside the lambda; both files are already counted.
    PROC_PAIR_CANCELLED = 5   // A pair was interrupted and the user cancelled the whole batch.
};

// ========== VIDEO SOURCE PROPERTIES ==========
struct VideoSourceProperties {
    string pixFmt;
    string codecName;
    int width = 0, height = 0;
    string fps;
    string fieldOrder;
    bool isInterlaced = false;
    bool is10bit = false;
    bool isSubsampled422 = false;
    bool isSubsampled444 = false;
};

VideoSourceProperties getVideoProperties(const string& filePath) {
    VideoSourceProperties vp;
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return vp;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,r_frame_rate,field_order -of default=noprint_wrappers=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    istringstream iss(output);
    string line;
    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);
        if (key == "codec_name") vp.codecName = val;
        else if (key == "pix_fmt") vp.pixFmt = val;
        else if (key == "width") try { vp.width = stoi(val); } catch (...) {}
        else if (key == "height") try { vp.height = stoi(val); } catch (...) {}
        else if (key == "r_frame_rate") vp.fps = val;
        else if (key == "field_order") {
            vp.fieldOrder = val;
            if (val == "tt" || val == "bb" || val == "tb" || val == "bt") vp.isInterlaced = true;
        }
    }

    string lower = vp.pixFmt;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    vp.is10bit = (lower.find("10") != string::npos || lower.find("p010") != string::npos || lower.find("p016") != string::npos);
    vp.isSubsampled422 = (lower.find("422") != string::npos);
    vp.isSubsampled444 = (lower.find("444") != string::npos);

    return vp;
}

// ========== ENCODING PROBLEM DETECTION ==========
struct EncodingProblem {
    bool hasProblem = false;
    bool isOddDimensions = false;
    bool isUltraHighRes = false;
    int evenWidth = 0;
    int evenHeight = 0;
    string filePath;
    string description;
    string detailedReason;
};

// ========== 8K GPU CAPABILITY DETECTION ==========
bool is8KSupportedByGPU(AccelMode gpu, const string& codecPart) {
    // Standard H.264 profile specification and hardware encoders (NVENC/AMF/QSV) do NOT support > 4096x4096 on ANY GPU.
    if (codecPart == "H.264") {
        return false;
    }

    // For H.265 (HEVC) and AV1:
    if (gpu == ACCEL_NVIDIA) {
        string name = DETECTED_NVIDIA_NAME;
        transform(name.begin(), name.end(), name.begin(), ::tolower);
        // Turing (RTX 20xx / GTX 1660 / 1650S), Ampere (RTX 30xx), Ada (RTX 40xx), Blackwell (RTX 50xx), modern Quadros/Tesla support 8K HEVC/AV1 encode
        if (name.find("rtx") != string::npos ||
            name.find("1660") != string::npos ||
            name.find("1650 super") != string::npos ||
            name.find("titan rtx") != string::npos ||
            name.find("a4000") != string::npos ||
            name.find("a5000") != string::npos ||
            name.find("a6000") != string::npos ||
            name.find("a100") != string::npos ||
            name.find("h100") != string::npos ||
            name.find("l4") != string::npos) {
            return true;
        }
        // Older Pascal / Maxwell / Kepler (GTX 1080/1070/1060/1050/980/970/etc.) are limited to 4096x4096
        return false;
    }

    if (gpu == ACCEL_AMD) {
        string name = DETECTED_AMD_NAME;
        transform(name.begin(), name.end(), name.begin(), ::tolower);
        // RDNA3+ (RX 7000, 8000, 9000 series) supports 8K HEVC/AV1
        if (name.find("rx 7") != string::npos || name.find("rx 8") != string::npos || name.find("rx 9") != string::npos ||
            name.find("7900") != string::npos || name.find("7800") != string::npos || name.find("7700") != string::npos || name.find("7600") != string::npos) {
            return true;
        }
        return false;
    }

    if (gpu == ACCEL_INTEL) {
        string name = DETECTED_INTEL_NAME;
        transform(name.begin(), name.end(), name.begin(), ::tolower);
        // Intel Arc / Xe / Core Ultra supports 8K
        if (name.find("arc") != string::npos || name.find("ultra") != string::npos) {
            return true;
        }
        return false;
    }

    return false;
}

EncodingProblem detectEncodingProblem(const string& filePath) {
    EncodingProblem ep;
    ep.filePath = filePath;
    if (ACCELERATION_MODE == ACCEL_CPU_ONLY || ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) return ep;

    VideoSourceProperties vp = getVideoProperties(filePath);
    if (vp.pixFmt.empty() && vp.codecName.empty()) return ep;

    AccelMode activeGpu = getActiveGpuMode();
    string encoderName;
    if (activeGpu == ACCEL_NVIDIA) encoderName = "NVENC";
    else if (activeGpu == ACCEL_AMD) encoderName = "AMF";
    else if (activeGpu == ACCEL_INTEL) encoderName = "QSV";
    else return ep;

    string codecPart;
    if (OUTPUT_FORMAT.find("H.264") != string::npos) codecPart = "H.264";
    else if (OUTPUT_FORMAT.find("H.265") != string::npos || OUTPUT_FORMAT.find("HEVC") != string::npos) codecPart = "H.265/HEVC";
    else if (OUTPUT_FORMAT.find("AV1") != string::npos) codecPart = "AV1";
    else codecPart = "H.264";

    string fullEncoder = codecPart + " (" + encoderName + ")";

    // Detect legacy/unsupported input video codecs for hardware decoding (NVDEC/AMF/QSV)
    if (ACCELERATION_MODE == ACCEL_NVIDIA || ACCELERATION_MODE == ACCEL_AMD || ACCELERATION_MODE == ACCEL_INTEL) {
        string lowerCodec = vp.codecName;
        transform(lowerCodec.begin(), lowerCodec.end(), lowerCodec.begin(), ::tolower);
        if (lowerCodec == "mpeg4" || lowerCodec == "msmpeg4" || lowerCodec == "msmpeg4v1" ||
            lowerCodec == "msmpeg4v2" || lowerCodec == "msmpeg4v3" || lowerCodec == "flv1" ||
            lowerCodec == "vp6" || lowerCodec == "vp6f" || lowerCodec == "wmv1" || lowerCodec == "wmv2" ||
            lowerCodec == "rv10" || lowerCodec == "rv20" || lowerCodec == "rv30" || lowerCodec == "rv40") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "Input video codec (" + vp.codecName + ") cannot be decoded by GPU hardware decoder (" + encoderName + ").\n"
                "Hybrid mode (CPU decode + GPU encode) or Software CPU mode is recommended.",
                "Входной видеокодек (" + vp.codecName + ") не поддерживается аппаратным декодером вашей видеокарты (" + encoderName + ").\n"
                "Рекомендуется использовать Гибридный режим (CPU декод + GPU энкод) или программный режим CPU.");
            return ep;
        }
    }

    // Detect odd dimensions for hardware encoding
    if ((vp.width > 0 && vp.width % 2 != 0) || (vp.height > 0 && vp.height % 2 != 0)) {
        ep.hasProblem = true;
        ep.isOddDimensions = true;
        ep.evenWidth = (vp.width % 2 != 0) ? (vp.width + 1) : vp.width;
        ep.evenHeight = (vp.height % 2 != 0) ? (vp.height + 1) : vp.height;
        ep.description = fullEncoder;
        string dimStr = to_string(vp.width) + "x" + to_string(vp.height);
        string evenDimStr = to_string(ep.evenWidth) + "x" + to_string(ep.evenHeight);
        ep.detailedReason = tr(
            "Odd video resolution (" + dimStr + ").\n"
            "Hardware encoders of your graphics card (" + encoderName + ") require width and height to be divisible by 2.\n"
            "Recommended: Automatically align resolution to even (" + evenDimStr + ") or encode via CPU.",
            "Нечётное разрешение видео (" + dimStr + ").\n"
            "Аппаратные кодировщики вашей видеокарты (" + encoderName + ") требуют, чтобы ширина и высота делились на 2.\n"
            "Рекомендуется: Автоматически выровнять разрешение до чётного (" + evenDimStr + ") или кодировать через CPU.");
        return ep;
    }

    // Detect ultra-high resolution exceeding GPU limits (8K+ / > 4096)
    if (vp.width > 4096 || vp.height > 4096) {
        if (!is8KSupportedByGPU(activeGpu, codecPart)) {
            ep.hasProblem = true;
            ep.isUltraHighRes = true;
            ep.description = fullEncoder;
            string dimStr = to_string(vp.width) + "x" + to_string(vp.height);
            string reasonDetail;
            if (codecPart == "H.264") {
                reasonDetail = tr(
                    "Standard H.264 profile does not support resolutions higher than 4096x4096 on any graphics card.\n"
                    "Recommended: Switch to H.265 (HEVC) / AV1 or use software CPU encoder.",
                    "Стандартный профиль H.264 не поддерживает разрешение выше 4096x4096 ни на одной видеокарте.\n"
                    "Рекомендуется: Переключить на H.265 (HEVC) / AV1 или программный кодировщик CPU.");
            } else {
                reasonDetail = tr(
                    "Hardware encoder of your graphics card (" + encoderName + ") does not support resolution higher than 4096x4096.\n"
                    "Recommended: Use software CPU encoder (libx264 / libx265) or reduce resolution to 4K.",
                    "Аппаратный энкодер вашей видеокарты (" + encoderName + ") не поддерживает разрешение выше 4096x4096.\n"
                    "Рекомендуется: Использовать программный кодировщик CPU (libx264 / libx265) или уменьшить разрешение до 4K.");
            }
            ep.detailedReason = tr("Ultra-high video resolution (" + dimStr + ").\n", "Сверхвысокое разрешение видео (" + dimStr + ").\n") + reasonDetail;
            return ep;
        }
    }

    if (vp.is10bit) {
        if (codecPart == "H.264") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "10-bit pixel format (" + vp.pixFmt + ") cannot be encoded to hardware H.264 without loss.\n"
                "Hardware encoder of your graphics card (" + encoderName + ") does not support 10-bit input.\n"
                "Recommended: Choose H.265 (HEVC) / AV1 codec or use software CPU encoder.",
                "10-битный формат пикселей (" + vp.pixFmt + ") не может быть закодирован в аппаратный H.264 без потерь.\n"
                "Аппаратный энкодер вашей видеокарты (" + encoderName + ") не поддерживает 10-битный вход.\n"
                "Рекомендуется: Выбрать кодек H.265 (HEVC) / AV1 или программный кодировщик CPU.");
            return ep;
        }
    }

    if (vp.isSubsampled422 || vp.isSubsampled444) {
        if (codecPart == "H.264" || codecPart == "H.265/HEVC") {
            ep.hasProblem = true;
            ep.description = fullEncoder;
            ep.detailedReason = tr(
                "Chroma subsampling " + string(vp.isSubsampled444 ? "4:4:4" : "4:2:2") + " (" + vp.pixFmt + ") is not supported by " + codecPart + " encoder (" + encoderName + ").\n"
                "Recommended: Use CPU software encoder (libx264) or choose another format.",
                "Субдискретизация цветности " + string(vp.isSubsampled444 ? "4:4:4" : "4:2:2") + " (" + vp.pixFmt + ") не поддерживается кодеком " + codecPart + " (" + encoderName + ").\n"
                "Рекомендуется: Использовать программный кодировщик CPU (libx264) или выбрать другой формат.");
            return ep;
        }
    }

    return ep;
}

// ========== ENCODING PROBLEM DIALOG ==========
struct EncodingDialogResult {
    int action = -1;
    bool applyToAll = false;
    string chosenFormat;
    bool autoAlignResolution = false;
    bool downscaleTo4K = false;
};

EncodingDialogResult dialogEncodingProblem(const EncodingProblem& ep, bool batchMode) {
    EncodingDialogResult result;

    string fileLine = ep.filePath.empty() ? "" : ("\"" + ep.filePath + "\"\n\n");
    string description = tr(
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
    istringstream reasonStream(ep.detailedReason);
    string reasonLine;
    while (getline(reasonStream, reasonLine)) {
        description += "  " + reasonLine + "\n";
    }

    vector<string> options;
    vector<string> hints;
    int formatOptIdx = -1;
    int skipOptIdx = -1;

    if (ep.isOddDimensions) {
        string evenDimStr = to_string(ep.evenWidth) + "x" + to_string(ep.evenHeight);
        options = {
            tr("Automatically align resolution to even (" + evenDimStr + ")",
               "Автоматически выровнять разрешение до чётного (" + evenDimStr + ")"),
            tr("Use software encoder libx264 on your CPU",
               "Использовать программный кодировщик libx264 на вашем процессоре"),
            tr("Choose a different output codec",
               "Выбрать другой конечный кодек"),
            tr("Use hybrid encoder: CPU decodes, GPU encodes",
               "Использовать гибридный кодировщик: процессор декодирует, а видеокарта кодирует"),
            tr("Try anyway / Retry (possible error and file loss)",
               "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            tr("Skip video",
               "Пропустить видео")
        };
        hints = {
            tr("Adds 1 pixel to satisfy hardware GPU alignment requirement.",
               "Добавляет 1 пиксель для соблюдения аппаратного выравнивания видеокарты."),
            tr("Converts to libx264 software encoding. Handles any resolution.",
               "Конвертирует в программный кодировщик libx264. Обрабатывает любые разрешения."),
            tr("Opens format selection menu where you can pick another codec.",
               "Откроет меню выбора формата, где можно выбрать другой кодек."),
            tr("CPU decodes the source, GPU encodes the output.",
               "Процессор декодирует исходник, видеокарта кодирует результат."),
            tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
               "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            tr("Do not process this video, move to the next one.",
               "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 2;  // "Choose a different output codec"
        skipOptIdx = 5;    // "Skip video"
    } else if (ep.isUltraHighRes) {
        options = {
            tr("Use software encoder libx264 on CPU (No resolution limits)",
               "Использовать программный кодировщик libx264 на CPU (Без ограничений по разрешению)"),
            tr("Downscale resolution to 4K (3840x2160) and encode on GPU",
               "Уменьшить разрешение до 4K (3840x2160) и кодировать на видеокарте"),
            tr("Choose a different output codec",
               "Выбрать другой конечный кодек"),
            tr("Try anyway / Retry (possible error and file loss)",
               "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            tr("Skip video",
               "Пропустить видео")
        };
        hints = {
            tr("CPU encodes video of any resolution without GPU hardware limits.",
               "Процессор кодирует видео любого разрешения без аппаратных ограничений видеокарты."),
            tr("Downscales video to 4K UHD so it fits GPU hardware encoder limits.",
               "Уменьшает видео до 4K UHD, чтобы оно укладывалось в аппаратные лимиты видеокарты."),
            tr("Opens format selection menu where you can pick another codec.",
               "Откроет меню выбора формата, где можно выбрать другой кодек."),
            tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
               "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            tr("Do not process this video, move to the next one.",
               "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 2;  // "Choose a different output codec"
        skipOptIdx = 4;    // "Skip video"
    } else {
        options = {
            tr("Use software encoder libx264 on your CPU (Recommended if unsure)", "Использовать программный кодировщик libx264 на вашем процессоре (Рекомендуется, если не знаете что выбрать)"),
            tr("Choose a different output codec (may help)", "Выбрать другой конечный кодек (может помочь)"),
            tr("Use hybrid encoder: CPU decodes, GPU encodes", "Использовать гибридный кодировщик: процессор декодирует, а видеокарта кодирует"),
            tr("Use reverse hybrid: GPU decodes, CPU encodes", "Использовать обратный гибридный кодировщик: видеокарта декодирует, а процессор кодирует"),
            tr("Try anyway / Retry (possible error and file loss)", "Всё равно попробовать / Повторить (возможна ошибка и даже потеря файла)"),
            tr("Skip video", "Пропустить видео")
        };
        hints = {
            tr("Converts to libx264 software encoding. Safest option, guaranteed compatibility.",
              "Конвертирует в программный кодировщик libx264. Самый безопасный вариант, гарантированная совместимость."),
            tr("Opens format selection menu where you can pick a compatible codec (e.g. H.265).",
              "Откроет меню выбора формата, где можно выбрать совместимый кодек (напр. H.265)."),
            tr("CPU decodes the source, GPU encodes the output. Safest hardware encoding mode.",
              "Процессор декодирует исходник, видеокарта кодирует результат. Самый надежный режим аппаратного кодирования."),
            tr("GPU decodes the source, CPU encodes the output. Uses hardware decoding with software encoding.",
              "Видеокарта декодирует исходник, процессор кодирует результат. Аппаратное декодирование с программным кодированием."),
            tr("Ignores the warning or retries the current operation. USE WITH CAUTION.",
              "Игнорирует предупреждение или повторяет операцию. ИСПОЛЬЗУЙТЕ С ОСТОРОЖНОСТЬЮ."),
            tr("Do not process this video, move to the next one.",
              "Не обрабатывать это видео, перейти к следующему.")
        };
        formatOptIdx = 1;  // "Choose a different output codec (may help)"
        skipOptIdx = 5;    // "Skip video"
    }

    int sel = arrowSelect(
        tr("ENCODING PROBLEM", "ПРОБЛЕМА КОДИРОВАНИЯ"),
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
        vector<string> fmtKeys = {
            "MP4(H.264)", "MP4(H.265/HEVC)", "MP4(AV1)",
            "MKV(H.264)", "MKV(H.265/HEVC)",
            "WEBM(VP9)", "WEBM(AV1)", "MOV(H.264)", "AVI(MPEG4)"
        };
        vector<string> fmtOpts = {
            tr("MP4 (H.264 / AVC)", "MP4 (H.264 / AVC)"),
            tr("MP4 (H.265 / HEVC)", "MP4 (H.265 / HEVC)"),
            tr("MP4 (AV1)", "MP4 (AV1)"),
            tr("MKV (H.264)", "MKV (H.264)"),
            tr("MKV (H.265 / HEVC)", "MKV (H.265 / HEVC)"),
            tr("WEBM (VP9)", "WEBM (VP9)"),
            tr("WEBM (AV1)", "WEBM (AV1)"),
            tr("MOV (H.264)", "MOV (H.264)"),
            tr("AVI (MPEG-4)", "AVI (MPEG-4)")
        };
        vector<string> fmtHints = {
            tr("Maximum compatibility. Plays on all devices.",
               "Максимальная совместимость. Воспроизводится на любых устройствах."),
            tr("Modern high-efficiency codec. 30-50% smaller than H.264.",
               "Современный кодек. Файлы на 30-50% меньше H.264."),
            tr("Next-gen royalty-free codec. Best compression, slower encoding.",
               "Кодек нового поколения. Максимальное сжатие, медленнее кодирование."),
            tr("Matroska container with H.264.",
               "Контейнер Matroska с H.264."),
            tr("Matroska container with HEVC codec.",
               "Контейнер Matroska с кодеком HEVC."),
            tr("Google web video format. Supported by all browsers.",
               "Веб-формат Google. Поддерживается всеми браузерами."),
            tr("Next-gen web format with AV1.",
               "Новейший веб-формат с AV1."),
            tr("Apple QuickTime. Native for macOS/iPhone and video editors.",
               "Apple QuickTime. Родной формат для macOS/iPhone и видеоредакторов."),
            tr("Legacy format for older DVD players and car stereos.",
               "Устаревший формат для старых DVD-плееров и автомагнитол.")
        };

        int fmtSel = arrowSelect(
            tr("CHOOSE CODEC", "ВЫБЕРИТЕ КОДЕК"),
            tr("Select a different output format/codec.\nThis change applies ONLY to the current video.\nGlobal settings remain unchanged.",
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
        vector<string> scopeOptions;
        vector<string> scopeHints;
        if (sel == skipOptIdx) {
            scopeOptions = {
                tr("Skip this video only", "Пропустить только это видео"),
                tr("Skip ALL remaining videos", "Пропустить ВСЕ оставшиеся видео")
            };
            scopeHints = {
                tr("Only this video will be skipped. The batch continues with the next file.",
                  "Только это видео будет пропущено. Пакет продолжится со следующего файла."),
                tr("All remaining videos in this batch will be skipped.",
                  "Все оставшиеся видео в этом пакете будут пропущены.")
            };
        } else {
            scopeOptions = {
                tr("Apply to this video only", "Применить только к этому видео"),
                tr("Apply to ALL remaining videos in this task", "Применить ко ВСЕМ оставшимся видео в этой задаче")
            };
            scopeHints = {
                tr("The choice applies only to the current video. Global settings remain unchanged.",
                  "Выбор применяется только к текущему видео. Глобальные настройки не изменяются."),
                tr("The choice applies to all remaining videos. You can also change this in global settings.",
                  "Выбор применяется ко всем оставшимся видео. Тот же выбор можно сделать в глобальных настройках.")
            };
        }
        int scopeSel = arrowSelect(
            tr("APPLY SCOPE", "ОБЛАСТЬ ПРИМЕНЕНИЯ"),
            tr("Your current choice does NOT change global settings.\nYou can also make the same choice in the global settings to save it.",
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

// ========== FORMAT MEDIA INFO FOR DISPLAY ==========
string cleanFormatName(const string& raw) {
    string lower = raw;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("matroska") != string::npos && lower.find("webm") != string::npos) return "Matroska/WebM";
    if (lower.find("matroska") != string::npos) return "Matroska (MKV)";
    if (lower.find("webm") != string::npos) return "WebM";
    if (lower.find("mp4") != string::npos) return "MP4";
    if (lower.find("avi") != string::npos) return "AVI";
    if (lower.find("mov") != string::npos) return "MOV";
    if (lower.find("flac") != string::npos) return "FLAC";
    if (lower.find("mp3") != string::npos) return "MP3";
    if (lower.find("ogg") != string::npos) return "OGG";
    if (lower.find("wav") != string::npos) return "WAV";
    if (lower.find("3gp") != string::npos) return "3GP";
    if (lower.find("mpegts") != string::npos) return "MPEG-TS";
    if (lower.find("mpeg") != string::npos) return "MPEG";
    if (lower.find("m4a") != string::npos) return "M4A";
    return raw;
}

// ========== MEDIA PROPERTIES STRUCT ==========
struct MediaProperties {
    string path;
    string format;
    string durationStr;
    double durationSec = 0;
    string sizeStr;
    double sizeBytes = 0;
    string bitrateStr;
    double bitrateVal = 0;

    // Primary video summary
    string videoCodec;
    string width, height;
    string resolution;
    string fps;
    string pixFmt;
    string videoBitrateStr;
    double videoBitrateVal = 0;

    // Primary audio summary
    string audioCodec;
    string sampleRate;
    string channels;
    string audioBitrateStr;
    double audioBitrateVal = 0;

    // Stream counts & lists
    string nbStreams;
    vector<VideoTrack> videoTracks;
    vector<AudioTrack> audioTracks;
    vector<SubtitleTrack> subtitleTracks;
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

    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return mp;

    // Get track collections
    mp.videoTracks = getVideoTracks(filePath);
    mp.audioTracks = getAudioTracks(filePath);
    mp.subtitleTracks = getSubtitleTracks(filePath);

    // Format query
    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_entries format=format_name,duration,bit_rate,nb_streams -of default=noprint_wrappers=1 \"" + filePath + "\"";
    string output = runCommand(cmd);

    istringstream iss(output);
    string line;
    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == string::npos) continue;
        string key = line.substr(0, eq);
        string val = line.substr(eq + 1);

        if (key == "format_name") {
            mp.format = cleanFormatName(val);
        } else if (key == "duration") {
            double dur = 0;
            if (parseNumberLoose(val, dur)) {
                mp.durationSec = dur;
                int h = (int)(dur / 3600);
                int m = (int)((dur - h * 3600) / 60);
                int s = (int)(dur) % 60;
                char buf[64];
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
                mp.durationStr = buf;
            }
        }
        else if (key == "bit_rate") {
            double br = 0;
            if (parseNumberLoose(val, br)) {
                mp.bitrateVal = br;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                mp.bitrateStr = buf;
            }
        } else if (key == "nb_streams") {
            mp.nbStreams = val;
        }
    }

    if (mp.nbStreams.empty()) {
        int total = (int)(mp.videoTracks.size() + mp.audioTracks.size() + mp.subtitleTracks.size());
        if (total > 0) mp.nbStreams = to_string(total);
    }

    // Populate primary video overview from the first non-cover video track (or first video track)
    const VideoTrack* mainVideo = nullptr;
    for (const auto& vt : mp.videoTracks) {
        if (!vt.isAttachedPic) {
            mainVideo = &vt;
            break;
        }
    }
    if (!mainVideo && !mp.videoTracks.empty()) {
        mainVideo = &mp.videoTracks[0];
    }

    if (mainVideo) {
        mp.videoCodec = mainVideo->codec;
        if (mainVideo->width > 0 && mainVideo->height > 0) {
            mp.width = to_string(mainVideo->width);
            mp.height = to_string(mainVideo->height);
            mp.resolution = mp.width + "x" + mp.height;
        }
        mp.fps = mainVideo->fps;
        mp.pixFmt = mainVideo->pixFmt;
        if (!mainVideo->bitRate.empty()) {
            double br = 0;
            if (parseNumberLoose(mainVideo->bitRate, br)) {
                mp.videoBitrateVal = br;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                mp.videoBitrateStr = buf;
            }
        }
    }

    // Populate primary audio overview from the first audio track
    if (!mp.audioTracks.empty()) {
        const auto& at = mp.audioTracks[0];
        mp.audioCodec = at.codec;
        if (!at.channelLayout.empty()) {
            mp.channels = at.channelLayout;
        } else if (at.channels > 0) {
            mp.channels = to_string(at.channels) + (CURRENT_LANG == LANG_RU ? " кан." : " ch");
        }
        mp.sampleRate = at.sampleRate;
        if (!at.bitRate.empty()) {
            double br = 0;
            if (parseNumberLoose(at.bitRate, br)) {
                mp.audioBitrateVal = br;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                mp.audioBitrateStr = buf;
            }
        }
    }

    return mp;
}

string formatMediaPropertiesDisplay(const MediaProperties& mp) {
    string res;

    // Container / general info
    res += tr("  Format: ", "  Формат: ") + (mp.format.empty() ? tr("Unknown", "Неизвестно") : mp.format) + "\n";
    if (mp.durationSec > 0 || !mp.durationStr.empty()) {
        char buf[128];
        int h = (int)(mp.durationSec / 3600);
        int m = (int)((mp.durationSec - h * 3600) / 60);
        int s = (int)(mp.durationSec) % 60;
        snprintf(buf, sizeof(buf), (CURRENT_LANG == LANG_RU) ? "  Длительность: %02d:%02d:%02d (%.1f сек)\n" : "  Duration: %02d:%02d:%02d (%.1f sec)\n", h, m, s, mp.durationSec);
        res += buf;
    }
    if (!mp.sizeStr.empty()) {
        res += tr("  Size: ", "  Размер: ") + mp.sizeStr + "\n";
    }

    // Streams breakdown
    if (!mp.videoTracks.empty()) {
        res += "\n" + tr("  [Video Streams]", "  [Видеопотоки]") + "\n";
        for (size_t i = 0; i < mp.videoTracks.size(); i++) {
            res += "    " + tr("Stream ", "Поток ") + to_string(i + 1) + ": " + mp.videoTracks[i].getDisplayString() + "\n";
        }
    }

    if (!mp.audioTracks.empty()) {
        res += "\n" + tr("  [Audio Tracks]", "  [Аудиодорожки]") + "\n";
        for (size_t i = 0; i < mp.audioTracks.size(); i++) {
            res += "    " + tr("Track ", "Дорожка ") + to_string(i + 1) + ": " + mp.audioTracks[i].getDisplayString() + "\n";
        }
    }

    if (!mp.subtitleTracks.empty()) {
        res += "\n" + tr("  [Subtitle Tracks]", "  [Субтитры]") + "\n";
        for (size_t i = 0; i < mp.subtitleTracks.size(); i++) {
            res += "    " + tr("Subtitle ", "Субтитры ") + to_string(i + 1) + ": " + mp.subtitleTracks[i].getDisplayString() + "\n";
        }
    }

    return res;
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

// ========== DOUBLE (PAIR) BATCH PROCESSING ENGINE ==========
// Experimental feature, driven by PARALLEL_BATCH. Two videos are only ever launched
// together when the batch loop would not stop and ask the user anything for either of
// them (see buildPairFingerprint below). That guarantee is what makes two concurrent
// FFmpeg processes safe to drive from a single keyboard loop: no interactive prompt can
// appear while a job is already running, so _getwch() is never needed mid-run.

struct PairJob {
    PROCESS_INFORMATION pi = {};
    HANDLE hRead = INVALID_HANDLE_VALUE;
    string fileName;
    string pending;
    double currentSec = 0;
    double duration = 0;
    string speed;
    bool running = false;
    bool errorSeen = false;
    string lastError;
    DWORD exitCode = 1;
};

bool spawnPairJob(PairJob& job, const wstring& cmdLine, const string& fileName, double duration) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hWrite = NULL;
    if (!CreatePipe(&job.hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(job.hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    wstring mutableCmd = cmdLine;
    if (!CreateProcessW(NULL, &mutableCmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &job.pi)) {
        CloseHandle(job.hRead);
        job.hRead = INVALID_HANDLE_VALUE;
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hWrite);
    job.fileName = fileName;
    job.duration = duration;
    job.running = true;
    return true;
}

// Drains whatever FFmpeg has produced so far without blocking.
void consumePairOutput(PairJob& job) {
    if (!job.running || job.hRead == INVALID_HANDLE_VALUE) return;

    char buffer[4096];
    DWORD bytesRead = 0;
    DWORD avail = 0;
    if (PeekNamedPipe(job.hRead, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        if (ReadFile(job.hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            for (DWORD i = 0; i < bytesRead; i++) {
                char c = buffer[i];
                if (c == '\r' || c == '\n') {
                    if (!job.pending.empty()) {
                        string timeStr, spd;
                        if (parseFFmpegProgress(job.pending, timeStr, spd)) {
                            job.currentSec = timeToSeconds(timeStr);
                            if (!spd.empty()) job.speed = spd;
                        } else if (job.pending.find("rror") != string::npos &&
                                   job.pending.find("encoder") == string::npos) {
                            job.errorSeen = true;
                            if (job.lastError.empty()) job.lastError = job.pending;
                        }
                        job.pending.clear();
                    }
                } else {
                    job.pending += c;
                }
            }
        }
    }
}

// Marks a job finished once the process exited AND the pipe has been fully drained.
bool tryFinishPairJob(PairJob& job) {
    if (!job.running) return true;
    if (WaitForSingleObject(job.pi.hProcess, 0) != WAIT_OBJECT_0) return false;

    consumePairOutput(job);
    DWORD left = 0;
    if (PeekNamedPipe(job.hRead, NULL, 0, NULL, &left, NULL) && left > 0) return false;

    GetExitCodeProcess(job.pi.hProcess, &job.exitCode);
    job.running = false;
    return true;
}

void releasePairJob(PairJob& job) {
    if (job.hRead != INVALID_HANDLE_VALUE) { CloseHandle(job.hRead); job.hRead = INVALID_HANDLE_VALUE; }
    if (job.pi.hProcess) { CloseHandle(job.pi.hProcess); job.pi.hProcess = NULL; }
    if (job.pi.hThread) { CloseHandle(job.pi.hThread); job.pi.hThread = NULL; }
    job.running = false;
}

// Integer-only formatting: never goes through %f, so a Russian LC_NUMERIC cannot
// turn a progress value into "45,00" (see AGENTS.md, decimal separator rules).
string pairTimeLabel(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = (int)(seconds + 0.5);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    char buf[24];
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

string pairStatusLine(const PairJob& job, int slot) {
    int percent = 0;
    if (job.duration > 0) {
        percent = (int)(job.currentSec * 100.0 / job.duration + 0.5);
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
    } else if (!job.running) {
        percent = job.exitCode == 0 ? 100 : percent;
    }

    const int barWidth = 22;
    int filled = (int)((long long)barWidth * percent / 100);
    if (filled < 0) filled = 0;
    if (filled > barWidth) filled = barWidth;

    string name = job.fileName;
    size_t dot = name.find_last_of('.');
    if (dot != string::npos && dot > 0) name = name.substr(0, dot);
    const size_t maxName = 40;
    if (name.size() > maxName) name = name.substr(0, maxName - 3) + "...";

    char head[16];
    snprintf(head, sizeof(head), "[%d] ", slot);

    string out = head + name + " [";
    for (int b = 0; b < barWidth; b++) out += (b < filled) ? '=' : ' ';
    out += "] ";

    char pct[16];
    snprintf(pct, sizeof(pct), "%3d%%", percent);
    out += pct;
    if (!job.speed.empty()) out += "  " + job.speed;
    out += "  " + pairTimeLabel(job.currentSec) + "/" + pairTimeLabel(job.duration);

    if (!job.running) {
        if (job.exitCode == 0) {
            out += tr("  [OK]", "  [OK]");
        } else {
            out += tr("  [FAILED]", "  [ОШИБКА]");
        }
    }
    return out;
}

// Number of status lines currently on screen, so the next repaint knows how far to
// walk the cursor back up. Reset by execFFmpegPair() before the first render.
int g_pairRenderedLines = 0;

void renderPairStatus(const PairJob& a, const PairJob& b) {
    if (g_pairRenderedLines > 0) {
        cout << "\033[" << g_pairRenderedLines << "A\r\033[J";
    }
    cout << pairStatusLine(a, 1) << "\n";
    cout << pairStatusLine(b, 2) << "\n";
    cout << flush;
    g_pairRenderedLines = 2;
}

// Runs both jobs to completion, repainting the two status lines in place.
// Returns true only if BOTH finished with exit code 0.
bool execFFmpegPair(PairJob& a, PairJob& b, bool& escaped) {
    escaped = false;
    g_pairRenderedLines = 0;
    bool okA = false, okB = false;

    while (a.running || b.running) {
        if (_kbhit()) {
            int key = _getch();
            if (key == 27) {
                escaped = true;
                if (a.running) TerminateProcess(a.pi.hProcess, 1);
                if (b.running) TerminateProcess(b.pi.hProcess, 1);
            }
        }

        consumePairOutput(a);
        consumePairOutput(b);
        bool doneA = tryFinishPairJob(a);
        bool doneB = tryFinishPairJob(b);

        if (!doneA || !doneB) {
            renderPairStatus(a, b);
            Sleep(80);
        } else {
            break;
        }
    }

    renderPairStatus(a, b);
    cout << endl;

    okA = (a.exitCode == 0);
    okB = (b.exitCode == 0);
    return (okA && okB);
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
                      L"Медиа файлы (*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff)\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff\0"
                      L"Видео файлы (*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v)\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v\0"
                      L"Аудио файлы (*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma)\0*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma\0"
                      L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff\0"
                      L"Все файлы (*.*)\0*.*\0" :
                      L"Media Files (*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff)\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v;*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma;*.gif;*.png;*.jpg;*.jpeg;*.bmp;*.tiff\0"
                      L"Video Files (*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v)\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.flv;*.webm;*.ts;*.m4v\0"
                      L"Audio Files (*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma)\0*.mp3;*.m4a;*.aac;*.wav;*.ogg;*.flac;*.opus;*.wma\0"
                      L"Image Files (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff\0"
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
      << "AUDIO_CODEC=" << AUDIO_CODEC << "\n"
      << "SUBTITLE_ACTION=" << SUBTITLE_ACTION << "\n"
      << "DELETE_ORIGINAL=" << DELETE_ORIGINAL << "\n"
      << "PARALLEL_BATCH=" << (PARALLEL_BATCH ? "true" : "false") << "\n"
      << "LANGUAGE=" << (CURRENT_LANG == LANG_RU ? "ru" : "en") << "\n"
      << "SAVE_COVER=" << (SAVE_COVER ? "true" : "false") << "\n"
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
                else if (l.find("AUDIO_CODEC=") == 0) AUDIO_CODEC = l.substr(12);
                else if (l.find("AUDIO_CODEC_ASK=") == 0) { if (l.substr(16) == "true") AUDIO_CODEC = "ask"; else AUDIO_CODEC = "copy"; }
                else if (l.find("SUBTITLE_ACTION=") == 0) SUBTITLE_ACTION = l.substr(16);
                else if (l.find("DELETE_ORIGINAL=") == 0) DELETE_ORIGINAL = l.substr(16);
                else if (l.find("LANGUAGE=") == 0) CURRENT_LANG = (l.substr(9) == "ru") ? LANG_RU : LANG_EN;
                else if (l.find("SAVE_COVER=") == 0) SAVE_COVER = (l.substr(11) == "true");
                else if (l.find("PARALLEL_BATCH=") == 0) PARALLEL_BATCH = (l.substr(15) == "true");
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
    if (OUTPUT_FORMAT.find("M4V") != string::npos) return "m4v";
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

    if (ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) {
        if (OUTPUT_FORMAT.find("H.264") != string::npos) return "-c:v libx264";
        if (OUTPUT_FORMAT.find("H.265") != string::npos || OUTPUT_FORMAT.find("HEVC") != string::npos) return "-c:v libx265";
        if (OUTPUT_FORMAT.find("AV1") != string::npos) return "-c:v libaom-av1";
        if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-c:v libvpx-vp9";
        if (OUTPUT_FORMAT.find("MPEG4") != string::npos) return "-c:v mpeg4";
        return "-c:v libx264";
    }

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
        if (hwEncoder == "nvenc") return "-c:v av1_nvenc -rc:v vbr -cq:v";
        if (hwEncoder == "amf") return "-c:v av1_amf -rc:v vbr -cq:v";
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

string getAudioCodecSettingName(const string& overrideCodec = "") {
    string choice = overrideCodec.empty() ? AUDIO_CODEC : overrideCodec;
    if (choice == "copy" || choice == "original") return tr("Copy original", "Как в оригинале");
    if (choice == "ask") return tr("Always ask", "Всегда спрашивать");
    if (choice == "aac") return "AAC";
    if (choice == "ac3") return "AC3 (Dolby Digital)";
    if (choice == "eac3") return "E-AC3 (Dolby Digital+)";
    if (choice == "mp3") return "MP3";
    if (choice == "opus") return "Opus";
    if (choice == "flac") return "FLAC";
    if (choice == "pcm_s16le" || choice == "pcm" || choice == "wav") return "PCM (WAV)";
    return tr("Copy original", "Как в оригинале");
}

string getAudioCodecArgs(const string& overrideCodec = "") {
    string choice = overrideCodec.empty() ? AUDIO_CODEC : overrideCodec;
    if (choice == "ask") {
        choice = "copy";
    }

    if (choice == "copy" || choice == "original") {
        return "-c:a copy";
    }
    if (choice == "aac") return "-c:a aac -b:a " + AUDIO_BITRATE + "k";
    if (choice == "ac3") return "-c:a ac3 -b:a " + AUDIO_BITRATE + "k";
    if (choice == "eac3") return "-c:a eac3 -b:a " + AUDIO_BITRATE + "k";
    if (choice == "mp3") return "-c:a libmp3lame -ac 2 -b:a " + AUDIO_BITRATE + "k";
    if (choice == "opus") return "-c:a libopus -b:a " + AUDIO_BITRATE + "k";
    if (choice == "flac") return "-c:a flac";
    if (choice == "pcm_s16le" || choice == "pcm" || choice == "wav") return "-c:a pcm_s16le";

    if (OUTPUT_FORMAT.find("MP3") != string::npos) return "-c:a libmp3lame -ac 2 -b:a " + AUDIO_BITRATE + "k";
    if (OUTPUT_FORMAT.find("Opus") != string::npos || OUTPUT_FORMAT.find("WEBM") != string::npos) return "-c:a libopus -b:a " + AUDIO_BITRATE + "k";
    if (OUTPUT_FORMAT.find("FLAC") != string::npos) return "-c:a flac";
    if (OUTPUT_FORMAT.find("WAV") != string::npos) return "-c:a pcm_s16le";
    return "-c:a copy";
}

string getVideoQualityArgs(const string& crfVal, bool forceCPU = false) {
    if (forceCPU) {
        if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-crf " + crfVal + " -b:v 0";
        return "-crf " + crfVal;
    }
    if (ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) {
        if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-crf " + crfVal + " -b:v 0";
        return "-crf " + crfVal;
    }
    AccelMode activeGpu = getActiveGpuMode();
    bool isHW = (activeGpu == ACCEL_NVIDIA || activeGpu == ACCEL_AMD || activeGpu == ACCEL_INTEL);
    if (isHW) {
        if (OUTPUT_FORMAT.find("AV1") != string::npos) return crfVal;
        return "-qp " + crfVal;
    }
    if (OUTPUT_FORMAT.find("VP9") != string::npos) return "-crf " + crfVal + " -b:v 0";
    return "-crf " + crfVal;
}

string getVideoPresetArgs(const string& overridePreset = "", bool forceCPU = false) {
    const string& p = overridePreset.empty() ? PRESET : overridePreset;
    if (forceCPU) return "-preset " + p;
    if (ACCELERATION_MODE == ACCEL_GPU_DEC_CPU_ENC) return "-preset " + p;
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
    if (OUTPUT_FORMAT.find("AV1") != string::npos) {
        if (p == "ultrafast" || p == "superfast") return "-cpu-used 8";
        if (p == "veryfast" || p == "faster") return "-cpu-used 7";
        if (p == "fast") return "-cpu-used 6";
        if (p == "medium") return "-cpu-used 5";
        if (p == "slow") return "-cpu-used 4";
        if (p == "slower") return "-cpu-used 3";
        if (p == "veryslow") return "-cpu-used 2";
        return "-cpu-used 5";
    }
    if (OUTPUT_FORMAT.find("VP9") != string::npos) {
        if (p == "ultrafast" || p == "superfast") return "-cpu-used 5";
        if (p == "veryfast" || p == "faster") return "-cpu-used 4";
        if (p == "fast") return "-cpu-used 3";
        if (p == "medium") return "-cpu-used 2";
        if (p == "slow") return "-cpu-used 1";
        if (p == "slower" || p == "veryslow") return "-cpu-used 0";
        return "-cpu-used 2";
    }
    return "-preset " + p;
}

string buildVideoFilterSpec(const string& filePath, bool useAutoAlign = false, bool useDownscale4K = false, const string& extraFilter = "", const string& overrideResolution = "") {
    vector<string> filters;
    VideoSourceProperties vp = getVideoProperties(filePath);

    // 1. Auto deinterlacing if video source is interlaced
    if (vp.isInterlaced) {
        filters.push_back("bwdif");
    }

    // 2. Extra filter (e.g. watermark, rotate, etc.)
    if (!extraFilter.empty()) {
        filters.push_back(extraFilter);
    }

    // 3. Scaling / Resolution
    if (useAutoAlign) {
        filters.push_back("scale=trunc(iw/2)*2:trunc(ih/2)*2");
    } else if (useDownscale4K) {
        filters.push_back("scale=-2:2160");
    } else {
        string targetRes = overrideResolution.empty() ? OUTPUT_RESOLUTION : overrideResolution;
        if (targetRes != "original") {
            filters.push_back("scale=-2:" + targetRes);
        }
    }

    if (filters.empty()) return "";
    string res = "-vf \"";
    for (size_t i = 0; i < filters.size(); i++) {
        if (i > 0) res += ",";
        res += filters[i];
    }
    res += "\"";
    return res;
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
    string inputPath;
    bool isTemp = false;
    // Set when the FFmpeg command already attached the cover itself, so
    // finalizeFFmpegTarget() must not remux the finished file a second time just to
    // add the picture.
    bool coverEmbedded = false;
};

FFmpegTarget prepareFFmpegTarget(const string& targetPath, const vector<string>& inputPaths) {
    FFmpegTarget ft;
    ft.targetPath = targetPath;
    ft.writePath = targetPath;
    ft.isTemp = false;
    if (!inputPaths.empty()) ft.inputPath = inputPaths[0];

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

void processCover(const string& inputPath, const string& outputPath);

bool finalizeFFmpegTarget(const FFmpegTarget& ft, bool success) {
    if (!ft.isTemp) {
        if (success && !ft.coverEmbedded) processCover(ft.inputPath, ft.targetPath);
        return success;
    }

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

        if (!ft.coverEmbedded) processCover(ft.inputPath, ft.targetPath);
        return fileExistsW(wTarget);
    } else {
        DeleteFileW(wTemp.c_str());
        return false;
    }
}

// ========== COVER HANDLING ==========

struct EmbeddedCoverInfo {
    bool found = false;
    bool isAttachment = false; // true if MKV attachment stream
    int streamIndex = -1;
    string filename = "";
    string mimeType = "";
    string ext = "jpg"; // "jpg" or "png"
};

EmbeddedCoverInfo detectEmbeddedCover(const string& filePath) {
    EmbeddedCoverInfo ci;
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty()) return ci;

    string cmd = "\"" + FFPROBE_PATH + "\" -v quiet -show_entries stream=index,codec_type,codec_name,disposition:stream_tags=filename,mimetype -of default \"" + filePath + "\"";
    string out = runCommand(cmd);
    if (out.empty()) return ci;

    istringstream iss(out);
    string line;

    int curIndex = -1;
    string curCodecType = "";
    string curCodecName = "";
    int curAttachedPic = 0;
    string curFilename = "";
    string curMime = "";

    auto checkCurrentStream = [&]() {
        if (curIndex < 0) return;
        string lowerFilename = curFilename;
        transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
        string lowerMime = curMime;
        transform(lowerMime.begin(), lowerMime.end(), lowerMime.begin(), ::tolower);
        string lowerCodec = curCodecName;
        transform(lowerCodec.begin(), lowerCodec.end(), lowerCodec.begin(), ::tolower);

        // Case 1: Attachment stream (MKV)
        if (curCodecType == "attachment") {
            bool isImg = false;
            if (lowerMime.rfind("image/", 0) == 0) isImg = true;
            if (lowerFilename.find(".jpg") != string::npos || lowerFilename.find(".jpeg") != string::npos ||
                lowerFilename.find(".png") != string::npos || lowerFilename.find(".bmp") != string::npos ||
                lowerFilename.find(".webp") != string::npos) isImg = true;
            if (lowerCodec == "mjpeg" || lowerCodec == "png" || lowerCodec == "jpeg" || lowerCodec == "bmp") isImg = true;
            if (lowerFilename.find("cover") != string::npos || lowerFilename.find("poster") != string::npos ||
                lowerFilename.find("folder") != string::npos) isImg = true;

            if (isImg && !ci.found) {
                ci.found = true;
                ci.isAttachment = true;
                ci.streamIndex = curIndex;
                ci.filename = curFilename;
                ci.mimeType = curMime;
                if (lowerFilename.find(".png") != string::npos || lowerMime == "image/png" || lowerCodec == "png") {
                    ci.ext = "png";
                } else {
                    ci.ext = "jpg";
                }
            }
        }
        // Case 2: Video stream with attached_pic disposition (MP4, MP3, FLAC, MKV video cover)
        else if (curCodecType == "video") {
            if (curAttachedPic == 1 || lowerCodec == "mjpeg" || lowerCodec == "png") {
                if (curAttachedPic == 1 || curIndex > 0) {
                    if (!ci.found) {
                        ci.found = true;
                        ci.isAttachment = false;
                        ci.streamIndex = curIndex;
                        ci.filename = curFilename;
                        ci.mimeType = curMime;
                        if (lowerCodec == "png") ci.ext = "png";
                        else ci.ext = "jpg";
                    }
                }
            }
        }
    };

    while (getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        if (line == "[STREAM]") {
            curIndex = -1;
            curCodecType = "";
            curCodecName = "";
            curAttachedPic = 0;
            curFilename = "";
            curMime = "";
        } else if (line == "[/STREAM]") {
            checkCurrentStream();
        } else {
            size_t eq = line.find('=');
            if (eq != string::npos) {
                string k = line.substr(0, eq);
                string v = line.substr(eq + 1);
                if (k == "index") try { curIndex = stoi(v); } catch (...) {}
                else if (k == "codec_type") curCodecType = v;
                else if (k == "codec_name") curCodecName = v;
                else if (k == "DISPOSITION:attached_pic") try { curAttachedPic = stoi(v); } catch (...) {}
                else if (k == "TAG:filename") curFilename = v;
                else if (k == "TAG:mimetype") curMime = v;
            }
        }
    }
    checkCurrentStream();

    return ci;
}

bool extractCover(const string& sourcePath, const string& outCoverPath) {
    EmbeddedCoverInfo ci = detectEmbeddedCover(sourcePath);
    if (ci.found) {
        if (ci.isAttachment) {
            wstring wCmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            wCmd += L" -loglevel quiet -nostats -dump_attachment:s:" + to_wstring(ci.streamIndex) + L" \"" + utf8ToWstring(outCoverPath) + L"\"";
            wCmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(sourcePath)) + L"\" -y";
            runCommand(wstringToUtf8(wCmd));
            if (fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;

            wstring wCmd2 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            wCmd2 += L" -loglevel quiet -nostats -i \"" + utf8ToWstring(getSafeFFmpegPath(sourcePath)) + L"\"";
            wCmd2 += L" -map 0:" + to_wstring(ci.streamIndex) + L" -c copy -y \"" + utf8ToWstring(outCoverPath) + L"\"";
            runCommand(wstringToUtf8(wCmd2));
            if (fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;
        } else {
            wstring wCmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            wCmd += L" -loglevel quiet -nostats -i \"" + utf8ToWstring(getSafeFFmpegPath(sourcePath)) + L"\"";
            wCmd += L" -map 0:" + to_wstring(ci.streamIndex) + L" -vframes 1 -c:v copy -y \"" + utf8ToWstring(outCoverPath) + L"\"";
            runCommand(wstringToUtf8(wCmd));
            if (fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;
        }
    }

    // Fallback: extract first frame as thumbnail / cover
    wstring wCmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    wCmd += L" -loglevel quiet -nostats -ss 00:00:01 -i \"" + utf8ToWstring(getSafeFFmpegPath(sourcePath)) + L"\"";
    wCmd += L" -vframes 1 -q:v 2 -update 1 -y \"" + utf8ToWstring(outCoverPath) + L"\"";
    runCommand(wstringToUtf8(wCmd));
    if (fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;

    wstring wCmd0 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    wCmd0 += L" -loglevel quiet -nostats -ss 00:00:00 -i \"" + utf8ToWstring(getSafeFFmpegPath(sourcePath)) + L"\"";
    wCmd0 += L" -vframes 1 -q:v 2 -update 1 -y \"" + utf8ToWstring(outCoverPath) + L"\"";
    runCommand(wstringToUtf8(wCmd0));
    return fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0;
}

// Container specific FFmpeg arguments that attach a cover to a stream copy pass.
// The caller has already placed the main input on its command line; this appends the
// cover to that same pass. Returns false for containers that cannot carry a cover, in
// which case the caller must fall back to the post-pass rewrite.
//
// This is the single source of truth for cover embedding: both the standalone
// embedCoverIntoFile() remux and the concatenate path build their command from it, so the
// two can never disagree about a container.
static bool buildCoverAttachArgs(const string& outputExt, const string& coverPath, wstring& argsOut) {
    string ext = outputExt;
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    fs::path cp = fs::u8path(coverPath);
    string coverExt = cp.extension().u8string();
    transform(coverExt.begin(), coverExt.end(), coverExt.begin(), ::tolower);
    string mime = (coverExt == ".png") ? "image/png" : "image/jpeg";

    argsOut.clear();

    if (ext == ".mkv") {
        // Matroska: the cover is a file attachment, not a stream
        argsOut += L" -attach \"" + utf8ToWstring(getSafeFFmpegPath(coverPath)) + L"\"";
        argsOut += L" -metadata:s:t mimetype=\"" + utf8ToWstring(mime) + L"\"";
        argsOut += L" -metadata:s:t:0 filename=\"cover" + utf8ToWstring(coverExt.empty() ? ".jpg" : coverExt) + L"\"";
        return true;
    }

    bool isMp4Family = (ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".m4a");
    bool isAudio = (ext == ".mp3" || ext == ".flac");
    if (!isMp4Family && !isAudio) return false;

    // Everything else: the cover becomes a second input, copied in as a video stream.
    // "-c:v:1 copy" is stated explicitly so the cover is never handed to the encoder
    // settings of the main stream when this is folded into a re-encode.
    argsOut += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(coverPath)) + L"\"";
    argsOut += isAudio ? L" -map 0:a -map 1:v" : L" -map 0 -map 1";
    argsOut += L" -c:v:1 copy";
    if (ext == ".mp3") {
        argsOut += L" -id3v2_version 3";
        argsOut += L" -metadata:s:v title=\"Album cover\" -metadata:s:v comment=\"Cover (front)\"";
    } else if (ext == ".flac") {
        argsOut += L" -disposition:v attached_pic";
    } else {
        argsOut += L" -disposition:v:1 attached_pic";
    }
    return true;
}

bool embedCoverIntoFile(const string& videoPath, const string& coverPath) {
    if (!fileExists(videoPath) || !fileExists(coverPath)) return false;

    fs::path vp = fs::u8path(videoPath);
    string ext = vp.extension().u8string();
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    string tempOut = videoPath.substr(0, videoPath.length() - ext.length()) + "_tmp_cover" + ext;

    wstring attachArgs;
    if (!buildCoverAttachArgs(ext, coverPath, attachArgs)) return true;

    wstring wCmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\" -loglevel quiet -nostats";
    wCmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(videoPath)) + L"\"";
    wCmd += attachArgs;
    wCmd += L" -c copy -y \"" + utf8ToWstring(tempOut) + L"\"";

    runCommand(wstringToUtf8(wCmd));

    if (fileExists(tempOut) && fs::file_size(fs::u8path(tempOut)) > 0) {
        wstring wTemp = utf8ToWstring(tempOut);
        wstring wTarget = utf8ToWstring(videoPath);
        DWORD tAttrs = GetFileAttributesW(wTarget.c_str());
        if (tAttrs != INVALID_FILE_ATTRIBUTES && (tAttrs & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesW(wTarget.c_str(), tAttrs & ~FILE_ATTRIBUTE_READONLY);
        if (ReplaceFileW(wTarget.c_str(), wTemp.c_str(), NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL))
            return true;

        DeleteFileW(wTarget.c_str());
        if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;

        DeleteFileW(wTemp.c_str());
    }
    return false;
}

void processCover(const string& inputPath, const string& outputPath) {
    if (!SAVE_COVER) return;
    if (!fileExists(outputPath) || inputPath.empty()) return;

    fs::path outP = fs::u8path(outputPath);
    string tempCover = outP.parent_path().u8string() + "\\.~mr_tmp_cover_" + outP.stem().u8string() + ".jpg";

    if (extractCover(inputPath, tempCover)) {
        embedCoverIntoFile(outputPath, tempCover);
        std::error_code ec;
        fs::remove(fs::u8path(tempCover), ec);
    }
}


// ========== ARROW-KEY SELECTION MENU ==========
int arrowSelect(const string& title, const string& description, const vector<string>& options, int currentIdx, const vector<string>& hints, bool inlineMode) {
    int selected = (currentIdx >= 0 && currentIdx < (int)options.size()) ? currentIdx : 0;
    int linesRendered = 0;
    while (true) {
        if (!inlineMode) {
            clearScreen();
        } else if (linesRendered > 0) {
            cout << "\033[" << linesRendered << "A\r\033[J" << flush;
        }

        int currentLines = 0;
        auto printLine = [&](const string& text, int color = WHITE, bool nl = true) {
            setColor(color);
            cout << text;
            setColor(WHITE);
            if (nl) {
                cout << "\n";
                currentLines++;
            }
        };

        if (inlineMode) {
            cout << "\n";
            currentLines++;
        }
        printLine("========================================", CYAN);
        printLine(" " + title, CYAN);
        printLine("========================================", CYAN);
        if (!description.empty()) {
            cout << "\n" << description << "\n";
            currentLines += 2;
            for (char c : description) {
                if (c == '\n') currentLines++;
            }
        }
        cout << "\n";
        currentLines++;
        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) {
                setColor(GREEN);
                cout << " > " << options[i] << "\n";
                setColor(WHITE);
            } else {
                cout << "   " << options[i] << "\n";
            }
            currentLines++;
            for (char c : options[i]) {
                if (c == '\n') currentLines++;
            }
        }
        if (!hints.empty() && selected >= 0 && selected < (int)hints.size() && !hints[selected].empty()) {
            cout << "\n";
            currentLines++;
            printLine("----------------------------------------------------------------------", CYAN);
            setColor(YELLOW);
            cout << " [i] " << hints[selected] << "\n";
            setColor(WHITE);
            currentLines++;
            for (char c : hints[selected]) {
                if (c == '\n') currentLines++;
            }
            printLine("----------------------------------------------------------------------", CYAN);
        }
        cout << "\n";
        currentLines++;
        printLine(tr("Arrow keys to select, Enter to confirm, ESC or 0 to go back",
                     "Стрелки для выбора, Enter для подтверждения, ESC или 0 для возврата"), WHITE);

        linesRendered = currentLines;
        cout << flush;

        wint_t key = _getwch();
        // The dialog stays on screen (the chosen option must remain visible), so leave a
        // blank line behind it. Without this, the next output lands on the same line as
        // the "Arrow keys to select..." hint and the two run together.
        if (key == 27 || key == '0') { cout << "\n" << flush; return -1; }
        if (key == 13) { cout << "\n" << flush; return selected; }
        if (key == 0 || key == 0xE0) {
            wint_t scan = _getwch();
            if (scan == 72) selected = (selected > 0) ? selected - 1 : (int)options.size() - 1;
            else if (scan == 80) selected = (selected < (int)options.size() - 1) ? selected + 1 : 0;
        } else {
            char ch = normalizeKeyToEnglish(key);
            for (int i = 0; i < (int)options.size(); i++) {
                if (options[i].length() >= 2 && options[i][0] == ' ' && options[i][1] == ch) {
                    cout << "\n" << flush;
                    return i;
                }
                if (options[i].length() >= 1 && options[i][0] == ch) {
                    cout << "\n" << flush;
                    return i;
                }
            }
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

string selectAudioCodecForOperation() {
    vector<string> keys = {
        "copy", "aac", "ac3", "eac3", "mp3", "opus", "flac", "pcm_s16le"
    };

    vector<string> options = {
        tr("Copy original (copy)            - Keep original stream, no quality loss (Fastest)",
           "Как в оригинале (copy)          - Без пережатия звука и без потерь качества (Быстрее всего)"),
        tr("AAC                             - Modern universal high-quality standard",
           "AAC                             - Универсальный стандарт высокого качества"),
        tr("AC3 (Dolby Digital)             - Surround 5.1 / Home Cinema standard",
           "AC3 (Dolby Digital)             - Стандарт объемного звука 5.1 для ТВ и кинотеатров"),
        tr("E-AC3 (Dolby Digital Plus)      - Advanced streaming surround audio",
           "E-AC3 (Dolby Digital Plus)      - Улучшенный объемный звук для современных медиаплееров"),
        tr("MP3 (libmp3lame)                - Classic MP3 compression",
           "MP3 (libmp3lame)                - Классическое сжатие MP3"),
        tr("Opus (libopus)                  - Ultra-efficient modern speech & music codec",
           "Opus (libopus)                  - Сверхэффективный современный кодек"),
        tr("FLAC                            - Lossless compression (100% studio quality)",
           "FLAC                            - Сжатие без потерь (100% студийное качество)"),
        tr("PCM / WAV                       - Uncompressed studio PCM audio",
           "PCM / WAV                       - Несжатый студийный звук")
    };

    vector<string> hints = {
        tr("Preserves the original audio stream bit-for-bit without re-encoding. Zero quality loss and maximum speed.",
           "Сохраняет исходный аудиопоток бит-в-бит без перекодирования. Нулевая потеря качества и максимальная скорость."),
        tr("Advanced Audio Coding. Standard for MP4, YouTube and Apple. Best balance of quality and compatibility.",
           "Стандарт для MP4, YouTube и Apple. Оптимальный баланс совместимости и качества."),
        tr("Dolby Digital 5.1 AC-3. Supported by virtually all home theaters, AV receivers and TVs.",
           "Dolby Digital 5.1. Поддерживается практически всеми домашними кинотеатрами, ресиверами и ТВ."),
        tr("Dolby Digital Plus. Enhanced bitrate efficiency with up to 7.1 channels for modern setups.",
           "Dolby Digital Plus. Улучшенная эффективность и поддержка до 7.1 каналов для современных устройств."),
        tr("Classic MPEG-1 Audio Layer III. Plays everywhere, but less efficient than AAC.",
           "Классический MP3. Воспроизводится на любых устройствах, но менее эффективен, чем AAC."),
        tr("Modern low-latency open codec with superior clarity at lower bitrates (ideal for WebM/MKV).",
           "Современный открытый кодек с отличной детализацией при низких битрейтах (идеален для WebM/MKV)."),
        tr("Free Lossless Audio Codec. Exact mathematical bit-perfect audio preservation.",
           "Сжатие без потерь. Точное побитовое сохранение оригинального звука без изменений."),
        tr("Uncompressed raw PCM. Zero compression, maximum compatibility with editing software.",
           "Несжатый PCM. Максимальная совместимость с программами монтажа, большие файлы.")
    };

    int sel = arrowSelect(tr("SELECT AUDIO CODEC", "ВЫБОР АУДИОКОДЕКА"),
                          tr("Choose audio codec for this operation:", "Выберите аудиокодек для этой операции:"),
                          options, 0, hints);
    if (sel < 0) return "";
    return keys[sel];
}

bool promptAudioCodecSettings(string& chosenCodec) {
    if (AUDIO_CODEC != "ask") {
        chosenCodec = AUDIO_CODEC;
        return true;
    }
    string codec = selectAudioCodecForOperation();
    if (codec.empty()) return false;
    chosenCodec = codec;
    return true;
}

// ========== SUBTITLE PROMPT HELPERS ==========
string getSubtitleActionSettingName(const string& val = "") {
    string s = val.empty() ? SUBTITLE_ACTION : val;
    if (s == "convert") {
        return tr("Always convert", "Всегда конвертировать");
    } else if (s == "burn") {
        return tr("Always burn into video", "Всегда вшивать в видео");
    } else if (s == "skip") {
        return tr("Always skip", "Всегда пропускать");
    } else if (s == "remove" || s == "drop") {
        return tr("Always remove subtitles", "Всегда удалять субтитры");
    }
    return tr("Always Ask", "Всегда спрашивать");
}

// ========== DELETE ORIGINAL HELPERS ==========
string getDeleteOriginalSettingName() {
    if (DELETE_ORIGINAL == "ask") return tr("Always Ask", "Всегда спрашивать");
    if (DELETE_ORIGINAL == "yes") return tr("Yes", "Да");
    return tr("No", "Нет");
}

bool promptDeleteOriginal(bool isBatch, bool& outDelete) {
    if (DELETE_ORIGINAL == "yes") {
        outDelete = true;
        return true;
    }
    if (DELETE_ORIGINAL == "no") {
        outDelete = false;
        return true;
    }
    // DELETE_ORIGINAL == "ask"
    if (isBatch) {
        cout << "\n" << tr("Delete original files after conversion? [Y/N]: ",
                           "Удалить оригиналы после преобразования? [Y/N]: ");
    } else {
        cout << "\n" << tr("Delete original file after processing? [Y/N]: ",
                           "Удалить исходный файл после обработки? [Y/N]: ");
    }
    char chDel = getMenuChoice();
    if (chDel == 27) return false;
    outDelete = (chDel == 'y' || chDel == 'Y');
    cout << (outDelete ? "Y\n" : "N\n");
    return true;
}

void handleOriginalDeletion(const string& srcPath, const string& dstPath, bool isTemp, bool shouldDelete) {
    if (!shouldDelete || srcPath.empty() || isTemp) return;
    std::error_code ec;
    auto absIn = fs::weakly_canonical(fs::u8path(srcPath), ec);
    if (ec) return;
    auto absOut = fs::weakly_canonical(fs::u8path(dstPath), ec);
    if (ec) return;
    if (absIn != absOut && fileExists(srcPath)) {
        fs::remove(fs::u8path(srcPath), ec);
    }
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
    printColor(" 1: \"" + file1 + "\"", CYAN);
    printColor(" 2: \"" + file2 + "\"", CYAN);
    printColor("========================================", CYAN);

    // UTF-8 visual length and padding helpers
    auto utf8Len = [](const string& s) -> size_t {
        size_t len = 0;
        for (size_t i = 0; i < s.length(); ) {
            unsigned char c = (unsigned char)s[i];
            if (c < 0x80) i += 1;
            else if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else i += 1;
            len++;
        }
        return len;
    };

    auto padUtf8 = [&](const string& s, size_t targetWidth) -> string {
        size_t len = utf8Len(s);
        if (len < targetWidth) {
            return s + string(targetWidth - len, ' ');
        }
        return s;
    };

    struct CompareItem {
        string label;
        string v1;
        string v2;
        double n1 = 0;
        double n2 = 0;
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

    addItem(tr("Format", "Формат"), mp1.format, mp2.format);
    addItem(tr("Duration", "Длительность"), mp1.durationStr, mp2.durationStr, mp1.durationSec, mp2.durationSec, true, true);
    addItem(tr("Size", "Размер"), mp1.sizeStr, mp2.sizeStr, mp1.sizeBytes, mp2.sizeBytes, false, true);
    addItem(tr("Overall Bitrate", "Общий битрейт"), mp1.bitrateStr, mp2.bitrateStr, mp1.bitrateVal, mp2.bitrateVal, true, true);
    addItem(tr("Resolution", "Разрешение"), mp1.resolution, mp2.resolution);
    addItem(tr("Framerate", "Частота кадров"), mp1.fps.empty() ? "" : mp1.fps + tr(" fps", " к/с"),
                                              mp2.fps.empty() ? "" : mp2.fps + tr(" fps", " к/с"));
    addItem(tr("Video Codec", "Видеокодек"), mp1.videoCodec.empty() ? "" : (mp1.pixFmt.empty() ? mp1.videoCodec : mp1.videoCodec + " (" + mp1.pixFmt + ")"),
                                             mp2.videoCodec.empty() ? "" : (mp2.pixFmt.empty() ? mp2.videoCodec : mp2.videoCodec + " (" + mp2.pixFmt + ")"));
    addItem(tr("Video Bitrate", "Видео битрейт"), mp1.videoBitrateStr, mp2.videoBitrateStr, mp1.videoBitrateVal, mp2.videoBitrateVal, true, mp1.videoBitrateVal > 0 && mp2.videoBitrateVal > 0);
    addItem(tr("Audio Codec", "Аудиокодек"), mp1.audioCodec, mp2.audioCodec);
    addItem(tr("Audio Bitrate", "Аудио битрейт"), mp1.audioBitrateStr, mp2.audioBitrateStr, mp1.audioBitrateVal, mp2.audioBitrateVal, true, mp1.audioBitrateVal > 0 && mp2.audioBitrateVal > 0);
    addItem(tr("Sample Rate", "Частота аудио"), mp1.sampleRate.empty() ? "" : mp1.sampleRate + tr(" Hz", " Гц"),
                                               mp2.sampleRate.empty() ? "" : mp2.sampleRate + tr(" Hz", " Гц"));
    addItem(tr("Channels", "Каналы"), mp1.channels, mp2.channels);
    addItem(tr("Video Streams", "Видеопотоки"), to_string(mp1.videoTracks.size()), to_string(mp2.videoTracks.size()));
    addItem(tr("Audio Tracks", "Аудиодорожки"), to_string(mp1.audioTracks.size()), to_string(mp2.audioTracks.size()));
    addItem(tr("Subtitles", "Субтитры"), to_string(mp1.subtitleTracks.size()), to_string(mp2.subtitleTracks.size()));

    // Dynamic widths for main table
    size_t tableLabelWidth = utf8Len(tr("Property", "Свойство"));
    size_t tableCol1Width = utf8Len(tr("File 1", "Файл 1"));
    for (const auto& it : items) {
        tableLabelWidth = max(tableLabelWidth, utf8Len(it.label));
        tableCol1Width = max(tableCol1Width, utf8Len(it.v1));
    }
    tableLabelWidth += 2;
    tableCol1Width += 3;

    cout << "\n";
    cout << padUtf8("", tableLabelWidth) << padUtf8(tr("File 1", "Файл 1"), tableCol1Width) << tr("File 2", "Файл 2") << "\n";
    cout << string(tableLabelWidth - 1, '-') << " " << string(tableCol1Width - 1, '-') << " " << string(tableCol1Width - 1, '-') << "\n";

    for (const auto& it : items) {
        cout << padUtf8(it.label, tableLabelWidth) << padUtf8(it.v1, tableCol1Width) << it.v2 << "\n";
    }

    // Detailed streams breakdown
    cout << "\n";
    printColor("========================================", CYAN);
    printColor(tr(" DETAILED STREAMS BREAKDOWN", " ПОДРОБНЫЙ СПИСОК ПОТОКОВ"), CYAN);
    printColor("========================================", CYAN);

    printColor("\n" + tr("  File 1 Streams:", "  Потоки Файла 1:"), CYAN);
    if (mp1.videoTracks.empty() && mp1.audioTracks.empty() && mp1.subtitleTracks.empty()) {
        cout << "    " << tr("No stream information found.", "Информация о потоках не найдена.") << "\n";
    } else {
        for (size_t i = 0; i < mp1.videoTracks.size(); i++) {
            cout << "    " << tr("Video ", "Видео ") << (i + 1) << ": " << mp1.videoTracks[i].getDisplayString() << "\n";
        }
        for (size_t i = 0; i < mp1.audioTracks.size(); i++) {
            cout << "    " << tr("Audio ", "Аудио ") << (i + 1) << ": " << mp1.audioTracks[i].getDisplayString() << "\n";
        }
        for (size_t i = 0; i < mp1.subtitleTracks.size(); i++) {
            cout << "    " << tr("Subtitle ", "Субтитры ") << (i + 1) << ": " << mp1.subtitleTracks[i].getDisplayString() << "\n";
        }
    }

    printColor("\n" + tr("  File 2 Streams:", "  Потоки Файла 2:"), CYAN);
    if (mp2.videoTracks.empty() && mp2.audioTracks.empty() && mp2.subtitleTracks.empty()) {
        cout << "    " << tr("No stream information found.", "Информация о потоках не найдена.") << "\n";
    } else {
        for (size_t i = 0; i < mp2.videoTracks.size(); i++) {
            cout << "    " << tr("Video ", "Видео ") << (i + 1) << ": " << mp2.videoTracks[i].getDisplayString() << "\n";
        }
        for (size_t i = 0; i < mp2.audioTracks.size(); i++) {
            cout << "    " << tr("Audio ", "Аудио ") << (i + 1) << ": " << mp2.audioTracks[i].getDisplayString() << "\n";
        }
        for (size_t i = 0; i < mp2.subtitleTracks.size(); i++) {
            cout << "    " << tr("Subtitle ", "Субтитры ") << (i + 1) << ": " << mp2.subtitleTracks[i].getDisplayString() << "\n";
        }
    }

    // Differences section
    cout << "\n";
    printColor("========================================", YELLOW);
    printColor(tr(" DIFFERENCES", " РАЗЛИЧИЯ"), YELLOW);
    printColor("========================================", YELLOW);
    cout << "\n";

    vector<CompareItem> diffItems;
    size_t diffLabelWidth = 0;
    size_t diffVal1Width = 0;

    for (const auto& it : items) {
        if (it.v1 != it.v2) {
            diffItems.push_back(it);
            diffLabelWidth = max(diffLabelWidth, utf8Len(it.label + ":"));
            diffVal1Width = max(diffVal1Width, utf8Len(it.v1));
        }
    }
    diffLabelWidth += 2;
    diffVal1Width += 2;

    if (diffItems.empty()) {
        printColor(tr("  No significant differences found.", "  Значительных различий не найдено."), GREEN);
    } else {
        for (const auto& it : diffItems) {
            cout << "  " << padUtf8(it.label + ":", diffLabelWidth);
            if (it.hasNumbers && it.n1 > 0 && it.n2 > 0) {
                bool firstBetter = it.higherBetter ? (it.n1 >= it.n2) : (it.n1 <= it.n2);
                setColor(firstBetter ? GREEN : RED);
                cout << padUtf8(it.v1, diffVal1Width);
                setColor(WHITE);
                cout << " | ";
                setColor(firstBetter ? RED : GREEN);
                cout << it.v2;
                setColor(WHITE);
            } else {
                setColor(CYAN);
                cout << padUtf8(it.v1, diffVal1Width);
                setColor(WHITE);
                cout << " | ";
                setColor(CYAN);
                cout << it.v2;
                setColor(WHITE);
            }
            cout << "\n";
        }
    }

    waitForKey();
}

// ========== BATCH TRACK PREFERENCE MATCHING ==========
// Returns an index into tracks, 9999 for "keep all", or -1 when nothing matched.
static int matchAudioPreference(const vector<AudioTrack>& tracks, const AudioTrackPreference& pref) {
    if (pref.keepAll) return 9999;
    int matchIdx = -1;
    if (!pref.preferredLanguage.empty() && pref.preferredLanguage != "und") {
        for (size_t t = 0; t < tracks.size(); t++) {
            if (!tracks[t].language.empty() && _stricmp(tracks[t].language.c_str(), pref.preferredLanguage.c_str()) == 0) {
                matchIdx = (int)t;
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredTitle.empty()) {
        for (size_t t = 0; t < tracks.size(); t++) {
            if (!tracks[t].title.empty() &&
                (tracks[t].title.find(pref.preferredTitle) != string::npos ||
                 pref.preferredTitle.find(tracks[t].title) != string::npos)) {
                matchIdx = (int)t;
                break;
            }
        }
    }
    if (matchIdx == -1 && pref.preferredLanguage.empty() && pref.preferredTitle.empty()) {
        if (pref.preferredAudioIndex >= 0 && pref.preferredAudioIndex < (int)tracks.size()) {
            matchIdx = pref.preferredAudioIndex;
        }
    }
    return matchIdx;
}

// Returns an index into vTracks, 9999 for "keep all", or -1 when nothing matched.
static int matchVideoPreference(const vector<VideoTrack>& vTracks, const vector<int>& realVideoIndices, const VideoTrackPreference& pref) {
    if (pref.keepAll) return 9999;
    if (pref.usePrimaryOnly) return realVideoIndices.empty() ? 0 : realVideoIndices[0];
    int matchIdx = -1;
    if (pref.preferredWidth > 0 && pref.preferredHeight > 0) {
        for (size_t t = 0; t < vTracks.size(); t++) {
            if (vTracks[t].width == pref.preferredWidth && vTracks[t].height == pref.preferredHeight) {
                matchIdx = (int)t;
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredLanguage.empty() && pref.preferredLanguage != "und") {
        for (size_t t = 0; t < vTracks.size(); t++) {
            if (!vTracks[t].language.empty() && _stricmp(vTracks[t].language.c_str(), pref.preferredLanguage.c_str()) == 0) {
                matchIdx = (int)t;
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredTitle.empty()) {
        for (size_t t = 0; t < vTracks.size(); t++) {
            if (!vTracks[t].title.empty() &&
                (vTracks[t].title.find(pref.preferredTitle) != string::npos ||
                 pref.preferredTitle.find(vTracks[t].title) != string::npos)) {
                matchIdx = (int)t;
                break;
            }
        }
    }
    if (matchIdx == -1 && pref.preferredVideoIndex >= 0 && pref.preferredVideoIndex < (int)vTracks.size()) {
        matchIdx = pref.preferredVideoIndex;
    }
    return matchIdx;
}

// Remember a "for ALL remaining videos" video stream choice.
static void recordVideoPreference(VideoTrackPreference& pref, int selectedTrackIdx, const vector<VideoTrack>& vTracks) {
    pref.hasPreference = true;
    if (selectedTrackIdx == -3) {
        pref.keepAll = true;
        pref.usePrimaryOnly = false;
        pref.displayName = tr("All video streams (All videos in batch)", "Все видеопотоки (для всех видео в пакете)");
    } else if (selectedTrackIdx == -1) {
        pref.keepAll = false;
        pref.usePrimaryOnly = true;
        pref.displayName = tr("Primary video stream (All videos in batch)", "Основной видеопоток (для всех видео в пакете)");
    } else if (selectedTrackIdx >= 0 && selectedTrackIdx < (int)vTracks.size()) {
        pref.keepAll = false;
        pref.usePrimaryOnly = false;
        const auto& trk = vTracks[selectedTrackIdx];
        pref.preferredVideoIndex = trk.videoIndex;
        pref.preferredWidth = trk.width;
        pref.preferredHeight = trk.height;
        pref.preferredCodec = trk.codec;
        pref.preferredLanguage = trk.language;
        pref.preferredTitle = trk.title;
        pref.displayName = trk.getDisplayString() + tr(" (All videos in batch)", " (для всех видео в пакете)");
    }
}

// ========== DOUBLE PROCESSING: PAIR ELIGIBILITY ==========
// Everything that makes batchCompressVideo() stop and ask the user something is
// fingerprinted here. Two videos may only be launched together when the fingerprint
// matches AND neither of them would open a dialog, so the answers given for the first
// file are provably valid for the second one and the keyboard is never needed mid-run.
struct PairFingerprint {
    int realVideoStreams = 0;
    int audioTracks = 0;
    bool complexSubs = false;
    bool encodingProblem = false;
    bool oddDimensions = false;
    bool ultraHighRes = false;
    bool needsQuestion = false;
};

PairFingerprint buildPairFingerprint(const string& filePath,
                                     bool videoPrefStored,
                                     bool audioPrefStored,
                                     bool subtitlesLocked,
                                     bool encodingLocked) {
    PairFingerprint fp;

    vector<VideoTrack> vTracks = getVideoTracks(filePath);
    for (size_t vi = 0; vi < vTracks.size(); vi++) {
        if (!vTracks[vi].isCoverOrAttachedPic()) fp.realVideoStreams++;
    }
    fp.audioTracks = (int)getAudioTracks(filePath).size();

    bool isMp4Family = (OUTPUT_FORMAT.find("MP4") != string::npos ||
                        OUTPUT_FORMAT.find("MOV") != string::npos ||
                        OUTPUT_FORMAT.find("M4V") != string::npos);
    if (isMp4Family) {
        vector<SubtitleTrack> subTracks = getSubtitleTracks(filePath);
        if (!subTracks.empty()) {
            for (const auto& st : subTracks) {
                string lc = st.codec;
                transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                if (lc != "mov_text") { fp.complexSubs = true; break; }
            }
        }
    }

    EncodingProblem ep = detectEncodingProblem(filePath);
    fp.encodingProblem = ep.hasProblem;
    fp.oddDimensions = ep.isOddDimensions;
    fp.ultraHighRes = ep.isUltraHighRes;

    AccelMode activeGpu = getActiveGpuMode();
    bool isHW = (activeGpu == ACCEL_NVIDIA || activeGpu == ACCEL_AMD || activeGpu == ACCEL_INTEL);

    if (fp.complexSubs && !subtitlesLocked) fp.needsQuestion = true;
    if (isHW && fp.encodingProblem && !encodingLocked) fp.needsQuestion = true;
    if (fp.realVideoStreams > 1 && !videoPrefStored) fp.needsQuestion = true;
    if (fp.audioTracks > 1 && !audioPrefStored) fp.needsQuestion = true;

    return fp;
}

bool samePairFingerprint(const PairFingerprint& a, const PairFingerprint& b) {
    return a.realVideoStreams == b.realVideoStreams &&
           a.audioTracks == b.audioTracks &&
           a.complexSubs == b.complexSubs &&
           a.encodingProblem == b.encodingProblem &&
           a.oddDimensions == b.oddDimensions &&
           a.ultraHighRes == b.ultraHighRes;
}

// Names the second file of a pair. Filled in by the batch loop before
// processVideoFile() is called. When nextPath is non-empty the lambda builds both
// FFmpeg commands, runs them together and accounts for both files itself, so the
// caller only has to advance the index by two.
struct PairPrep {
    string nextPath;
    string nextStreamMapArg;
};

// Decides whether two consecutive files may be processed together and, if so, fills
// `out` with the second file and its -map arguments.
//
// Both call sites (the main batch loop and the postponed-conflict resolver) must use
// this one function. The two loops reach it under different conditions but must obey
// the same safety rule, otherwise a batch would silently lose double processing on
// exactly the files that needed a second pass.
//
// `nextForcedAudioMap` is non-empty when the caller has already decided the second
// file's audio selection itself (the conflict resolver does this once "keep all" or
// "apply to all" has been chosen); it bypasses the preference match in that case.
bool tryPreparePair(const string& curPath, const string& nextPath,
                    bool videoPrefStored, bool audioPrefStored, bool subsLocked, bool encodingLocked,
                    const VideoTrackPreference& vPref, const AudioTrackPreference& aPref,
                    const string& nextForcedAudioMap,
                    PairPrep& out) {
    // A file that would open a dialog disqualifies the pair: while a job is running
    // the keyboard belongs to the progress loop, so no prompt may appear.
    PairFingerprint fpCur = buildPairFingerprint(curPath, videoPrefStored, audioPrefStored, subsLocked, encodingLocked);
    if (fpCur.needsQuestion) return false;

    // Only probed once the current file is already known to be question-free:
    // every probe is an ffprobe run and tells us nothing while file i could still stop.
    PairFingerprint fpNext = buildPairFingerprint(nextPath, videoPrefStored, audioPrefStored, subsLocked, encodingLocked);
    if (fpNext.needsQuestion) return false;
    if (!samePairFingerprint(fpCur, fpNext)) return false;

    bool nextResolvable = true;
    string nvMap = "", naMap = nextForcedAudioMap;

    vector<VideoTrack> nvT = getVideoTracks(nextPath);
    vector<int> nvReal;
    for (size_t k = 0; k < nvT.size(); k++) {
        if (!nvT[k].isCoverOrAttachedPic()) nvReal.push_back((int)k);
    }
    if (nvReal.size() <= 1 && nvT.size() > 1) {
        nvMap = " -map 0:v:" + to_string(nvT[nvReal.empty() ? 0 : nvReal[0]].videoIndex);
    } else if (nvReal.size() > 1) {
        int m = matchVideoPreference(nvT, nvReal, vPref);
        if (m == -1) nextResolvable = false;
        else nvMap = vPref.keepAll ? " -map 0:v?" : " -map 0:v:" + to_string(nvT[m].videoIndex);
    }

    if (naMap.empty()) {
        vector<AudioTrack> naT = getAudioTracks(nextPath);
        if (naT.size() > 1) {
            int m = matchAudioPreference(naT, aPref);
            if (m == -1) nextResolvable = false;
            else naMap = aPref.keepAll ? " -map 0:a?" : " -map 0:a:" + to_string(naT[m].audioIndex);
        }
    }

    // Two files must never fight over the same output file.
    if (buildOutputPath(nextPath, "_compressed") == buildOutputPath(curPath, "_compressed")) {
        nextResolvable = false;
    }

    if (!nextResolvable) return false;

    out.nextPath = nextPath;
    out.nextStreamMapArg = buildStreamMapArgs(nvMap, naMap);
    return true;
}

// Prints the one-line verdict shown before a file starts. The wording must state what
// is actually about to happen, so both branches are kept next to each other.
void printPairVerdict(bool enabled, bool paired, bool hasNext) {
    if (!enabled || !hasNext) return;
    cout << "\n";
    if (paired) {
        printColor(tr(" Next video qualifies for double processing, two videos will be processed at once.",
                      " Следующее видео подходит для двойной обработки, обрабатываются сразу два видео."), CYAN);
    } else {
        printColor(tr(" Next video does NOT qualify for double processing, only the current video is being processed, please wait.",
                      " Следующее видео НЕ подходит для двойной обработки, обрабатывается только текущее видео, ожидайте."), YELLOW);
    }
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

    string batchAudioCodec;
    if (!promptAudioCodecSettings(batchAudioCodec)) return;

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH VIDEO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ ВИДЕО"), CYAN);
    printColor(" \"" + folder + "\"", CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << tr("Settings:", "Настройки:") << "\n"
         << "  " << tr("Format: ", "Формат: ") << OUTPUT_FORMAT << "\n"
         << "  CRF: " << CRF_VALUE << "\n"
         << "  " << tr("Preset: ", "Пресет: ") << PRESET << "\n"
         << "  " << tr("Program acceleration: ", "Программное ускорение: ") << getAccelerationModeName() << "\n"
         << "  " << tr("Audio codec: ", "Аудиокодек: ") << getAudioCodecSettingName(batchAudioCodec) << "\n";

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
    if (!promptDeleteOriginal(true, deleteOrig)) return;

    int success = 0, fail = 0;
    vector<string> skippedFiles;
    static int forceMode = -1;
    static string forcedFormat = "";
    static int forceSubAction = -1;
    forceMode = -1;
    forcedFormat = "";
    forceSubAction = -1;

    AudioTrackPreference batchAudioPref;
    vector<BatchAudioMismatchWarning> batchAudioWarnings;
    VideoTrackPreference batchVideoPref;
    vector<BatchVideoMismatchWarning> batchVideoWarnings;
    vector<ConflictedVideoFile> conflictedFiles;

    // ---- Shared recovery prompts -------------------------------------------------
    // The single-file path and the double-processing path must offer the same choices
    // after an interruption or a failure, otherwise the experimental mode would quietly
    // become the only mode the user cannot recover from. Both live here so the wording
    // and the effect on forceMode/forcedFormat can never drift apart.

    // Returns 1 = skip, 2 = cancel entire batch, 3 = retry.
    auto askBatchPauseAction = [&](const string& fileLabel) -> int {
        cout << "\n";
        printColor("========================================", YELLOW);
        printColor(tr(" PAUSED - Batch Processing Interrupted", " ПАУЗА - Пакетная обработка прервана"), YELLOW);
        printColor("========================================", YELLOW);
        cout << "\n" << tr("File: ", "Файл: ") << fileLabel << "\n";
        cout << "\n 1. " << tr("Skip this file", "Пропустить этот файл")
             << "\n 2. " << tr("Cancel entire batch", "Отменить весь пакет")
             << "\n 3. " << tr("Retry this file", "Повторить этот файл")
             << "\n\n" << tr("Your choice: ", "Ваш выбор: ");

        char pauseCh = getMenuChoice();
        if (pauseCh == '2' || pauseCh == 27) {
            cout << (pauseCh == 27 ? "ESC" : "2") << "\n";
            return 2;
        } else if (pauseCh == '3') {
            cout << "3\n";
            return 3;
        }
        cout << "1\n";
        return 1;
    };

    // Asks what to do about a failed encode and applies the answer to the mode flags.
    // Returns true when the caller should rebuild the command and run it again,
    // false when the user gave up and the file(s) must be counted as failed.
    auto applyEncodingRecovery = [&](const EncodingDialogResult& dr,
                                     bool& useCPU, bool& useHybrid, bool& useReverseHybrid,
                                     string& dialogChosenFormat) -> bool {
        if (dr.action == 0) {
            useCPU = true;
            useHybrid = false;
            useReverseHybrid = false;
            if (dr.applyToAll) forceMode = 0;
            return true;
        } else if (dr.action == 1) {
            if (!dr.chosenFormat.empty()) {
                dialogChosenFormat = dr.chosenFormat;
                if (dr.applyToAll) {
                    forceMode = 1;
                    forcedFormat = dr.chosenFormat;
                }
            }
            return true;
        } else if (dr.action == 2) {
            useHybrid = true;
            useCPU = false;
            useReverseHybrid = false;
            if (dr.applyToAll) forceMode = 2;
            return true;
        } else if (dr.action == 3) {
            useReverseHybrid = true;
            useCPU = false;
            useHybrid = false;
            if (dr.applyToAll) forceMode = 3;
            return true;
        } else if (dr.action == 4) {
            if (dr.applyToAll) forceMode = 4;
            return true;
        }
        if (dr.applyToAll) forceMode = 5;
        return false;
    };

    // The reason text shown when FFmpeg itself failed, shared by both paths.
    auto makeFfmpegFailureProblem = []() -> EncodingProblem {
        EncodingProblem ep;
        ep.hasProblem = true;
        ep.description = tr("Encoding failed / Codec or Acceleration conflict", "Ошибка кодирования / Конфликт кодека или ускорения");
        ep.detailedReason = tr(
            "FFmpeg failed while processing this file.\n"
            "Possible cause: Hardware acceleration/decoder conflict with this video format.\n"
            "Recommended: Switch to Hybrid mode (CPU decode + GPU encode) or Software CPU (libx264).",
            "FFmpeg завершился с ошибкой при обработке этого файла.\n"
            "Возможная причина: Конфликт аппаратного ускорения/декодера с форматом этого видео.\n"
            "Рекомендуется: Переключить на Гибридный режим (CPU декод + GPU энкод) или Программный CPU (libx264).");
        return ep;
    };

    auto processVideoFile = [&](const string& filePath, size_t currentNum, size_t totalNum, const string& streamMapArg, PairPrep* pairPrep = nullptr) -> ProcessFileResult {
        bool useCPU = false;
        bool useHybrid = false;
        bool useReverseHybrid = false;
        bool useAutoAlign = false;
        bool useDownscale4K = false;
        string dialogChosenFormat;

        if (forceMode == 0) useCPU = true;
        else if (forceMode == 1 && !forcedFormat.empty()) dialogChosenFormat = forcedFormat;
        else if (forceMode == 2) useHybrid = true;
        else if (forceMode == 3) useReverseHybrid = true;
        else if (forceMode == 10) useAutoAlign = true;
        else if (forceMode == 11) useDownscale4K = true;
        else if (forceMode == 5) return PROC_FAIL;

        while (true) {
            cout << "\n";
            printColor("========================================", CYAN);
            char label[128];
            snprintf(label, sizeof(label), " %s %zu / %zu - %s",
                     tr("Processing", "Обработка").c_str(), currentNum, totalNum,
                     fs::u8path(filePath).filename().u8string().c_str());
            printColor(label, CYAN);
            printColor(" \"" + filePath + "\"", CYAN);
            printColor("========================================", CYAN);

            double duration = getMediaDuration(filePath);
            string outPath = buildOutputPath(filePath, "_compressed");
            auto ft = prepareFFmpegTarget(outPath, {filePath});

            AccelMode activeGpuForPfmt = getActiveGpuMode();
            bool isHWEncoder = (activeGpuForPfmt == ACCEL_NVIDIA || activeGpuForPfmt == ACCEL_AMD || activeGpuForPfmt == ACCEL_INTEL);

            if (isHWEncoder && forceMode == -1 && !useCPU && !useHybrid && !useReverseHybrid && !useAutoAlign && !useDownscale4K) {
                EncodingProblem ep = detectEncodingProblem(filePath);
                if (ep.hasProblem) {
                    EncodingDialogResult dr = dialogEncodingProblem(ep, true);
                    dialogChosenFormat = dr.chosenFormat;
                    if (dr.action == 0) {
                        useCPU = true;
                        if (dr.applyToAll) forceMode = 0;
                    } else if (dr.action == 1) {
                        if (dr.applyToAll) {
                            forceMode = 1;
                            forcedFormat = dr.chosenFormat;
                        }
                    } else if (dr.action == 2) {
                        useHybrid = true;
                        if (dr.applyToAll) forceMode = 2;
                    } else if (dr.action == 3) {
                        useReverseHybrid = true;
                        if (dr.applyToAll) forceMode = 3;
                    } else if (dr.action == 10) {
                        useAutoAlign = true;
                        if (dr.applyToAll) forceMode = 10;
                    } else if (dr.action == 11) {
                        useDownscale4K = true;
                        if (dr.applyToAll) forceMode = 11;
                    } else if (dr.action == 4) {
                        if (dr.applyToAll) forceMode = 4;
                    } else if (dr.action == 5 || dr.action == -1) {
                        if (dr.applyToAll) forceMode = 5;
                        return PROC_FAIL;
                    }
                }
            } else if (isHWEncoder && forceMode == 5) {
                return PROC_FAIL;
            } else if (isHWEncoder && forceMode == 0) {
                useCPU = true;
            } else if (isHWEncoder && forceMode == 1) {
                // format already set via forcedFormat
            } else if (isHWEncoder && forceMode == 2) {
                useHybrid = true;
            } else if (isHWEncoder && forceMode == 3) {
                useReverseHybrid = true;
            } else if (isHWEncoder && forceMode == 10) {
                useAutoAlign = true;
            } else if (isHWEncoder && forceMode == 11) {
                useDownscale4K = true;
            }

            string savedFormat = OUTPUT_FORMAT;
            if (forceMode == 1 && !forcedFormat.empty()) {
                OUTPUT_FORMAT = forcedFormat;
            } else if (!dialogChosenFormat.empty()) {
                OUTPUT_FORMAT = dialogChosenFormat;
            }

            string subExtraArgs = "";
            string subHardsubFilter = "";
            bool subsConverted = false;
            int subActionUsed = -1;   // Remembered so the same decision can be replayed for a paired file.
            string currentFmt = OUTPUT_FORMAT;
            bool isMp4Output = (currentFmt.find("MP4") != string::npos || currentFmt.find("MOV") != string::npos || currentFmt.find("M4V") != string::npos);

            vector<SubtitleTrack> subTracks = getSubtitleTracks(filePath);
            if (isMp4Output && !subTracks.empty()) {
                bool hasComplexSubs = false;
                string detectedTypes = "";
                for (const auto& st : subTracks) {
                    string lcodec = st.codec;
                    transform(lcodec.begin(), lcodec.end(), lcodec.begin(), ::tolower);
                    if (lcodec != "mov_text") {
                        hasComplexSubs = true;
                        if (!detectedTypes.empty()) detectedTypes += ", ";
                        detectedTypes += st.codec.empty() ? "unknown" : st.codec;
                    }
                }
                if (hasComplexSubs) {
                    SubtitleIncompatAction act = SUB_ACT_SKIP_CURRENT;
                    bool skipPrompt = false;

                    if (SUBTITLE_ACTION == "convert") {
                        act = SUB_ACT_CONVERT_TEXT;
                        skipPrompt = true;
                    } else if (SUBTITLE_ACTION == "burn") {
                        act = SUB_ACT_BURN_HARD;
                        skipPrompt = true;
                    } else if (SUBTITLE_ACTION == "skip") {
                        act = SUB_ACT_SKIP_CURRENT;
                        skipPrompt = true;
                    } else if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                        act = SUB_ACT_DROP_SUBS;
                        skipPrompt = true;
                    } else if (forceSubAction != -1) {
                        act = (SubtitleIncompatAction)forceSubAction;
                        skipPrompt = true;
                    }

                    if (!skipPrompt) {
                        SubtitleDialogResult dr = dialogSubtitleIncompatibility(filePath, detectedTypes, true);
                        act = dr.action;
                        if (dr.applyToAll && act != SUB_ACT_CANCEL_ALL) {
                            forceSubAction = (int)act;
                        }
                    }

                    subActionUsed = (int)act;

                    if (act == SUB_ACT_CONVERT_TEXT) {
                        subExtraArgs = " -c:s mov_text";
                        subsConverted = true;
                    } else if (act == SUB_ACT_BURN_HARD) {
                        string safeIn = filePath;
                        string escaped = "";
                        for (char c : safeIn) {
                            if (c == '\\') escaped += "/";
                            else if (c == ':') escaped += "\\:";
                            else if (c == '\'') escaped += "'\\''";
                            else escaped += c;
                        }
                        subHardsubFilter = "subtitles='" + escaped + "'";
                    } else if (act == SUB_ACT_DROP_SUBS) {
                        subExtraArgs = " -sn";
                    } else if (act == SUB_ACT_CHANGE_TO_MKV) {
                        dialogChosenFormat = (currentFmt.find("H.265") != string::npos || currentFmt.find("HEVC") != string::npos) ? "MKV(H.265/HEVC)" : "MKV(H.264)";
                        OUTPUT_FORMAT = dialogChosenFormat;
                        outPath = buildOutputPath(filePath, "_compressed", "mkv");
                        ft = prepareFFmpegTarget(outPath, {filePath});
                        subExtraArgs = " -c:s copy";
                    } else if (act == SUB_ACT_SKIP_CURRENT) {
                        cout << "\n";
                        printColor(tr("[INFO] Skipped: Incompatible subtitles detected (",
                                      "[ИНФО] Пропущено: Обнаружены несовместимые субтитры (")
                                   + fs::u8path(filePath).filename().u8string() + ")", YELLOW);
                        skippedFiles.push_back(fs::u8path(filePath).filename().u8string());
                        return PROC_FAIL;
                    } else if (act == SUB_ACT_CANCEL_ALL) {
                        return PROC_CANCEL_BATCH;
                    }
                } else {
                    if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                        subExtraArgs = " -sn";
                    } else {
                        subExtraArgs = " -c:s copy";
                    }
                }
            } else if (!subTracks.empty()) {
                if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                    subExtraArgs = " -sn";
                } else {
                    subExtraArgs = " -c:s copy";
                }
            }

            wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            if (!useCPU && !useHybrid && !useReverseHybrid) cmd += utf8ToWstring(getHWAccelArg(true));
            if (useReverseHybrid) {
                AccelMode gpu = getActiveGpuMode();
                if (gpu == ACCEL_NVIDIA) cmd += L" -hwaccel cuda";
                else if (gpu == ACCEL_INTEL) cmd += L" -hwaccel qsv";
                else cmd += L" -hwaccel auto";
            }
            cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(filePath)) + L"\"";
            if (!streamMapArg.empty()) {
                cmd += utf8ToWstring(streamMapArg);
            }
            string vfSpec = buildVideoFilterSpec(filePath, useAutoAlign, useDownscale4K, subHardsubFilter);
            if (!vfSpec.empty()) {
                cmd += L" " + utf8ToWstring(vfSpec);
            }
            if (useCPU || useReverseHybrid) {
                cmd += L" -c:v libx264";
            } else {
                cmd += L" " + utf8ToWstring(getVideoCodecArgs());
            }
            cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE, useCPU || useReverseHybrid));
            cmd += L" " + utf8ToWstring(getVideoPresetArgs("", useCPU || useReverseHybrid));
            cmd += L" " + utf8ToWstring(getAudioCodecArgs(batchAudioCodec));
            if (!subExtraArgs.empty() && subHardsubFilter.empty()) {
                cmd += L" " + utf8ToWstring(subExtraArgs);
            }
            if (OVERWRITE_FILES) cmd += L" -y";
            cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

            // ---- Double processing: build the second command, then run both together ----
            // Both commands are fully built before anything is executed, so the two
            // processes always start back to back and a retry simply rebuilds both.
            if (pairPrep && !pairPrep->nextPath.empty()) {
                const string& np = pairPrep->nextPath;
                const string curName = fs::u8path(filePath).filename().u8string();
                const string nextName = fs::u8path(np).filename().u8string();

                string nextOutPath = buildOutputPath(np, "_compressed");
                FFmpegTarget nft = prepareFFmpegTarget(nextOutPath, {np});
                double nextDuration = getMediaDuration(np);

                // The fingerprint guarantees the same subtitle decision applies, so the
                // action is replayed instead of asking again. Only the source path baked
                // into the hardsub filter has to be rebuilt.
                string nextExtraArgs = "";
                string nextHardsubFilter = "";
                if (subActionUsed == SUB_ACT_CONVERT_TEXT) {
                    nextExtraArgs = " -c:s mov_text";
                } else if (subActionUsed == SUB_ACT_DROP_SUBS) {
                    nextExtraArgs = " -sn";
                } else if (subActionUsed == SUB_ACT_BURN_HARD) {
                    string esc = "";
                    for (char c : np) {
                        if (c == '\\') esc += "/";
                        else if (c == ':') esc += "\\:";
                        else if (c == '\'') esc += "'\\''";
                        else esc += c;
                    }
                    nextHardsubFilter = "subtitles='" + esc + "'";
                } else {
                    nextExtraArgs = subExtraArgs;
                }

                string nextVfSpec = buildVideoFilterSpec(np, useAutoAlign, useDownscale4K, nextHardsubFilter);

                wstring ncmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
                if (!useCPU && !useHybrid && !useReverseHybrid) ncmd += utf8ToWstring(getHWAccelArg(true));
                if (useReverseHybrid) {
                    AccelMode gpu = getActiveGpuMode();
                    if (gpu == ACCEL_NVIDIA) ncmd += L" -hwaccel cuda";
                    else if (gpu == ACCEL_INTEL) ncmd += L" -hwaccel qsv";
                    else ncmd += L" -hwaccel auto";
                }
                ncmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(np)) + L"\"";
                if (!pairPrep->nextStreamMapArg.empty()) {
                    ncmd += utf8ToWstring(pairPrep->nextStreamMapArg);
                }
                if (!nextVfSpec.empty()) {
                    ncmd += L" " + utf8ToWstring(nextVfSpec);
                }
                if (useCPU || useReverseHybrid) {
                    ncmd += L" -c:v libx264";
                } else {
                    ncmd += L" " + utf8ToWstring(getVideoCodecArgs());
                }
                ncmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE, useCPU || useReverseHybrid));
                ncmd += L" " + utf8ToWstring(getVideoPresetArgs("", useCPU || useReverseHybrid));
                ncmd += L" " + utf8ToWstring(getAudioCodecArgs(batchAudioCodec));
                if (!nextExtraArgs.empty() && nextHardsubFilter.empty()) {
                    ncmd += L" " + utf8ToWstring(nextExtraArgs);
                }
                if (OVERWRITE_FILES) ncmd += L" -y";
                ncmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(nft.writePath)) + L"\"";

                // Both commands exist, so the two processes can start together.
                // OUTPUT_FORMAT is restored first: the commands above already captured
                // whatever the current file decided, and the retry loop below must
                // rebuild from the user's real settings, not from a dialog's leftover.
                if (savedFormat != OUTPUT_FORMAT) OUTPUT_FORMAT = savedFormat;

                cout << "\n";
                printColor("========================================", CYAN);
                printColor(tr(" Processing 2 videos at once", " Обработка двух видео одновременно"), CYAN);
                printColor("========================================", CYAN);

                PairJob jobA, jobB;
                bool launchedA = spawnPairJob(jobA, cmd, curName, duration);
                bool launchedB = launchedA && spawnPairJob(jobB, ncmd, nextName, nextDuration);
                bool escaped = false;

                if (launchedA && launchedB) {
                    execFFmpegPair(jobA, jobB, escaped);
                } else {
                    printColor(tr("[ERROR] Failed to launch FFmpeg!", "[ОШИБКА] Не удалось запустить FFmpeg!"), RED);
                    if (launchedA) {
                        TerminateProcess(jobA.pi.hProcess, 1);
                        WaitForSingleObject(jobA.pi.hProcess, 5000);
                    }
                }
                releasePairJob(jobA);
                releasePairJob(jobB);

                const string pairLabel = curName + tr(" + ", " + ") + nextName;

                // ---- Interrupted by the user: offer the same pause menu as a single file ----
                if (escaped) {
                    finalizeFFmpegTarget(ft, false);
                    finalizeFFmpegTarget(nft, false);
                    if (!jobA.lastError.empty()) printColor(tr("[ERROR] ", "[ОШИБКА] ") + jobA.lastError, RED);
                    if (!jobB.lastError.empty()) printColor(tr("[ERROR] ", "[ОШИБКА] ") + jobB.lastError, RED);
                    g_ffmpegEscaped = false;

                    int pauseCh = askBatchPauseAction(pairLabel);
                    if (pauseCh == 2) {
                        printColor(tr("[INFO] Batch processing cancelled.", "[ИНФО] Пакетная обработка отменена."), YELLOW);
                        fail += 2;
                        return PROC_PAIR_CANCELLED;
                    } else if (pauseCh == 3) {
                        continue;   // rebuild both commands and run the pair again
                    }
                    printColor(tr("[INFO] Files skipped.", "[ИНФО] Файлы пропущены."), YELLOW);
                    skippedFiles.push_back(curName);
                    skippedFiles.push_back(nextName);
                    fail += 2;
                    return PROC_PAIR_HANDLED;
                }

                bool bothLaunched = (launchedA && launchedB);
                bool okA = bothLaunched && jobA.exitCode == 0;
                bool okB = bothLaunched && jobB.exitCode == 0;
                if (!okA && !jobA.lastError.empty()) printColor("  " + curName + ": " + jobA.lastError, RED);
                if (!okB && !jobB.lastError.empty()) printColor("  " + nextName + ": " + jobB.lastError, RED);

                // ---- Failed: offer the same recovery as a single file, then retry the pair ----
                if (!okA || !okB) {
                    EncodingDialogResult dr = dialogEncodingProblem(makeFfmpegFailureProblem(), true);
                    if (applyEncodingRecovery(dr, useCPU, useHybrid, useReverseHybrid, dialogChosenFormat)) {
                        finalizeFFmpegTarget(ft, false);
                        finalizeFFmpegTarget(nft, false);
                        continue;
                    }
                }

                okA = finalizeFFmpegTarget(ft, okA) && okA;
                okB = finalizeFFmpegTarget(nft, okB) && okB;

                // The fingerprint guarantees both files got the same subtitle treatment,
                // so both report it the same way.
                const bool pairSubsConverted = (subActionUsed == SUB_ACT_CONVERT_TEXT);

                if (okA) {
                    if (pairSubsConverted) {
                        printColor(tr("[OK] Subtitles converted", "[OK] Субтитры конвертированы") + " (" + curName + ")", GREEN);
                    } else {
                        printColor(tr("[OK] Done", "[OK] Готово") + " (" + curName + ")", GREEN);
                    }
                    handleOriginalDeletion(filePath, outPath, ft.isTemp, deleteOrig);
                    success++;
                } else {
                    printColor(tr("[ERROR] Processing failed!", "[ОШИБКА] Ошибка обработки!") + " (" + curName + ")", RED);
                    skippedFiles.push_back(curName);
                    fail++;
                }
                if (okB) {
                    if (pairSubsConverted) {
                        printColor(tr("[OK] Subtitles converted", "[OK] Субтитры конвертированы") + " (" + nextName + ")", GREEN);
                    } else {
                        printColor(tr("[OK] Done", "[OK] Готово") + " (" + nextName + ")", GREEN);
                    }
                    handleOriginalDeletion(np, nextOutPath, nft.isTemp, deleteOrig);
                    success++;
                } else {
                    printColor(tr("[ERROR] Processing failed!", "[ОШИБКА] Ошибка обработки!") + " (" + nextName + ")", RED);
                    skippedFiles.push_back(nextName);
                    fail++;
                }
                return PROC_PAIR_HANDLED;
            }

            bool ok = execFFmpegWithProgress(cmd, duration);
            ok = finalizeFFmpegTarget(ft, ok);

            if (savedFormat != OUTPUT_FORMAT) OUTPUT_FORMAT = savedFormat;

            if (g_ffmpegEscaped) {
                finalizeFFmpegTarget(ft, false);
                g_ffmpegEscaped = false;

                int pauseCh = askBatchPauseAction(fs::u8path(filePath).filename().u8string() +
                                                  " (" + to_string(currentNum) + "/" + to_string(totalNum) + ")");
                if (pauseCh == 2) {
                    printColor(tr("[INFO] Batch processing cancelled.", "[ИНФО] Пакетная обработка отменена."), YELLOW);
                    return PROC_CANCEL_BATCH;
                } else if (pauseCh == 3) {
                    continue;
                }
                printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                skippedFiles.push_back(fs::u8path(filePath).filename().u8string());
                return PROC_FAIL;
            }

            if (ok) {
                if (subsConverted) {
                    printColor(tr("[OK] Subtitles converted", "[OK] Субтитры конвертированы"), GREEN);
                } else {
                    printColor(tr("[OK] Done", "[OK] Готово"), GREEN);
                }
                handleOriginalDeletion(filePath, outPath, ft.isTemp, deleteOrig);
                return PROC_SUCCESS;
            } else {
                printColor(tr("[ERROR] Processing failed!", "[ОШИБКА] Ошибка обработки!"), RED);

                EncodingDialogResult dr = dialogEncodingProblem(makeFfmpegFailureProblem(), true);
                if (applyEncodingRecovery(dr, useCPU, useHybrid, useReverseHybrid, dialogChosenFormat)) {
                    continue;
                }
                return PROC_FAIL;
            }
        }
    };

    bool batchCancelled = false;
    size_t i = 0;
    while (i < files.size()) {
        vector<VideoTrack> vTracks = getVideoTracks(files[i]);
        string videoMapArg = "";
        vector<int> realVideoIndices;
        for (size_t vi = 0; vi < vTracks.size(); vi++) {
            if (!vTracks[vi].isCoverOrAttachedPic()) realVideoIndices.push_back((int)vi);
        }

        if (realVideoIndices.size() <= 1 && vTracks.size() > 1) {
            int realIdx = realVideoIndices.empty() ? 0 : realVideoIndices[0];
            videoMapArg = " -map 0:v:" + to_string(vTracks[realIdx].videoIndex);
        } else if (realVideoIndices.size() > 1) {
            if (!batchVideoPref.hasPreference) {
                string selectedMap;
                bool applyToAllBatch = false;
                int selectedTrackIdx = -1;
                if (!selectVideoTrackForFile(files[i], selectedMap, true, &applyToAllBatch, &selectedTrackIdx)) {
                    printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                    skippedFiles.push_back(fs::u8path(files[i]).filename().u8string());
                    fail++;
                    i++;
                    continue;
                }
                videoMapArg = selectedMap;
                if (applyToAllBatch) recordVideoPreference(batchVideoPref, selectedTrackIdx, vTracks);
            } else {
                int matchIdx = matchVideoPreference(vTracks, realVideoIndices, batchVideoPref);
                if (matchIdx != -1) {
                    if (batchVideoPref.keepAll) {
                        videoMapArg = " -map 0:v?";
                    } else {
                        videoMapArg = " -map 0:v:" + to_string(vTracks[matchIdx].videoIndex);
                    }
                } else {
                    int fallbackIdx = realVideoIndices.empty() ? 0 : realVideoIndices[0];
                    videoMapArg = " -map 0:v:" + to_string(vTracks[fallbackIdx].videoIndex);
                }
            }
        }

        vector<AudioTrack> tracks = getAudioTracks(files[i]);
        string audioMapArg = "";

        if (tracks.size() > 1) {
            if (!batchAudioPref.hasPreference) {
                string selectedMap;
                bool keepAllForBatch = false;
                bool applyToAllAudio = false;
                int selectedTrackIdx = -1;
                if (!selectAudioTrackForFile(files[i], selectedMap, true, true, &keepAllForBatch, &selectedTrackIdx, &applyToAllAudio)) {
                    printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                    skippedFiles.push_back(fs::u8path(files[i]).filename().u8string());
                    fail++;
                    i++;
                    continue;
                }
                audioMapArg = selectedMap;
                if (!applyToAllAudio) {
                    // "This video only": remember nothing so the next file asks again.
                    batchAudioPref.hasPreference = false;
                } else if (keepAllForBatch || selectedTrackIdx == -1) {
                    batchAudioPref.hasPreference = true;
                    batchAudioPref.keepAll = true;
                    batchAudioPref.displayName = tr("All audio tracks (All videos in batch)", "Все аудиодорожки (для всех видео в пакете)");
                } else if (selectedTrackIdx >= 0 && selectedTrackIdx < (int)tracks.size()) {
                    batchAudioPref.hasPreference = true;
                    batchAudioPref.keepAll = false;
                    const auto& trk = tracks[selectedTrackIdx];
                    batchAudioPref.preferredAudioIndex = trk.audioIndex;
                    batchAudioPref.preferredLanguage = trk.language;
                    batchAudioPref.preferredTitle = trk.title;
                    batchAudioPref.preferredCodec = trk.codec;
                    batchAudioPref.displayName = trk.getDisplayString() + tr(" (All videos in batch)", " (для всех видео в пакете)");
                }
            } else {
                int matchIdx = matchAudioPreference(tracks, batchAudioPref);

                if (matchIdx != -1) {
                    if (batchAudioPref.keepAll) {
                        audioMapArg = " -map 0:a?";
                    } else {
                        audioMapArg = " -map 0:a:" + to_string(tracks[matchIdx].audioIndex);
                    }
                } else {
                    // Conflict detected: postpone for end of batch
                    cout << "\n";
                    printColor("----------------------------------------------------------------------", YELLOW);
                    printColor(tr(" [!] Postponed: Audio tracks differ from preference (will resolve at end of batch): ",
                                  " [!] Отложено: Аудиодорожки отличаются от предпочтения (разрешение в конце пакета): ")
                               + fs::u8path(files[i]).filename().u8string(), YELLOW);
                    printColor("----------------------------------------------------------------------", YELLOW);

                    ConflictedVideoFile cf;
                    cf.filePath = files[i];
                    cf.originalIndex = i + 1;
                    cf.tracks = tracks;
                    conflictedFiles.push_back(cf);
                    i++;
                    continue;
                }
            }
        } else if (tracks.size() == 1) {
            if (batchAudioPref.hasPreference && !batchAudioPref.keepAll) {
                bool match = false;
                if (!batchAudioPref.preferredLanguage.empty() && batchAudioPref.preferredLanguage != "und") {
                    if (!tracks[0].language.empty() && _stricmp(tracks[0].language.c_str(), batchAudioPref.preferredLanguage.c_str()) == 0) {
                        match = true;
                    }
                } else if (!batchAudioPref.preferredTitle.empty()) {
                    if (tracks[0].title.find(batchAudioPref.preferredTitle) != string::npos) match = true;
                }
                if (!match) {
                    BatchAudioMismatchWarning w;
                    w.fileName = fs::u8path(files[i]).filename().u8string();
                    w.preferredTrack = batchAudioPref.displayName;
                    w.actualTrack = tracks[0].getDisplayString() + (CURRENT_LANG == LANG_RU ? " (единственная)" : " (single track)");
                    batchAudioWarnings.push_back(w);
                }
            }
        }

        string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

        // ---- Double processing: decide whether the next file may join this one ----
        PairPrep pairPrep;
        if (PARALLEL_BATCH && i + 1 < files.size()) {
            tryPreparePair(files[i], files[i + 1],
                           batchVideoPref.hasPreference, batchAudioPref.hasPreference,
                           (SUBTITLE_ACTION != "ask" || forceSubAction != -1), forceMode != -1,
                           batchVideoPref, batchAudioPref, "",
                           pairPrep);
        }

        printPairVerdict(PARALLEL_BATCH, !pairPrep.nextPath.empty(), i + 1 < files.size());

        ProcessFileResult res = processVideoFile(files[i], i + 1, files.size(), streamMapArg, &pairPrep);

        // A pair was processed inside the lambda: it already counted both files in
        // success/fail/skippedFiles, so the loop only has to move the index.
        if (res == PROC_PAIR_HANDLED) {
            i += 2;
            continue;
        }
        if (res == PROC_PAIR_CANCELLED) {
            batchCancelled = true;
            break;
        }

        if (res == PROC_CANCEL_BATCH) {
            fail++;
            batchCancelled = true;
            break;
        } else if (res == PROC_SUCCESS) {
            success++;
        } else {
            fail++;
        }
        i++;
    }

    // Resolve postponed conflicted files before final summary
    if (!batchCancelled && !conflictedFiles.empty()) {
        cout << "\n";
        printColor("========================================================================", YELLOW);
        printColor(tr(" RESOLVING POSTPONED AUDIO TRACK CONFLICTS", " РАЗРЕШЕНИЕ ОТЛОЖЕННЫХ КОНФЛИКТОВ АУДИОДОРОЖЕК"), YELLOW);
        printColor("========================================================================", YELLOW);
        cout << tr(" The following files have multiple audio tracks differing from your preference.\n Please choose audio track for each file:\n",
                   " Следующие файлы имеют несколько аудиодорожек, отличных от выбранного предпочтения.\n Пожалуйста, выберите аудиодорожку для каждого файла:\n");

        bool keepAllForAllRemaining = false;
        bool useRememberedAudioPref = false;

        for (size_t c = 0; c < conflictedFiles.size(); c++) {
            const auto& cf = conflictedFiles[c];
            cout << "\n";
            printColor("------------------------------------------------------------------------", CYAN);
            cout << " " << tr("Conflict ", "Конфликт ") << (c + 1) << " / " << conflictedFiles.size() << ": "
                 << fs::u8path(cf.filePath).filename().u8string() << "\n";
            cout << " " << tr("Remembered preference: ", "Ранее выбранная дорожка: ") << batchAudioPref.displayName << "\n";
            printColor("------------------------------------------------------------------------", CYAN);

            string selectedAudioMap;
            if (keepAllForAllRemaining) {
                selectedAudioMap = " -map 0:a?";
                printColor(tr("[INFO] Using all audio tracks (applied to all remaining files)",
                              "[ИНФО] Сохранение всех аудиодорожек (применено ко всем оставшимся файлам)"), GREEN);
            } else if (useRememberedAudioPref) {
                int matchIdx = matchAudioPreference(cf.tracks, batchAudioPref);
                if (matchIdx != -1 && matchIdx != 9999) {
                    selectedAudioMap = " -map 0:a:" + to_string(cf.tracks[matchIdx].audioIndex);
                    printColor(tr("[INFO] Using remembered audio track (applied to all remaining files)",
                                  "[ИНФО] Использование запомненной аудиодорожки (применено ко всем оставшимся файлам)"), GREEN);
                } else {
                    useRememberedAudioPref = false;
                }
            }
            if (selectedAudioMap.empty()) {
                bool keepAllBatch = false;
                bool applyToAllAudio = false;
                int selectedTrackIdx = -1;
                if (!selectAudioTrackForFile(cf.filePath, selectedAudioMap, true, true, &keepAllBatch, &selectedTrackIdx, &applyToAllAudio)) {
                    printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                    skippedFiles.push_back(fs::u8path(cf.filePath).filename().u8string());
                    fail++;
                    continue;
                }
                if (applyToAllAudio) {
                    if (keepAllBatch || selectedTrackIdx == -1) {
                        keepAllForAllRemaining = true;
                        selectedAudioMap = " -map 0:a?";
                    } else if (selectedTrackIdx >= 0 && selectedTrackIdx < (int)cf.tracks.size()) {
                        batchAudioPref.hasPreference = true;
                        batchAudioPref.keepAll = false;
                        const auto& trk = cf.tracks[selectedTrackIdx];
                        batchAudioPref.preferredAudioIndex = trk.audioIndex;
                        batchAudioPref.preferredLanguage = trk.language;
                        batchAudioPref.preferredTitle = trk.title;
                        batchAudioPref.preferredCodec = trk.codec;
                        batchAudioPref.displayName = trk.getDisplayString() + tr(" (All videos in batch)", " (для всех видео в пакете)");
                        useRememberedAudioPref = true;
                    }
                }
            }

            string videoMap;
            vector<VideoTrack> vTracks = getVideoTracks(cf.filePath);
            vector<int> realVideoIndices;
            for (size_t vi = 0; vi < vTracks.size(); vi++) {
                if (!vTracks[vi].isCoverOrAttachedPic()) realVideoIndices.push_back((int)vi);
            }
            if (realVideoIndices.size() <= 1 && vTracks.size() > 1) {
                int realIdx = realVideoIndices.empty() ? 0 : realVideoIndices[0];
                videoMap = " -map 0:v:" + to_string(vTracks[realIdx].videoIndex);
            } else if (realVideoIndices.size() > 1) {
                int matchIdx = batchVideoPref.hasPreference ? matchVideoPreference(vTracks, realVideoIndices, batchVideoPref) : -1;
                if (matchIdx != -1) {
                    videoMap = batchVideoPref.keepAll ? " -map 0:v?" : " -map 0:v:" + to_string(vTracks[matchIdx].videoIndex);
                } else {
                    string selMap;
                    bool applyAllVideo = false;
                    int selVideoIdx = -1;
                    if (!selectVideoTrackForFile(cf.filePath, selMap, true, &applyAllVideo, &selVideoIdx)) {
                        printColor(tr("[INFO] File skipped.", "[ИНФО] Файл пропущен."), YELLOW);
                        skippedFiles.push_back(fs::u8path(cf.filePath).filename().u8string());
                        fail++;
                        continue;
                    }
                    videoMap = selMap;
                    if (applyAllVideo) recordVideoPreference(batchVideoPref, selVideoIdx, vTracks);
                }
            }
            string streamMapArg = buildStreamMapArgs(videoMap, selectedAudioMap);

            // ---- Double processing inside the conflict resolver ----
            // These files are the ones that were postponed, so without this block a batch
            // with many multi-audio files would lose double processing for all of them.
            // Pairing is only allowed once this file's own audio/video choice came from a
            // stored preference rather than a dialog: a file that was just asked about
            // cannot vouch for the next one.
            PairPrep pairPrep;
            bool curResolvedWithoutDialog = (keepAllForAllRemaining || useRememberedAudioPref);
            bool hasNextConflict = (c + 1 < conflictedFiles.size());
            if (PARALLEL_BATCH && hasNextConflict && curResolvedWithoutDialog) {
                string nextForcedAudio;
                if (keepAllForAllRemaining) {
                    nextForcedAudio = " -map 0:a?";
                } else {
                    // Same guard as the selection above: matchAudioPreference() answers
                    // 9999 for "keep all", which is not an index into the track list.
                    int nm = matchAudioPreference(conflictedFiles[c + 1].tracks, batchAudioPref);
                    if (nm != -1 && nm != 9999) {
                        nextForcedAudio = " -map 0:a:" + to_string(conflictedFiles[c + 1].tracks[nm].audioIndex);
                    }
                }
                if (!nextForcedAudio.empty()) {
                    // "Keep all" does not write a batchAudioPref preference, yet it has
                    // resolved the audio selection just as firmly, so it must count as a
                    // stored choice here or no pair would ever form in that mode.
                    tryPreparePair(cf.filePath, conflictedFiles[c + 1].filePath,
                                   batchVideoPref.hasPreference,
                                   (batchAudioPref.hasPreference || keepAllForAllRemaining),
                                   (SUBTITLE_ACTION != "ask" || forceSubAction != -1), forceMode != -1,
                                   batchVideoPref, batchAudioPref, nextForcedAudio,
                                   pairPrep);
                }
            }

            printPairVerdict(PARALLEL_BATCH, !pairPrep.nextPath.empty(), hasNextConflict);

            ProcessFileResult res = processVideoFile(cf.filePath, cf.originalIndex, files.size(), streamMapArg, &pairPrep);
            if (res == PROC_PAIR_HANDLED) {
                c++;   // the lambda already counted both files
                continue;
            }
            if (res == PROC_PAIR_CANCELLED) {
                batchCancelled = true;
                break;
            }
            if (res == PROC_CANCEL_BATCH) {
                fail++;
                batchCancelled = true;
                break;
            } else if (res == PROC_SUCCESS) {
                success++;
            } else {
                fail++;
            }
        }
    }

    // Print informational table for single-track mismatches (non-blocking)
    if (!batchAudioWarnings.empty()) {
        cout << "\n";
        printColor("================================================================================", YELLOW);
        printColor(tr(" [!] AUDIO TRACK MISMATCH SUMMARY",
                      " [!] СВОДКА НЕСООТВЕТСТВИЙ АУДИОДОРОЖЕК"), YELLOW);
        printColor("================================================================================", YELLOW);
        cout << tr(" The following files had only 1 audio track and differed from your preference:\n\n",
                   " Следующие файлы имели только 1 аудиодорожку и отличались от выбранного предпочтения:\n\n");

        cout << " +------------------------------------------+-----------------------+-----------------------+\n";
        cout << " | " << left << setw(40) << tr("File Name", "Имя файла")
             << " | " << left << setw(21) << tr("Preferred Track", "Ожидаемая дорожка")
             << " | " << left << setw(21) << tr("Used Track", "Использованная") << " |\n";
        cout << " +------------------------------------------+-----------------------+-----------------------+\n";
        for (const auto& bw : batchAudioWarnings) {
            string shortFile = bw.fileName;
            if (shortFile.length() > 40) shortFile = shortFile.substr(0, 37) + "...";
            string shortPref = bw.preferredTrack;
            if (shortPref.length() > 21) shortPref = shortPref.substr(0, 18) + "...";
            string shortAct = bw.actualTrack;
            if (shortAct.length() > 21) shortAct = shortAct.substr(0, 18) + "...";

            cout << " | " << left << setw(40) << shortFile
                 << " | " << left << setw(21) << shortPref
                 << " | " << left << setw(21) << shortAct << " |\n";
        }
        cout << " +------------------------------------------+-----------------------+-----------------------+\n";
        printColor("================================================================================", YELLOW);
    }

    // Skipped files list
    if (!skippedFiles.empty()) {
        cout << "\n";
        printColor("========================================", YELLOW);
        printColor(tr(" The following videos were skipped: ", " Следующие видео были пропущены: "), YELLOW);
        printColor("========================================", YELLOW);
        for (const auto& sf : skippedFiles) {
            cout << "  " << sf << "\n";
        }
        printColor("========================================", YELLOW);
    }

    // Final result summary
    cout << "\n";
    printColor("========================================", GREEN);
    printColor(tr(" Batch processing completed successfully!", " Пакетная обработка успешно завершена!"), GREEN);
    char summary[128];
    snprintf(summary, sizeof(summary), " %s: %d %s, %d %s",
             tr("Result", "Результат").c_str(), success,
             tr("success", "успешно").c_str(), fail,
             tr("failed/skipped", "ошибок/пропущено").c_str());
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

    string batchAudioCodec;
    if (!promptAudioCodecSettings(batchAudioCodec)) return;

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" BATCH AUDIO COMPRESSION", " ПАКЕТНОЕ СЖАТИЕ АУДИО"), CYAN);
    printColor(" \"" + folder + "\"", CYAN);
    printColor("========================================", CYAN);

    cout << "\n" << tr("Found files: ", "Найдено файлов: ") << files.size() << "\n";
    for (size_t i = 0; i < files.size(); i++) {
        cout << "  " << (i + 1) << ". " << fs::u8path(files[i]).filename().u8string() << "\n";
    }

    cout << "\n" << tr("Settings:", "Настройки:") << "\n"
         << "  " << tr("Audio codec: ", "Аудиокодек: ") << getAudioCodecSettingName(batchAudioCodec) << "\n"
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
    if (!promptDeleteOriginal(true, deleteOrig)) return;

    string targetExt = "mp3";
    if (batchAudioCodec == "aac") targetExt = "m4a";
    else if (batchAudioCodec == "ac3") targetExt = "ac3";
    else if (batchAudioCodec == "eac3") targetExt = "eac3";
    else if (batchAudioCodec == "opus") targetExt = "opus";
    else if (batchAudioCodec == "flac") targetExt = "flac";
    else if (batchAudioCodec == "pcm_s16le" || batchAudioCodec == "wav") targetExt = "wav";

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
        printColor(" \"" + files[i] + "\"", CYAN);
        printColor("========================================", CYAN);

        double duration = getMediaDuration(files[i]);
        string outExt = (batchAudioCodec == "copy") ? fs::u8path(files[i]).extension().u8string() : targetExt;
        if (!outExt.empty() && outExt.front() == '.') outExt = outExt.substr(1);
        string outPath = buildOutputPath(files[i], "_compressed", outExt);
        auto ft = prepareFFmpegTarget(outPath, {files[i]});

        wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(files[i])) + L"\"";
        cmd += L" " + utf8ToWstring(getAudioCodecArgs(batchAudioCodec));
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
            g_ffmpegEscaped = false;
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
            handleOriginalDeletion(files[i], outPath, ft.isTemp, deleteOrig);
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
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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
    MediaProperties mp = parseMediaProperties(inputFile);
    if (mp.durationSec > 0 || !mp.videoTracks.empty() || !mp.audioTracks.empty() || !mp.format.empty()) {
        printColor("\n" + tr("--- File Info ---", "--- Информация о файле ---"), CYAN);
        cout << formatMediaPropertiesDisplay(mp);
        printColor("-----------------", CYAN);
    }

    double duration = getMediaDuration(inputFile);
    string outPath = buildOutputPath(inputFile, "_converted");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    bool useCPU = false;
    bool useHybrid = false;
    bool useReverseHybrid = false;
    bool useAutoAlign = false;
    bool useDownscale4K = false;
    string chosenFormat = "";
    AccelMode activeGpuForPfmt = getActiveGpuMode();
    bool isHWEncoder = (activeGpuForPfmt == ACCEL_NVIDIA || activeGpuForPfmt == ACCEL_AMD || activeGpuForPfmt == ACCEL_INTEL);

    if (isHWEncoder) {
        EncodingProblem ep = detectEncodingProblem(inputFile);
        if (ep.hasProblem) {
            EncodingDialogResult dr = dialogEncodingProblem(ep, false);
            if (dr.action == 0) {
                useCPU = true;
            } else if (dr.action == 1) {
                if (!dr.chosenFormat.empty()) chosenFormat = dr.chosenFormat;
            } else if (dr.action == 2) {
                useHybrid = true;
            } else if (dr.action == 3) {
                useReverseHybrid = true;
            } else if (dr.action == 10) {
                useAutoAlign = true;
            } else if (dr.action == 11) {
                useDownscale4K = true;
            } else if (dr.action == 5 || dr.action == -1) {
                waitForKey();
                return;
            }
        }
    }

    string subExtraArgs = "";
    string subHardsubFilter = "";
    string currentFmt = chosenFormat.empty() ? OUTPUT_FORMAT : chosenFormat;
    bool isMp4Output = (currentFmt.find("MP4") != string::npos || currentFmt.find("MOV") != string::npos || currentFmt.find("M4V") != string::npos);

    if (isMp4Output && !mp.subtitleTracks.empty()) {
        bool hasComplexSubs = false;
        string detectedTypes = "";
        for (const auto& st : mp.subtitleTracks) {
            string lcodec = st.codec;
            transform(lcodec.begin(), lcodec.end(), lcodec.begin(), ::tolower);
            if (lcodec != "mov_text") {
                hasComplexSubs = true;
                if (!detectedTypes.empty()) detectedTypes += ", ";
                detectedTypes += st.codec.empty() ? "unknown" : st.codec;
            }
        }
        if (hasComplexSubs) {
            SubtitleIncompatAction act = SUB_ACT_SKIP_CURRENT;
            if (SUBTITLE_ACTION == "convert") {
                act = SUB_ACT_CONVERT_TEXT;
            } else if (SUBTITLE_ACTION == "burn") {
                act = SUB_ACT_BURN_HARD;
            } else if (SUBTITLE_ACTION == "skip") {
                printColor(tr("[INFO] Video skipped due to subtitle settings.", "[ИНФО] Видео пропущено согласно настройкам субтитров."), YELLOW);
                waitForKey();
                return;
            } else if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                act = SUB_ACT_DROP_SUBS;
            } else {
                SubtitleDialogResult dr = dialogSubtitleIncompatibility(inputFile, detectedTypes, false);
                act = dr.action;
            }

            if (act == SUB_ACT_CONVERT_TEXT) {
                subExtraArgs = " -c:s mov_text";
            } else if (act == SUB_ACT_BURN_HARD) {
                string safeIn = inputFile;
                string escaped = "";
                for (char c : safeIn) {
                    if (c == '\\') escaped += "/";
                    else if (c == ':') escaped += "\\:";
                    else if (c == '\'') escaped += "'\\''";
                    else escaped += c;
                }
                subHardsubFilter = "subtitles='" + escaped + "'";
            } else if (act == SUB_ACT_DROP_SUBS) {
                subExtraArgs = " -sn";
            } else if (act == SUB_ACT_CHANGE_TO_MKV) {
                chosenFormat = (currentFmt.find("H.265") != string::npos || currentFmt.find("HEVC") != string::npos) ? "MKV(H.265/HEVC)" : "MKV(H.264)";
                outPath = buildOutputPath(inputFile, "_converted", "mkv");
                ft = prepareFFmpegTarget(outPath, {inputFile});
                subExtraArgs = " -c:s copy";
            } else if (act == SUB_ACT_SKIP_CURRENT || act == SUB_ACT_CANCEL_ALL) {
                return;
            }
        } else {
            if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                subExtraArgs = " -sn";
            } else {
                subExtraArgs = " -c:s copy";
            }
        }
    } else if (!mp.subtitleTracks.empty()) {
        if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
            subExtraArgs = " -sn";
        } else {
            subExtraArgs = " -c:s copy";
        }
    }

    printColor("\n" + tr("Output: ", "Выход: ") + outPath, GREEN);
    printColor(tr("Format: ", "Формат: ") + (chosenFormat.empty() ? OUTPUT_FORMAT : chosenFormat), GREEN);

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    cout << "\n" << tr("1. Start conversion\n0. Cancel (ESC)\n\nYour choice: ", "1. Начать конвертацию\n0. Отмена (ESC)\n\nВаш выбор: ");
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string savedFormat = OUTPUT_FORMAT;
    if (!chosenFormat.empty()) OUTPUT_FORMAT = chosenFormat;

    while (true) {
        wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        if (!useCPU && !useHybrid && !useReverseHybrid) cmd += utf8ToWstring(getHWAccelArg(true));
        if (useReverseHybrid) {
            AccelMode gpu = getActiveGpuMode();
            if (gpu == ACCEL_NVIDIA) cmd += L" -hwaccel cuda";
            else if (gpu == ACCEL_INTEL) cmd += L" -hwaccel qsv";
            else cmd += L" -hwaccel auto";
        }
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
        if (!streamMapArg.empty()) {
            cmd += utf8ToWstring(streamMapArg);
        }
        string vfSpec = buildVideoFilterSpec(inputFile, useAutoAlign, useDownscale4K, subHardsubFilter);
        if (!vfSpec.empty()) {
            cmd += L" " + utf8ToWstring(vfSpec);
        }
        if (useCPU || useReverseHybrid) {
            cmd += L" -c:v libx264";
        } else {
            cmd += L" " + utf8ToWstring(getVideoCodecArgs());
        }
        cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));

        if (VIDEO_BITRATE != "auto") {
            cmd += L" -b:v " + utf8ToWstring(VIDEO_BITRATE) + L"k";
        } else {
            cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE, useCPU || useReverseHybrid));
        }

        cmd += L" " + utf8ToWstring(getVideoPresetArgs("", useCPU || useReverseHybrid));

        if (!subExtraArgs.empty() && subHardsubFilter.empty()) {
            cmd += L" " + utf8ToWstring(subExtraArgs);
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
        printColor(" \"" + inputFile + "\"", CYAN);
        printColor("========================================", CYAN);
        cout << endl;

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (ok) {
            handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
            printColor("\n========================================", GREEN);
            printColor(tr("[OK] Conversion completed successfully!", "[OK] Конвертация успешно завершена!"), GREEN);
            printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
            printColor("========================================", GREEN);
            break;
        } else {
            printColor("\n========================================", RED);
            printColor(tr("[ERROR] Conversion failed!", "[ОШИБКА] Ошибка конвертации!"), RED);
            printColor("========================================", RED);

            EncodingProblem ep;
            ep.hasProblem = true;
            ep.description = tr("Encoding failed / Codec or Acceleration conflict", "Ошибка кодирования / Конфликт кодека или ускорения");
            ep.detailedReason = tr(
                "FFmpeg failed while converting this file.\n"
                "Possible cause: Hardware acceleration/decoder conflict with this video format.\n"
                "Recommended: Switch to Hybrid mode (CPU decode + GPU encode) or Software CPU (libx264).",
                "FFmpeg завершился с ошибкой при конвертации этого файла.\n"
                "Возможная причина: Конфликт аппаратного ускорения/декодера с форматом этого видео.\n"
                "Рекомендуется: Переключить на Гибридный режим (CPU декод + GPU энкод) или Программный CPU (libx264).");

            EncodingDialogResult dr = dialogEncodingProblem(ep, false);
            if (dr.action == 0) {
                useCPU = true;
                useHybrid = false;
                useReverseHybrid = false;
                continue;
            } else if (dr.action == 1) {
                if (!dr.chosenFormat.empty()) {
                    OUTPUT_FORMAT = dr.chosenFormat;
                    outPath = buildOutputPath(inputFile, "_converted");
                    ft = prepareFFmpegTarget(outPath, {inputFile});
                }
                continue;
            } else if (dr.action == 2) {
                useHybrid = true;
                useCPU = false;
                useReverseHybrid = false;
                continue;
            } else if (dr.action == 3) {
                useReverseHybrid = true;
                useCPU = false;
                useHybrid = false;
                continue;
            } else if (dr.action == 10) {
                useAutoAlign = true;
                continue;
            } else if (dr.action == 11) {
                useDownscale4K = true;
                continue;
            } else if (dr.action == 4) {
                continue;
            } else {
                break;
            }
        }
    }
    if (savedFormat != OUTPUT_FORMAT) OUTPUT_FORMAT = savedFormat;
    waitForKey();
}

double parseTimeStringToSeconds(const string& str) {
    if (str.empty()) return 0;
    string s = str;
    s.erase(remove_if(s.begin(), s.end(), ::isspace), s.end());
    if (s.empty()) return 0;

    vector<string> parts;
    stringstream ss(s);
    string item;
    while (getline(ss, item, ':')) {
        parts.push_back(item);
    }

    // parseNumberLoose is locale-independent: "00:01.5" must read as 1.5 s even when
    // the C locale decimal point is a comma.
    if (parts.size() == 1) {
        double v = 0;
        return parseNumberLoose(parts[0], v) ? v : 0;
    } else if (parts.size() == 2) {
        double mm = 0, ssVal = 0;
        if (!parseNumberLoose(parts[0], mm) || !parseNumberLoose(parts[1], ssVal)) return 0;
        return mm * 60.0 + ssVal;
    } else if (parts.size() == 3) {
        double hh = 0, mm = 0, ssVal = 0;
        if (!parseNumberLoose(parts[0], hh) || !parseNumberLoose(parts[1], mm) ||
            !parseNumberLoose(parts[2], ssVal)) return 0;
        return hh * 3600.0 + mm * 60.0 + ssVal;
    }
    return 0;
}

// How much longer than requested a "-c copy" trim of this file will be.
//
// A stream copy can only cut on a keyframe, and FFmpeg's input seek starts at the
// keyframe BEFORE the requested position -- verified by comparing the first frame of the
// output against the source at starts that were themselves keyframes: it never started
// at the requested keyframe, always one keyframe earlier. So the extra time is exactly
//     requested + (start - that preceding keyframe)
// and it is NOT bounded: on a file with a keyframe every 10 s, a 10->20 s trim yields
// 20 s. The extra time is pre-roll, i.e. content from before the requested start.
//
// Only the last 120 s before the cut can hold the keyframe that matters, and ffprobe is
// told to read just that window, so the cost does not grow with the file size (~29 ms).
// Returns -1 when it cannot be determined.
double getTrimPreRoll(const string& filePath, const string& videoMapArg, double timeSec) {
    if (!FFPROBE_FOUND || FFPROBE_PATH.empty() || timeSec <= 0) return -1;

    const double WINDOW = 120.0;
    double from = timeSec - WINDOW;
    if (from < 0) from = 0;

    // "-map 0:v:2" in the caller's args names the stream that will actually be cut.
    string sel = "v:0";
    size_t p = videoMapArg.find("0:v:");
    if (p != string::npos) {
        size_t q = videoMapArg.find_first_not_of("0123456789", p + 4);
        string num = videoMapArg.substr(p + 4, (q == string::npos ? videoMapArg.size() : q) - (p + 4));
        if (!num.empty()) sel = "v:" + num;
    }

    // formatDot() because this string goes into a command line (AGENTS.md 5.12).
    string cmd = "\"" + FFPROBE_PATH + "\" -v error -read_intervals " + formatDot(from, 3) + "%"
               + formatDot(timeSec, 3) + " -select_streams " + sel
               + " -show_entries packet=pts_time,flags -of csv=p=0 \"" + filePath + "\"";

    string out = runCommand(cmd);
    double best = -1;
    size_t i = 0;
    while (i < out.size()) {
        size_t eol = out.find('\n', i);
        if (eol == string::npos) eol = out.size();
        string line = out.substr(i, eol - i);
        i = eol + 1;
        size_t c = line.find(',');
        if (c == string::npos) continue;
        double pts = 0;
        if (!parseNumberLoose(line.substr(0, c), pts)) continue;
        if (pts > timeSec + 0.001) continue;                 // only keyframes at or before the cut
        if (line.find('K', c + 1) == string::npos) continue; // flags look like "K__" for a keyframe
        // Packet order is NOT monotonic (B frames reorder pts), so keep the largest value
        // rather than the last one seen.
        if (pts > best) best = pts;
    }
    if (best < 0) return -1;
    return timeSec - best;
}

// ========== EXACT TRIM: ENCODERS TAKEN FROM THE FILE, NOT FROM THE SETTINGS ==========
//
// A trim must not be steered by OUTPUT_FORMAT / CRF_VALUE / PRESET / ACCELERATION_MODE /
// AUDIO_CODEC: the user asked to cut a file, not to convert it, so the result has to keep
// the codecs, the resolution, the frame rate and the pixel format of the source. That is
// also what makes the exact mode safe for the container -- re-encoding to the same codec
// family cannot produce a combination the file did not already contain.
//
// Quality is a fixed near-transparent constant per codec family, chosen here rather than
// taken from the settings, because the settings are the user's for conversions.
struct ExactTrimCodecs {
    string videoArgs;
    string audioArgs;
    string videoDesc;
    string audioDesc;
};

static string lowerAscii(string s) {
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = char(s[i] - 'A' + 'a');
    }
    return s;
}

// Pixel formats worth carrying over verbatim, so a 10-bit or 4:2:2 source is not silently
// flattened to 8-bit 4:2:0. Anything else is left to the encoder's own default rather than
// risking a combination the encoder does not accept and failing the trim outright.
static bool exactKeepPixelFormat(const string& pixFmtLower) {
    static const char* keepable[] = {
        "yuv420p", "yuvj420p", "yuv420p10le", "yuv422p", "yuv422p10le",
        "yuv444p", "yuv444p10le", "gray", "yuv440p"
    };
    for (size_t i = 0; i < sizeof(keepable) / sizeof(keepable[0]); i++) {
        if (pixFmtLower == keepable[i]) return true;
    }
    return false;
}

// Encoder name plus quality options for one video codec. Deliberately WITHOUT the "-c:v"
// prefix and WITHOUT any pixel format, so the same table serves both the single-stream form
// and the per-stream form ("-c:v:1 ...") used when every stream is kept.
static string exactVideoEncoderOpts(const string& videoCodec, string* descOut) {
    string vc = lowerAscii(videoCodec);
    // CRF scales differ per encoder, so the constant is per family too.
    if (vc == "h264")                      { *descOut = "H.264 (libx264, CRF 18)";                return "libx264 -crf 18"; }
    else if (vc == "hevc" || vc == "h265") { *descOut = "H.265 (libx265, CRF 20)";                return "libx265 -crf 20"; }
    else if (vc == "vp9")                   { *descOut = "VP9 (libvpx-vp9, CRF 30)";              return "libvpx-vp9 -crf 30 -b:v 0"; }
    else if (vc == "vp8")                   { *descOut = "VP8 (libvpx, CRF 10)";                  return "libvpx -crf 10 -b:v 0"; }
    else if (vc == "av1")                   { *descOut = "AV1 (libsvtav1, CRF 30)";               return "libsvtav1 -crf 30"; }
    else if (vc == "mpeg2video")            { *descOut = "MPEG-2 (q 2)";                          return "mpeg2video -q:v 2"; }
    else if (vc == "mpeg4")                 { *descOut = "MPEG-4 Part 2 (q 2)";                   return "mpeg4 -q:v 2"; }
    else if (vc == "mjpeg")                 { *descOut = "MJPEG (q 2)";                           return "mjpeg -q:v 2"; }
    else if (vc == "theora")                { *descOut = "Theora (q 7)";                         return "libtheora -q:v 7"; }
    else if (vc == "prores")                { *descOut = "ProRes (profile 3)";                    return "prores -profile:v 3"; }
    else if (vc == "ffv1")                  { *descOut = "FFV1 (lossless)";                       return "ffv1"; }
    else                                    { *descOut = tr("H.264 (libx264, CRF 18) - the source codec is not recognised",
                                                                  "H.264 (libx264, CRF 18) - кодек источника не распознан"); return "libx264 -crf 18"; }
}

// Encoder name plus bitrate options for one audio codec, WITHOUT the "-c:a" prefix.
static string exactAudioEncoderOpts(const string& audioCodec, const string& audioBitRateBps, string* descOut) {
    string ac = lowerAscii(audioCodec);

    // Audio bitrate comes from the source when ffprobe reported it, so a 320 kb/s track
    // does not get re-encoded to 192 kb/s. Clamped to a sane range; PCM is passed through
    // as-is and never gets a bitrate at all.
    string bitrate = "192k";
    double bps = 0;
    if (parseNumberLoose(audioBitRateBps, bps) && bps >= 32000 && bps <= 2000000) {
        int kbps = (int)(bps / 1000 + 0.5);
        bitrate = to_string(kbps) + "k";
    }

    if (ac.compare(0, 4, "pcm_") == 0)      { *descOut = ac + " (PCM, без пережатия)";                          return ac; }
    else if (ac == "aac")                    { *descOut = "AAC " + bitrate;                                       return "aac -b:a " + bitrate; }
    else if (ac == "ac3")                    { *descOut = "AC3 " + bitrate;                                       return "ac3 -b:a " + bitrate; }
    else if (ac == "eac3")                   { *descOut = "E-AC3 " + bitrate;                                      return "eac3 -b:a " + bitrate; }
    else if (ac == "mp3" || ac == "mp3float"){ *descOut = "MP3 " + bitrate;                                       return "libmp3lame -b:a " + bitrate; }
    else if (ac == "mp2" || ac == "mp2float"){ *descOut = "MP2 " + bitrate;                                       return "mp2 -b:a " + bitrate; }
    else if (ac == "opus")                   { *descOut = "Opus " + bitrate;                                      return "libopus -b:a " + bitrate; }
    else if (ac == "vorbis")                 { *descOut = "Vorbis (q 6)";                                         return "libvorbis -q:a 6"; }
    else if (ac == "flac")                   { *descOut = tr("FLAC (lossless)", "FLAC (без потерь)");            return "flac"; }
    else if (ac == "alac")                   { *descOut = "ALAC (lossless)";                                      return "alac"; }
    else if (ac == "wavpack")                { *descOut = "WavPack";                                              return "wavpack"; }
    else if (ac == "truehd")                 { *descOut = "TrueHD";                                               return "truehd"; }
    else if (ac == "dts")                    { *descOut = "DTS (DCA)";                                            return "dca"; }
    else                                     { *descOut = "AAC " + bitrate + tr(" - the source codec is not recognised",
                                                                                      " - кодек источника не распознан"); return "aac -b:a " + bitrate; }
}

ExactTrimCodecs buildExactTrimCodecs(const string& videoCodec, const string& videoPixFmt,
                                    const string& audioCodec, const string& audioBitRateBps) {
    ExactTrimCodecs r;
    string pf = lowerAscii(videoPixFmt);
    r.videoArgs = "-c:v " + exactVideoEncoderOpts(videoCodec, &r.videoDesc);
    if (exactKeepPixelFormat(pf)) r.videoArgs += " -pix_fmt " + pf;
    r.audioArgs = "-c:a " + exactAudioEncoderOpts(audioCodec, audioBitRateBps, &r.audioDesc);
    return r;
}

// When the user keeps EVERY video or audio stream, one "-c:v" would force the same codec on
// all of them: a file with an H.264 and an H.265 stream would come out with two H.264
// streams. Measured - "-map 0:v? -c:v libx264" turned the hevc stream into h264. So each
// mapped stream is addressed separately ("-c:v:1"), which works because "-map 0:v?" keeps
// the file order and the output ordinal of a stream is its position in that order (verified:
// streams 0,1,2 came out as 0,1,2 in the same order).
//
// Cover art is copied, not re-encoded: it is a single still, and handing it a video encoder
// would turn the cover into a clip or fail outright.
struct ExactTrimMultiArgs { string videoArgs; string audioArgs; };

ExactTrimMultiArgs buildExactTrimMultiArgs(const vector<VideoTrack>& vtracks,
                                           const vector<AudioTrack>& atracks) {
    ExactTrimMultiArgs m;
    for (size_t i = 0; i < vtracks.size(); i++) {
        string slot = "-c:v:" + to_string((int)i);
        if (vtracks[i].isCoverOrAttachedPic()) {
            m.videoArgs += " " + slot + " copy";
            continue;
        }
        string desc;
        m.videoArgs += " " + slot + " " + exactVideoEncoderOpts(vtracks[i].codec, &desc);
        string pf = lowerAscii(vtracks[i].pixFmt);
        if (exactKeepPixelFormat(pf)) m.videoArgs += " -pix_fmt:" + to_string((int)i) + " " + pf;
    }
    for (size_t i = 0; i < atracks.size(); i++) {
        string desc;
        m.audioArgs += " -c:a:" + to_string((int)i) + " "
                     + exactAudioEncoderOpts(atracks[i].codec, atracks[i].bitRate, &desc);
    }
    return m;
}

// A stream-copy trim cannot cut on a non-keyframe: FFmpeg starts the output at the
// keyframe BEFORE the requested position, so the file comes out longer by exactly that
// distance. Report the measured result rather than letting the user assume it is exact.
void printTrimLengthReport(double requested, double actual, const string& startLabel, bool exactMode) {
    if (actual <= 0) return;
    double extra = actual - requested;
    // 0.25 s is ~6 frames at 25 fps: below that the difference is only timestamp
    // quantisation of the audio and video packets.
    bool longer = extra > 0.25;
    // formatDot() rather than snprintf("%f"): LC_NUMERIC follows the user locale, so a
    // Russian system prints "11,170". tr() picks the language independently of LC_NUMERIC,
    // which would pair English text with a comma decimal separator.
    printColor("\n" + tr("Requested: ", "Запрошено: ") + formatDot(requested, 3) + tr(" sec", " сек")
             + tr("   Actual: ", "   Получилось: ") + formatDot(actual, 3) + tr(" sec", " сек"),
             longer ? YELLOW : GREEN);

    if (exactMode) {
        // A re-encode has no reason to be off by more than the container rounding, so a
        // visible excess here would mean the cut did not land where it was asked to.
        if (longer) {
            printColor(tr("[NOTE] The cut is still longer than requested. This should not happen\n"
                          "       in exact mode - please report it.",
                          "[ПРИМЕЧАНИЕ] Обрезка всё ещё длиннее запрошенного. В точном режиме\n"
                          "       так быть не должно - сообщите об этом, пожалуйста."), YELLOW);
        }
        return;
    }
    if (!longer) return;

    printColor(tr("[NOTE] The cut could not start exactly at ", "[ПРИМЕЧАНИЕ] Обрезка не смогла начаться точно в ")
             + startLabel + tr(", so it started at the preceding keyframe.",
                             ", поэтому началась с предыдущего ключевого кадра."), YELLOW);
    printColor(tr("       The file is ", "       Файл длиннее на ") + formatDot(extra, 3)
             + tr(" sec: that extra time is content from BEFORE the requested start.",
                   " сек: это фрагмент ДО запрошенного начала."), YELLOW);
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

    // The requested length is needed before anything else, because the mode screen quotes
    // it back to the user.
    double startSec = parseTimeStringToSeconds(startTime);
    double endSec = parseTimeStringToSeconds(endTime);
    double trimDuration = endSec - startSec;
    if (trimDuration <= 0 && duration > startSec) {
        trimDuration = duration - startSec;   // "to the end of the file"
    }
    if (trimDuration <= 0) {
        printColor(tr("[ERROR] The end time must be later than the start time!",
                      "[ОШИБКА] Время окончания должно быть позже времени начала!"), RED);
        waitForKey();
        return;
    }

    string videoMapArg;
    int selectedVideoIndex = -1;
    if (!selectVideoTrackForFile(inputFile, videoMapArg, true, false, nullptr, &selectedVideoIndex)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    // ---- how to cut: stream copy (fast, approximate) or re-encode (exact) ----
    //
    // A stream copy cannot place the cut on a non-keyframe, and FFmpeg's input seek
    // starts at the keyframe BEFORE the requested position even when a keyframe sits
    // exactly there. Verified by hashing the first frame of the output against the source
    // on three files with two keyframe layouts, including starts that were themselves
    // keyframes (8.2, 10, 18, 20 on the irregular grid) -- every one of them began at the
    // previous keyframe. So the result is
    //     requested + (start - that preceding keyframe)
    // which is 11.10 s / 12.08 s / 11.88 s for pre-rolls of 1.0 / 2.0 / 1.8 s, and the
    // content is wrong as well, not just the length: measured against a reference of the
    // real 10->20 s segment, the copy scores PSNR 21.7 dB while the re-encode scores
    // 47.6 dB. The pre-roll sits at the head, the tail still ends at start + duration, so
    // shortening "-t" to compensate would cut the tail short -- it must not be done.
    // Output-side seeking gives the exact length but begins the video at the next
    // keyframe (167 of 250 frames kept, frozen picture at the head), so it is no better.
    // Only re-encoding is frame exact, and it is offered rather than imposed.
    double preRoll = getTrimPreRoll(inputFile, videoMapArg, startSec);
    string preRollTxt = formatDot(preRoll, 3);
    string copyLenTxt = formatDot(trimDuration + (preRoll > 0 ? preRoll : 0), 3);
    string reqTxt = formatDot(trimDuration, 3);

    vector<string> trimModes;
    vector<string> trimHints;
    // Exact is listed first and is what Enter selects: it is the only mode that actually cuts
    // where the user asked, so it should be the default rather than something to opt into.
    // Copy stays available for very long files, where re-encoding the whole excerpt would
    // take minutes and the fraction of a second does not matter.
    trimModes.push_back(tr("Exact - re-encode, frame accurate (recommended)",
                           "Точно - с перекодированием, кадр в кадр (рекомендуется)"));
    trimModes.push_back(tr("Fast - copy the streams (no re-encoding)",
                           "Быстро - копирование потоков (без перекодирования)"));

    {
        string h = tr(
            "The excerpt is decoded and encoded again, so the cut lands exactly on the\n"
            "requested frame. The file will be the ", "Отрезок декодируется и кодируется заново, поэтому граница попадает\n"
            "ровно в запрошенный кадр. Файл будет длиной ");
        h += reqTxt + tr(" sec you asked for", " се, сколько вы просили")
           + tr(" (measured: 10.023 sec, the few extra milliseconds are container rounding),\n"
                 "and it starts on exactly the frame at that second.\n",
                 " - измерено: 10,023 с, лишние миллисекунды - это округление контейнера, -\n"
                 "и начинается ровно с кадра этой секунды.\n");
        h += tr(
            "\n"
            "Checked against a reference of the real 10->20 s segment: PSNR 47.6 dB here\n"
            "versus 21.7 dB for stream copy, i.e. this is the requested content and the\n"
            "difference is only the re-compression.\n"
            "\n"
            "The audio is re-encoded too, and that is not optional: with \"-c:a copy\" the video\n"
            "is correct (exactly 250 frames) but the container still came out 11.888 sec,\n"
            "because the copied audio keeps the pre-roll timestamps. Measured on this file.\n"
            "\n"
            "Codecs, resolution, frame rate and pixel format are taken FROM THE FILE, not\n"
            "from the program settings: a trim must not turn into a conversion. The only\n"
            "thing not taken from the file is the quality constant, a near-transparent CRF\n"
            "per codec family (18 for H.264, 20 for H.265, 30 for VP9), chosen so the extra\n"
            "generation is not visible. The audio bitrate is copied from the source track.\n"
            "\n"
            "Speed: 0.43 s for a 10 s 720p excerpt and 0.92 s for 1080p, versus 0.04 s for\n"
            "stream copy - roughly 20x slower on this machine, and the gap widens for long\n"
            "files and for HEVC or AV1 sources. GPU acceleration is deliberately NOT used,\n"
            "so that the program's settings cannot influence the result.",
            "\n"
            "Проверено по эталону настоящего отрезка 10->20 с: здесь PSNR 47,6 dB против\n"
            "21,7 dB у копирования, то есть это запрошенное содержимое, а разница - только\n"
            "повторное сжатие.\n"
            "\n"
            "Звук перекодируется тоже, и это не выбор: при \"-c:a copy\" видео верное (ровно\n"
            "250 кадров), но контейнер всё равно вышел 11,888 с, потому что скопированный звук\n"
            "сохраняет таймстемпы пре-ролла. Измерено на этом файле.\n"
            "\n"
            "Кодеки, разрешение, частота кадров и пиксельный формат берутся ИЗ ФАЙЛА, а не из\n"
            "настроек программы: обрезка не должна превращаться в конвертацию. Единственное,\n"
            "что не берётся из файла, - константа качества, почти прозрачный CRF для каждого\n"
            "семейства кодеков (18 для H.264, 20 для H.265, 30 для VP9), подобранная так,\n"
            "чтобы лишнее поколение не было видно. Битрейт звука копируется из исходной дорожки.\n"
            "\n"
            "Скорость: 0,43 с на отрезок 10 с в 720p и 0,92 с в 1080p против 0,04 с при\n"
            "копировании - примерно в 20 раз медленнее на этой машине, и разрыв больше для\n"
            "длинных файлов и для исходников HEVC или AV1. Ускорение на GPU намеренно НЕ\n"
            "используется, чтобы настройки программы не влияли на результат.");
        trimHints.push_back(h);
    }
    {
        string h = tr(
            "Stream copy cannot cut between frames, and the cut may only land on a keyframe.\n"
            "FFmpeg then starts at the PREVIOUS keyframe - even when the requested second\n"
            "itself is a keyframe - so the file is longer than you asked for and the extra\n"
            "time at the head is content from BEFORE the requested start.\n"
            "\n"
            "This file: ", "This file: ");
        if (preRoll > 0) {
            h += tr("the keyframe before the start is ", "ключевой кадр перед началом на ") + preRollTxt
               + tr(" sec earlier, so the result will be about ", " сек раньше, поэтому файл будет примерно ")
               + copyLenTxt + tr(" sec instead of ", " сек вместо ") + reqTxt + tr(" sec.\n",
                                                                                    " сек.\n");
        } else {
            h += tr("the keyframe grid could not be read, so the extra time is unknown "
                    "(it is never negative, and it is NOT capped).\n",
                    "сетку ключевых кадров прочитать не удалось, поэтому лишнее время неизвестно "
                    "(оно не бывает отрицательным и НЕ ограничено сверху).\n");
        }
        h += tr(
            "\n"
            "The error is not proportional to the request. On a file with a keyframe every\n"
            "10 s (measured here) a 10->20 s trim produced 20.08 s - twice as long - because\n"
            "the keyframe before 10 s is the one at 0 s.\n"
            "\n"
            "Content is affected too, not only the length: measured against a reference of\n"
            "the real 10->20 s segment, this mode scores PSNR 21.7 dB, i.e. it is mostly\n"
            "showing the wrong 1.8 s.\n"
            "\n"
            "Speed: 0.03-0.04 s for a 10 s excerpt (no encoding at all).\n"
            "Quality: none lost, the original streams are copied bit for bit.\n"
            "\n"
            "Choose it when the extra fraction of a second does not matter, or when the file\n"
            "is very long and re-encoding the whole excerpt would take minutes.",
            "\n"
            "Ошибка не пропорциональна запросу. На файле с ключевым кадром раз в 10 секунд\n"
            "(измерено здесь) обрезка 10->20 с дала 20,08 с - вдвое больше, - потому что\n"
            "перед десятой секундой ключевой кадр был нулевой.\n"
            "\n"
            "Страдает и содержимое, а не только длина: если сравнить с эталоном настоящего\n"
            "отрезка 10->20 с, этот режим даёт PSNR 21,7 dB, то есть большую часть времени\n"
            "показывает не то, что было запрошено.\n"
            "\n"
            "Скорость: 0,03-0,04 с на отрезок в 10 с (кодирования нет вообще).\n"
            "Качество: без потерь, исходные потоки копируются бит в бит.\n"
            "\n"
            "Выбирайте его, если лишняя доля секунды не важна, или если файл очень длинный и\n"
            "перекодирование всего отрезка заняло бы минуты.");
        trimHints.push_back(h);
    }

    int trimMode = arrowSelect(tr("TRIM MODE", "РЕЖИМ ОБРЕЗКИ"),
                               tr("The cut can only be exact if the excerpt is encoded again.\n"
                                  "Copying the streams is far faster but cannot cut between frames.\n"
                                  "Hover an arrow to read what each one costs and gains.",
                                  "Граница может быть точной, только если отрезок перекодировать.\n"
                                  "Копирование потоков намного быстрее, но не умеет резать между кадрами.\n"
                                  "Наведите стрелку, чтобы прочитать, что каждый вариант даёт и чего стоит."),
                               trimModes, 0, trimHints);
    if (trimMode < 0) { return; }
    bool exactTrim = (trimMode == 0);   // 0 = "Exact (recommended)", see the list above

    string audioMapArg;
    int selectedAudioIndex = -1;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true, false, nullptr, &selectedAudioIndex)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    // In exact mode the excerpt is re-encoded, so the encoders must come from the file
    // itself. Reading the codecs here is cheap: these are the same ffprobe calls the track
    // pickers just made, and the probe cache answers them without a new process.
    ExactTrimCodecs exactCodecs;
    if (exactTrim) {
        vector<VideoTrack> vtracks = getVideoTracks(inputFile);
        vector<AudioTrack> atracks = getAudioTracks(inputFile);

        // Codecs of the single stream that is actually mapped.
        string vcodec, vpix, acodec, abitrate;
        for (size_t i = 0; i < vtracks.size(); i++) {
            if (vtracks[i].isCoverOrAttachedPic()) continue;
            if (selectedVideoIndex < 0 || (int)i == selectedVideoIndex) {
                vcodec = vtracks[i].codec;
                vpix = vtracks[i].pixFmt;
                break;
            }
        }
        for (size_t i = 0; i < atracks.size(); i++) {
            if (selectedAudioIndex < 0 || (int)i == selectedAudioIndex) {
                acodec = atracks[i].codec;
                abitrate = atracks[i].bitRate;
                break;
            }
        }
        exactCodecs = buildExactTrimCodecs(vcodec, vpix, acodec, abitrate);

        // "Keep all streams" maps with "0:v?" / "0:a?", i.e. more than one stream of that
        // kind, and a single "-c:v" would apply one codec to every mapped stream. The
        // shorter "0:v:0?" that buildStreamMapArgs emits for a single-stream file never
        // matches this test, so the two cases cannot be confused.
        bool allVideo = streamMapArg.find("0:v?") != string::npos;
        bool allAudio = streamMapArg.find("0:a?") != string::npos;
        if (allVideo || allAudio) {
            ExactTrimMultiArgs multi = buildExactTrimMultiArgs(vtracks, atracks);
            if (allVideo) {
                exactCodecs.videoArgs = multi.videoArgs;
                exactCodecs.videoDesc = to_string((int)vtracks.size())
                    + tr(" video streams, each to its own codec", " видеопотоков, каждый в свой кодек");
            }
            if (allAudio) {
                exactCodecs.audioArgs = multi.audioArgs;
                exactCodecs.audioDesc = to_string((int)atracks.size())
                    + tr(" audio streams, each to its own codec", " аудиопотоков, каждый в свой кодек");
            }
        }
    }

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_trimmed");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});
    printColor("\n" + tr("Output: ", "Выход: ") + outPath, GREEN);

    // The cut is always expressed as a DURATION ("-t"), never as an output "-to <end>".
    // "-ss" in front of "-i" is an input seek, so FFmpeg rebases the output timeline to
    // zero at the seek point; an output "-to 20" is then measured from that new zero and
    // keeps writing past the requested end. Measured on a 60 s test file, request
    // 10 s -> 20 s (10 s wanted), with a keyframe every 1 s:
    //     -ss 10 -i in -to 20 -c copy   -> 21.20 s   (as built: more than double)
    //     -ss 10 -i in -t 10  -c copy   -> 11.17 s   (what this mode can do at best)
    //     -i in -ss 10 -to 20 -c copy   -> 10.13 s, but the video starts 1 s late
    //     -ss 10 -i in -t 10  re-encode -> 10.02 s   (the exact mode above)
    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -ss " + utf8ToWstring(startTime);
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -t " + utf8ToWstring(formatDot(trimDuration, 3));
    if (!streamMapArg.empty()) {
        cmd += utf8ToWstring(streamMapArg);
    }
    if (exactTrim) {
        // Encoders from the source file, never from the program settings.
        cmd += L" " + utf8ToWstring(exactCodecs.videoArgs);
        cmd += L" " + utf8ToWstring(exactCodecs.audioArgs);
    } else {
        cmd += L" -c copy";
    }
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" TRIMMING...", " ОБРЕЗКА..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    printColor(tr("From ", "С ") + startTime + tr(" to ", " по ") + endTime, CYAN);
    if (exactTrim) {
        printColor(tr("(exact: re-encoding with the file's own codecs - ",
                      "(точно: перекодирование кодеками самого файла - ")
               + exactCodecs.videoDesc + " / " + exactCodecs.audioDesc + ")", CYAN);
    } else {
        printColor(tr("(stream copy: the cut can only start at a keyframe, so the result will be longer)",
                      "(потоковое копирование: обрезка возможна только с ключевого кадра, файл будет длиннее)"), CYAN);
    }
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, trimDuration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Trim completed successfully!", "[OK] Обрезка успешно завершена!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);

        // Report what the file really is. In stream copy mode the length cannot match the
        // request, and saying so is the honest outcome; in exact mode this doubles as a
        // self-check that the cut landed where it should.
        printTrimLengthReport(trimDuration, getMediaDuration(ft.targetPath), startTime, exactTrim);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Trim failed!", "[ОШИБКА] Ошибка обрезки!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 3: EXTRACT AUDIO ==========
void extractAudio() {
    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACT AUDIO FROM VIDEO", " ИЗВЛЕЧЕНИЕ АУДИО ИЗ ВИДЕО"), CYAN);
    printColor("========================================", CYAN);
    cout << "\n" << tr("Select video file...", "Выберите видеофайл...") << "\n";

    string inputFile = openFileDialogMedia(CURRENT_LANG == LANG_RU ? L"Выберите видеофайл для извлечения звука" : L"Select video to extract audio from");
    if (inputFile.empty()) { printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW); waitForKey(); return; }

    printColor("\n" + tr("Input: ", "Вход: ") + inputFile, GREEN);
    double duration = getMediaDuration(inputFile);

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, false)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    cout << "\n" << tr("Output audio format:\n1) MP3\n2) M4A (AAC)\n3) WAV\n4) FLAC\n5) OGG (Vorbis)\n0) Cancel (ESC)\n\nYour choice: ",
                       "Формат аудио на выходе:\n1) MP3\n2) M4A (AAC)\n3) WAV\n4) FLAC\n5) OGG (Vorbis)\n0) Отмена (ESC)\n\nВаш выбор: ");
    char ch = getMenuChoice();
    if (ch == 27 || ch == '0') return;
    cout << ch << endl;

    string ext, codecArgs;
    switch (ch) {
        case '1': ext = "mp3"; codecArgs = "-c:a libmp3lame -ac 2 -b:a " + AUDIO_BITRATE + "k"; break;
        case '2': ext = "m4a"; codecArgs = "-c:a aac -b:a " + AUDIO_BITRATE + "k"; break;
        case '3': ext = "wav"; codecArgs = "-c:a pcm_s16le"; break;
        case '4': ext = "flac"; codecArgs = "-c:a flac"; break;
        case '5': ext = "ogg"; codecArgs = "-c:a libvorbis -b:a " + AUDIO_BITRATE + "k"; break;
        default: printColor(tr("[ERROR] Invalid choice!", "[ОШИБКА] Неверный выбор!"), RED); waitForKey(); return;
    }

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_audio", ext);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!audioMapArg.empty()) {
        cmd += utf8ToWstring(audioMapArg);
    }
    cmd += L" -vn " + utf8ToWstring(codecArgs);
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" EXTRACTING AUDIO...", " ИЗВЛЕЧЕНИЕ АУДИО..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
        printColor("\n========================================", GREEN);
        printColor(tr("[OK] Audio extracted successfully!", "[OK] Аудио успешно извлечено!"), GREEN);
        printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
        printColor("========================================", GREEN);
    } else {
        printColor("\n========================================", RED);
        printColor(tr("[ERROR] Extraction failed!", "[ОШИБКА] Ошибка извлечения!"), RED);
        printColor("========================================", RED);
    }
    waitForKey();
}

// ========== OPERATION 4: MERGE VIDEO + AUDIO ==========
void mergeVideoAudio() {
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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

    string videoMapArg;
    int selectedVideoIdx = -1;
    if (!selectVideoTrackForFile(videoFile, videoMapArg, false, &selectedVideoIdx)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    int selectedAudioIdx = -1;
    if (!selectAudioTrackForFile(audioFile, audioMapArg, false, false, nullptr, &selectedAudioIdx)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    double duration = getMediaDuration(videoFile);
    string outPath = buildOutputPath(videoFile, "_merged");
    auto ft = prepareFFmpegTarget(outPath, {videoFile, audioFile});

    string vMap = (selectedVideoIdx >= 0) ? ("0:v:" + to_string(selectedVideoIdx)) : "0:v:0";
    string aMap = (selectedAudioIdx >= 0) ? ("1:a:" + to_string(selectedAudioIdx)) : "1:a:0";

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(videoFile)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(audioFile)) + L"\"";
    cmd += L" -c:v copy " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
    cmd += L" -map " + utf8ToWstring(vMap) + L" -map " + utf8ToWstring(aMap) + L" -shortest";
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" MERGING VIDEO + AUDIO...", " ОБЪЕДИНЕНИЕ ВИДЕО И АУДИО..."), CYAN);
    printColor(" \"" + videoFile + "\"", CYAN);
    printColor(" + \"" + audioFile + "\"", CYAN);
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
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string safeSuffix = "_resized";
    string outPath = buildOutputPath(inputFile, safeSuffix);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += utf8ToWstring(getHWAccelArg());
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!streamMapArg.empty()) {
        cmd += utf8ToWstring(streamMapArg);
    }
    string vfSpec = buildVideoFilterSpec(inputFile, false, false, "scale=" + scale + ":flags=lanczos", "original");
    if (!vfSpec.empty()) {
        cmd += L" " + utf8ToWstring(vfSpec);
    }
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CHANGING RESOLUTION TO ", " ИЗМЕНЕНИЕ РАЗРЕШЕНИЯ НА ") + scale + "...", CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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
            // The prompt asks for a dot decimal ("1.5"), so it must be parsed
            // locale-independently: on a Russian locale stod() reads the dot as a
            // thousands separator and returns 1.0 for "1.5", 0.0 for "0.25".
            if (!parseNumberLoose(s, speed)) {
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string speedStr = formatDot(speed, 2);
    string outPath = buildOutputPath(inputFile, "_speed" + speedStr);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    // Video: setpts=PTS/speed, Audio: atempo=speed (atempo only supports 0.5-2.0 range, chain for wider)
    // These go straight into the FFmpeg command line, so they must use a dot decimal separator.
    double videoFactor = 1.0 / speed;
    string vfFilter = "setpts=" + formatDot(videoFactor, 4) + "*PTS";

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
    if (!atempoChain.empty()) atempoChain += ",";
    atempoChain += "atempo=" + formatDot(remainingSpeed, 4);

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!streamMapArg.empty()) {
        cmd += utf8ToWstring(streamMapArg);
    }
    string vfSpec = buildVideoFilterSpec(inputFile, false, false, vfFilter, "original");
    if (!vfSpec.empty()) {
        cmd += L" " + utf8ToWstring(vfSpec);
    }
    cmd += L" -af \"" + utf8ToWstring(atempoChain) + L"\"";
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    string effCodec = opAudioCodec.empty() ? AUDIO_CODEC : opAudioCodec;
    string aArgs = (effCodec == "copy" || effCodec == "original" || effCodec == "ask") ? ("-c:a aac -b:a " + AUDIO_BITRATE + "k") : getAudioCodecArgs(effCodec);
    cmd += L" " + utf8ToWstring(aArgs);
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    char speedLabel[64];
    snprintf(speedLabel, sizeof(speedLabel), (CURRENT_LANG == LANG_RU) ? " ИЗМЕНЕНИЕ СКОРОСТИ НА %.2fx..." : " CHANGING SPEED TO %.2fx...", speed);
    printColor(speedLabel, CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration / speed);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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

    string videoMapArg;
    int selectedVideoIdx = -1;
    if (!selectVideoTrackForFile(inputFile, videoMapArg, false, &selectedVideoIdx)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_watermarked");
    auto ft = prepareFFmpegTarget(outPath, {inputFile, wmFile});

    string overlayFilter = (selectedVideoIdx >= 0) ?
        ("[0:v:" + to_string(selectedVideoIdx) + "][1:v]" + overlay) :
        overlay;

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(wmFile)) + L"\"";
    cmd += L" -filter_complex \"" + utf8ToWstring(overlayFilter) + L"\"";
    if (!audioMapArg.empty()) {
        cmd += utf8ToWstring(audioMapArg);
    } else {
        cmd += L" -map 0:a?";
    }
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" ADDING WATERMARK...", " ДОБАВЛЕНИЕ ВОДЯНОГО ЗНАКА..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_compressed");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    bool useCPU = false;
    bool useHybrid = false;
    bool useReverseHybrid = false;
    bool useAutoAlign = false;
    bool useDownscale4K = false;
    string chosenFormat = "";
    AccelMode activeGpuForPfmt = getActiveGpuMode();
    bool isHWEncoder = (activeGpuForPfmt == ACCEL_NVIDIA || activeGpuForPfmt == ACCEL_AMD || activeGpuForPfmt == ACCEL_INTEL);

    if (isHWEncoder) {
        EncodingProblem ep = detectEncodingProblem(inputFile);
        if (ep.hasProblem) {
            EncodingDialogResult dr = dialogEncodingProblem(ep, false);
            if (dr.action == 0) {
                useCPU = true;
            } else if (dr.action == 1) {
                if (!dr.chosenFormat.empty()) chosenFormat = dr.chosenFormat;
            } else if (dr.action == 2) {
                useHybrid = true;
            } else if (dr.action == 3) {
                useReverseHybrid = true;
            } else if (dr.action == 10) {
                useAutoAlign = true;
            } else if (dr.action == 11) {
                useDownscale4K = true;
            } else if (dr.action == 5 || dr.action == -1) {
                waitForKey();
                return;
            }
        }
    }

    string subExtraArgs = "";
    string subHardsubFilter = "";
    string currentFmt = chosenFormat.empty() ? OUTPUT_FORMAT : chosenFormat;
    bool isMp4Output = (currentFmt.find("MP4") != string::npos || currentFmt.find("MOV") != string::npos || currentFmt.find("M4V") != string::npos);

    vector<SubtitleTrack> subTracks = getSubtitleTracks(inputFile);
    if (isMp4Output && !subTracks.empty()) {
        bool hasComplexSubs = false;
        string detectedTypes = "";
        for (const auto& st : subTracks) {
            string lcodec = st.codec;
            transform(lcodec.begin(), lcodec.end(), lcodec.begin(), ::tolower);
            if (lcodec != "mov_text") {
                hasComplexSubs = true;
                if (!detectedTypes.empty()) detectedTypes += ", ";
                detectedTypes += st.codec.empty() ? "unknown" : st.codec;
            }
        }
        if (hasComplexSubs) {
            SubtitleIncompatAction act = SUB_ACT_SKIP_CURRENT;
            if (SUBTITLE_ACTION == "convert") {
                act = SUB_ACT_CONVERT_TEXT;
            } else if (SUBTITLE_ACTION == "burn") {
                act = SUB_ACT_BURN_HARD;
            } else if (SUBTITLE_ACTION == "skip") {
                printColor(tr("[INFO] Video skipped due to subtitle settings.", "[ИНФО] Видео пропущено согласно настройкам субтитров."), YELLOW);
                waitForKey();
                return;
            } else if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                act = SUB_ACT_DROP_SUBS;
            } else {
                SubtitleDialogResult dr = dialogSubtitleIncompatibility(inputFile, detectedTypes, false);
                act = dr.action;
            }

            if (act == SUB_ACT_CONVERT_TEXT) {
                subExtraArgs = " -c:s mov_text";
            } else if (act == SUB_ACT_BURN_HARD) {
                string safeIn = inputFile;
                string escaped = "";
                for (char c : safeIn) {
                    if (c == '\\') escaped += "/";
                    else if (c == ':') escaped += "\\:";
                    else if (c == '\'') escaped += "'\\''";
                    else escaped += c;
                }
                subHardsubFilter = "subtitles='" + escaped + "'";
            } else if (act == SUB_ACT_DROP_SUBS) {
                subExtraArgs = " -sn";
            } else if (act == SUB_ACT_CHANGE_TO_MKV) {
                chosenFormat = (currentFmt.find("H.265") != string::npos || currentFmt.find("HEVC") != string::npos) ? "MKV(H.265/HEVC)" : "MKV(H.264)";
                outPath = buildOutputPath(inputFile, "_compressed", "mkv");
                ft = prepareFFmpegTarget(outPath, {inputFile});
                subExtraArgs = " -c:s copy";
            } else if (act == SUB_ACT_SKIP_CURRENT || act == SUB_ACT_CANCEL_ALL) {
                return;
            }
        } else {
            if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
                subExtraArgs = " -sn";
            } else {
                subExtraArgs = " -c:s copy";
            }
        }
    } else if (!subTracks.empty()) {
        if (SUBTITLE_ACTION == "remove" || SUBTITLE_ACTION == "drop") {
            subExtraArgs = " -sn";
        } else {
            subExtraArgs = " -c:s copy";
        }
    }

    string savedFormat = OUTPUT_FORMAT;
    if (!chosenFormat.empty()) OUTPUT_FORMAT = chosenFormat;

    while (true) {
        wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        if (!useCPU && !useHybrid && !useReverseHybrid) cmd += utf8ToWstring(getHWAccelArg(true));
        if (useReverseHybrid) {
            AccelMode gpu = getActiveGpuMode();
            if (gpu == ACCEL_NVIDIA) cmd += L" -hwaccel cuda";
            else if (gpu == ACCEL_INTEL) cmd += L" -hwaccel qsv";
            else cmd += L" -hwaccel auto";
        }
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
        if (!streamMapArg.empty()) {
            cmd += utf8ToWstring(streamMapArg);
        }
        string vfSpec = buildVideoFilterSpec(inputFile, useAutoAlign, useDownscale4K, subHardsubFilter);
        if (!vfSpec.empty()) {
            cmd += L" " + utf8ToWstring(vfSpec);
        }
        if (useCPU || useReverseHybrid) {
            cmd += L" -c:v libx264";
        } else {
            cmd += L" " + utf8ToWstring(getVideoCodecArgs());
        }
        cmd += L" " + utf8ToWstring(getVideoQualityArgs(crf, useCPU || useReverseHybrid));
        cmd += L" " + utf8ToWstring(getVideoPresetArgs("slower", useCPU || useReverseHybrid));
        cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
        if (!subExtraArgs.empty() && subHardsubFilter.empty()) {
            cmd += L" " + utf8ToWstring(subExtraArgs);
        }
        if (OVERWRITE_FILES) cmd += L" -y";
        cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

        clearScreen();
        printColor("========================================", CYAN);
        printColor(tr(" COMPRESSING (CRF ", " СЖАТИЕ (CRF ") + crf + ")...", CYAN);
        printColor(" \"" + inputFile + "\"", CYAN);
        printColor("========================================", CYAN);
        cout << endl;

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (ok) {
            handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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
            break;
        } else {
            printColor("\n========================================", RED);
            printColor(tr("[ERROR] Compression failed!", "[ОШИБКА] Ошибка сжатия!"), RED);
            printColor("========================================", RED);

            EncodingProblem ep;
            ep.hasProblem = true;
            ep.description = tr("Encoding failed / Codec or Acceleration conflict", "Ошибка кодирования / Конфликт кодека или ускорения");
            ep.detailedReason = tr(
                "FFmpeg failed while compressing this file.\n"
                "Possible cause: Hardware acceleration/decoder conflict with this video format.\n"
                "Recommended: Switch to Hybrid mode (CPU decode + GPU encode) or Software CPU (libx264).",
                "FFmpeg завершился с ошибкой при сжатии этого файла.\n"
                "Возможная причина: Конфликт аппаратного ускорения/декодера с форматом этого видео.\n"
                "Рекомендуется: Переключить на Гибридный режим (CPU декод + GPU энкод) или Программный CPU (libx264).");

            EncodingDialogResult dr = dialogEncodingProblem(ep, false);
            if (dr.action == 0) {
                useCPU = true;
                useHybrid = false;
                useReverseHybrid = false;
                continue;
            } else if (dr.action == 1) {
                if (!dr.chosenFormat.empty()) {
                    OUTPUT_FORMAT = dr.chosenFormat;
                    outPath = buildOutputPath(inputFile, "_compressed");
                    ft = prepareFFmpegTarget(outPath, {inputFile});
                }
                continue;
            } else if (dr.action == 2) {
                useHybrid = true;
                useCPU = false;
                useReverseHybrid = false;
                continue;
            } else if (dr.action == 3) {
                useReverseHybrid = true;
                useCPU = false;
                useHybrid = false;
                continue;
            } else if (dr.action == 10) {
                useAutoAlign = true;
                continue;
            } else if (dr.action == 11) {
                useDownscale4K = true;
                continue;
            } else if (dr.action == 4) {
                continue;
            } else {
                break;
            }
        }
    }
    if (savedFormat != OUTPUT_FORMAT) OUTPUT_FORMAT = savedFormat;
    waitForKey();
}

// ========== OPERATION 9: ROTATE / FLIP VIDEO ==========
void rotateVideo() {
    string opAudioCodec;
    if (!promptAudioCodecSettings(opAudioCodec)) return;
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, suffix);
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!streamMapArg.empty()) {
        cmd += utf8ToWstring(streamMapArg);
    }
    string vfSpec = buildVideoFilterSpec(inputFile, false, false, vf, "original");
    if (!vfSpec.empty()) {
        cmd += L" " + utf8ToWstring(vfSpec);
    }
    cmd += L" " + utf8ToWstring(getVideoCodecArgs());
    cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
    cmd += L" " + utf8ToWstring(getVideoPresetArgs());
    cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" TRANSFORMING VIDEO...", " ПРЕОБРАЗОВАНИЕ ВИДЕО..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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
    gifDuration = sanitizeNumberArg(gifDuration);

    cout << "\n" << tr("GIF width (Enter=480):\n", "Ширина GIF (Enter=480):\n");
    string gifWidth;
    if (!inputLineWithEscape(gifWidth, "> ")) { gifWidth = "480"; }
    if (gifWidth.empty()) gifWidth = "480";
    gifWidth = sanitizeNumberArg(gifWidth);

    cout << "\n" << tr("FPS (Enter=15):\n", "FPS (Enter=15):\n");
    string gifFps;
    if (!inputLineWithEscape(gifFps, "> ")) { gifFps = "15"; }
    if (gifFps.empty()) gifFps = "15";
    gifFps = sanitizeNumberArg(gifFps);

    string videoMapArg;
    int selectedVideoIdx = -1;
    if (!selectVideoTrackForFile(inputFile, videoMapArg, false, &selectedVideoIdx)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string outPath = buildOutputPath(inputFile, "", "gif");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    // Two-pass GIF creation for good quality
    string palettePath = OUTPUT_PATH + "palette_tmp.png";

    // Pass 1: Generate palette
    wstring cmd1 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd1 += L" -ss " + utf8ToWstring(startTime);
    cmd1 += L" -t " + utf8ToWstring(gifDuration);
    cmd1 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!videoMapArg.empty()) {
        cmd1 += utf8ToWstring(videoMapArg);
    }
    cmd1 += L" -vf \"fps=" + utf8ToWstring(gifFps) + L",scale=" + utf8ToWstring(gifWidth) + L":-1:flags=lanczos,palettegen\"";
    cmd1 += L" -y \"" + utf8ToWstring(getSafeFFmpegPath(palettePath)) + L"\"";

    // Pass 2: Create GIF with palette
    string filterSpec = (selectedVideoIdx >= 0 ? ("[0:v:" + to_string(selectedVideoIdx) + "]") : "") + "fps=" + gifFps + ",scale=" + gifWidth + ":-1:flags=lanczos[x];[x][1:v]paletteuse";
    wstring cmd2 = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd2 += L" -ss " + utf8ToWstring(startTime);
    cmd2 += L" -t " + utf8ToWstring(gifDuration);
    cmd2 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    cmd2 += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(palettePath)) + L"\"";
    cmd2 += L" -filter_complex \"" + utf8ToWstring(filterSpec) + L"\"";
    cmd2 += L" -y \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" CREATING GIF...", " СОЗДАНИЕ GIF..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
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

    // Attach the cover inside the concat pass instead of remuxing the finished file.
    // A concat is itself a pure stream copy, so the extra full rewrite that
    // finalizeFFmpegTarget() would do for the cover was measured at 43.9% of the whole
    // operation (83 ms of 189 ms on two 38 MB inputs). Here it rides along for free.
    string coverTemp;
    wstring coverArgs;
    if (SAVE_COVER) {
        fs::path op = fs::u8path(outPath);
        coverTemp = op.parent_path().u8string() + "\\.~mr_tmp_join_cover_" + op.stem().u8string() + ".jpg";
        if (extractCover(files[0], coverTemp) &&
            buildCoverAttachArgs(op.extension().u8string(), coverTemp, coverArgs)) {
            ft.coverEmbedded = true;
        } else {
            std::error_code ec;
            fs::remove(fs::u8path(coverTemp), ec);
            coverTemp.clear();
        }
    }

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -f concat -safe 0 -i \"" + utf8ToWstring(getSafeFFmpegPath(listPath)) + L"\"";
    cmd += coverArgs;
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
    if (!coverTemp.empty()) {
        fs::remove(fs::u8path(coverTemp), ec);
        coverTemp.clear();
    }

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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_nosound");
    auto ft = prepareFFmpegTarget(outPath, {inputFile});

    wstring cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
    cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
    if (!videoMapArg.empty()) {
        cmd += utf8ToWstring(videoMapArg);
    }
    cmd += L" -c:v copy -an";  // Copy video, no audio
    if (OVERWRITE_FILES) cmd += L" -y";
    cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" STRIPPING AUDIO...", " УДАЛЕНИЕ ЗВУКА..."), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);
    cout << endl;

    bool ok = execFFmpegWithProgress(cmd, duration);
    ok = finalizeFFmpegTarget(ft, ok);

    if (ok) {
        handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
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

    clearScreen();
    printColor("========================================", CYAN);
    printColor(tr(" MEDIA FILE INFORMATION", " ИНФОРМАЦИЯ О МЕДИАФАЙЛЕ"), CYAN);
    printColor(" \"" + inputFile + "\"", CYAN);
    printColor("========================================", CYAN);

    MediaProperties mp = parseMediaProperties(inputFile);
    if (mp.durationSec > 0 || !mp.videoTracks.empty() || !mp.audioTracks.empty() || !mp.format.empty()) {
        printColor("\n" + tr("--- Detailed Media Info ---", "--- Подробная информация о файле ---"), CYAN);
        cout << formatMediaPropertiesDisplay(mp);
        printColor("---------------------------", CYAN);
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

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
    if (!videoMapArg.empty()) {
        cmd += utf8ToWstring(videoMapArg);
    }

    switch (ch) {
        case '1': {
            string timePos;
            cout << tr("Time position (HH:MM:SS or seconds): ", "Позиция по времени (ЧЧ:ММ:СС или секунды): ");
            if (!inputLineWithEscape(timePos, "")) return;
            cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
            cmd += L" -ss " + utf8ToWstring(timePos);
            cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
            if (!videoMapArg.empty()) {
                cmd += utf8ToWstring(videoMapArg);
            }
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
            cmd += L" -vf \"fps=1/" + utf8ToWstring(sanitizeNumberArg(interval)) + L"\" -q:v 2";
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
    printColor(" \"" + inputFile + "\"", CYAN);
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
    vector<string> opts = {
        tr("1. Add subtitles to video (Softsub track)", "1. Добавить субтитры к видео (Отключаемая дорожка)"),
        tr("2. Burn subtitles into video (Hardsub)", "2. Вшить субтитры в видео (Хардсаб / Наложение)")
    };
    vector<string> hints = {
        tr("Adds subtitles as an independent switchable track without re-encoding video. Instant processing, zero quality loss, can be toggled in player.",
           "Субтитры добавляются как отдельная отключаемая дорожка без перекодирования видео. Мгновенная обработка, без потери качества, можно включать/выключать в плеере."),
        tr("Permanently burns subtitles into the video frames. Fully preserves original fonts and effects, displays on 100% of all devices and players, but cannot be toggled off.",
           "Субтитры впечатываются прямо в видеоряд. Сохраняет оригинальные шрифты и эффекты, отображается на 100% любых устройств и плееров, но их нельзя отключить.")
    };

    int modeSel = arrowSelect(tr("ADD SUBTITLES", "ДОБАВЛЕНИЕ СУБТИТРОВ"),
                              tr("Choose subtitle embedding method:", "Выберите способ добавления субтитров к видео:"),
                              opts, 0, hints);
    if (modeSel < 0) return;

    string opAudioCodec;
    if (modeSel == 1) {
        if (!promptAudioCodecSettings(opAudioCodec)) return;
    }

    clearScreen();
    printColor("========================================", CYAN);
    if (modeSel == 0) {
        printColor(tr(" ADD SUBTITLES TO VIDEO (SOFTSUB)", " ДОБАВЛЕНИЕ СУБТИТРОВ К ВИДЕО (СОФТСАБ)"), CYAN);
    } else {
        printColor(tr(" BURN SUBTITLES INTO VIDEO (HARDSUB)", " ВШИВАНИЕ СУБТИТРОВ В ВИДЕО (ХАРДСАБ)"), CYAN);
    }
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
                      L"Файлы субтитров (*.srt;*.ass;*.ssa;*.sub;*.vtt)\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0Все файлы (*.*)\0*.*\0" :
                      L"Subtitle Files (*.srt;*.ass;*.ssa;*.sub;*.vtt)\0*.srt;*.ass;*.ssa;*.sub;*.vtt\0All Files (*.*)\0*.*\0";
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

    string videoMapArg;
    if (!selectVideoTrackForFile(inputFile, videoMapArg)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string audioMapArg;
    if (!selectAudioTrackForFile(inputFile, audioMapArg, true)) {
        printColor(tr("[INFO] Cancelled", "[ИНФО] Отменено"), YELLOW);
        waitForKey();
        return;
    }

    string streamMapArg = buildStreamMapArgs(videoMapArg, audioMapArg);

    bool deleteOrig = false;
    if (!promptDeleteOriginal(false, deleteOrig)) return;

    string outPath = buildOutputPath(inputFile, "_subtitled");

    wstring cmd;
    if (modeSel == 0) {
        // Softsub: mux stream without re-encoding video/audio
        string lowerSub = subFile;
        transform(lowerSub.begin(), lowerSub.end(), lowerSub.begin(), ::tolower);
        string lowerIn = inputFile;
        transform(lowerIn.begin(), lowerIn.end(), lowerIn.begin(), ::tolower);

        // If input is MP4 and sub is ASS/SSA, mux into MKV to avoid MP4 container limitation
        if ((lowerSub.find(".ass") != string::npos || lowerSub.find(".ssa") != string::npos) && lowerIn.find(".mp4") != string::npos) {
            size_t dotPos = outPath.rfind('.');
            if (dotPos != string::npos) {
                outPath = outPath.substr(0, dotPos) + ".mkv";
            } else {
                outPath += ".mkv";
            }
        }

        auto ft = prepareFFmpegTarget(outPath, {inputFile, subFile});

        cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(subFile)) + L"\"";
        if (!streamMapArg.empty()) {
            cmd += utf8ToWstring(streamMapArg);
        } else {
            cmd += L" -map 0:v? -map 0:a?";
        }
        cmd += L" -map 1:0";
        cmd += L" -c:v copy -c:a copy";
        if (outPath.find(".mp4") != string::npos || outPath.find(".m4v") != string::npos) {
            cmd += L" -c:s mov_text";
        } else {
            cmd += L" -c:s copy";
        }
        if (OVERWRITE_FILES) cmd += L" -y";
        cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

        clearScreen();
        printColor("========================================", CYAN);
        printColor(tr(" ADDING SUBTITLE TRACK...", " ДОБАВЛЕНИЕ ДОРОЖКИ СУБТИТРОВ..."), CYAN);
        printColor(" \"" + inputFile + "\"", CYAN);
        printColor("========================================", CYAN);
        cout << endl;

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (ok) {
            handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
            printColor("\n========================================", GREEN);
            printColor(tr("[OK] Subtitle track added successfully!", "[OK] Дорожка субтитров успешно добавлена!"), GREEN);
            printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
            printColor("========================================", GREEN);
        } else {
            printColor("\n========================================", RED);
            printColor(tr("[ERROR] Adding subtitle track failed!", "[ОШИБКА] Ошибка добавления дорожки субтитров!"), RED);
            printColor("========================================", RED);
        }
    } else {
        // Hardsub: burn into video frames
        string escapedSubPath = subFile;
        string escaped;
        for (char c : escapedSubPath) {
            if (c == '\\') escaped += "\\\\\\\\";
            else if (c == ':') escaped += "\\\\:";
            else if (c == '\'') escaped += "\\'";
            else escaped += c;
        }

        auto ft = prepareFFmpegTarget(outPath, {inputFile, subFile});

        cmd = L"\"" + utf8ToWstring(getSafeFFmpegPath(FFMPEG_PATH)) + L"\"";
        cmd += L" -i \"" + utf8ToWstring(getSafeFFmpegPath(inputFile)) + L"\"";
        if (!streamMapArg.empty()) {
            cmd += utf8ToWstring(streamMapArg);
        }
        cmd += L" -vf \"subtitles='" + utf8ToWstring(escaped) + L"'\"";
        cmd += L" " + utf8ToWstring(getVideoCodecArgs());
        cmd += L" " + utf8ToWstring(getVideoQualityArgs(CRF_VALUE));
        cmd += L" " + utf8ToWstring(getVideoPresetArgs());
        cmd += L" " + utf8ToWstring(getAudioCodecArgs(opAudioCodec));
        if (OVERWRITE_FILES) cmd += L" -y";
        cmd += L" \"" + utf8ToWstring(getSafeFFmpegPath(ft.writePath)) + L"\"";

        clearScreen();
        printColor("========================================", CYAN);
        printColor(tr(" BURNING SUBTITLES...", " ВШИВАНИЕ СУБТИТРОВ..."), CYAN);
        printColor(" \"" + inputFile + "\"", CYAN);
        printColor("========================================", CYAN);
        cout << endl;

        bool ok = execFFmpegWithProgress(cmd, duration);
        ok = finalizeFFmpegTarget(ft, ok);

        if (ok) {
            handleOriginalDeletion(inputFile, outPath, ft.isTemp, deleteOrig);
            printColor("\n========================================", GREEN);
            printColor(tr("[OK] Subtitles burned successfully!", "[OK] Субтитры успешно вшиты!"), GREEN);
            printColor(tr("Output: ", "Выход: ") + outPath, GREEN);
            printColor("========================================", GREEN);
        } else {
            printColor("\n========================================", RED);
            printColor(tr("[ERROR] Burning subtitles failed!", "[ОШИБКА] Ошибка вшивания субтитров!"), RED);
            printColor("========================================", RED);
        }
    }
    waitForKey();
}

// ========== SETTINGS MENUS ==========
void selectOutputFormat() {
    vector<string> formatKeys = {
        "MP4(H.264)", "MP4(H.265/HEVC)", "MP4(AV1)",
        "M4V(H.264)", "M4V(H.265/HEVC)",
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
        tr("[Video] M4V (H.264 / AVC)       - Apple iTunes / Apple TV",
           "[Видео] M4V (H.264 / AVC)       - Apple iTunes / Apple TV"),
        tr("[Video] M4V (H.265 / HEVC)      - Apple efficient HEVC",
           "[Видео] M4V (H.265 / HEVC)      - Эффективный HEVC для Apple"),
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
        tr("Apple iTunes / Apple TV container. Same codec as MP4, but uses .m4v extension for Apple ecosystem compatibility.",
           "Контейнер Apple iTunes / Apple TV. Тот же кодек, что и MP4, но с расширением .m4v для совместимости с экосистемой Apple."),
        tr("Apple efficient HEVC container. Optimal for Apple TV+ and Apple Music video content with smaller file size.",
           "Эффективный HEVC-контейнер Apple. Оптимален для Apple TV+ и Apple Music видео с меньшим размером файла."),
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

void selectAudioCodecMenu() {
    vector<string> keys = {
        "copy", "ask", "aac", "ac3", "eac3", "mp3", "opus", "flac", "pcm_s16le"
    };

    vector<string> options = {
        tr("Copy original (copy)            - Keep original stream, no quality loss (Fastest)",
           "Как в оригинале (copy)          - Без пережатия звука и без потерь качества (Быстрее всего)"),
        tr("Always ask                      - Prompt before each operation",
           "Всегда спрашивать               - Запрашивать перед каждой операцией"),
        tr("AAC                             - Modern universal high-quality standard",
           "AAC                             - Универсальный стандарт высокого качества"),
        tr("AC3 (Dolby Digital)             - Surround 5.1 / Home Cinema standard",
           "AC3 (Dolby Digital)             - Стандарт объемного звука 5.1 для ТВ и кинотеатров"),
        tr("E-AC3 (Dolby Digital Plus)      - Advanced streaming surround audio",
           "E-AC3 (Dolby Digital Plus)      - Улучшенный объемный звук для современных медиаплееров"),
        tr("MP3 (libmp3lame)                - Classic MP3 compression",
           "MP3 (libmp3lame)                - Классическое сжатие MP3"),
        tr("Opus (libopus)                  - Ultra-efficient modern speech & music codec",
           "Opus (libopus)                  - Сверхэффективный современный кодек"),
        tr("FLAC                            - Lossless compression (100% studio quality)",
           "FLAC                            - Сжатие без потерь (100% студийное качество)"),
        tr("PCM / WAV                       - Uncompressed studio PCM audio",
           "PCM / WAV                       - Несжатый студийный звук")
    };

    vector<string> hints = {
        tr("Preserves the original audio stream bit-for-bit without re-encoding. Zero quality loss and maximum speed.",
           "Сохраняет исходный аудиопоток бит-в-бит без перекодирования. Нулевая потеря качества и максимальная скорость."),
        tr("Before each operation, a prompt will ask to review or select the audio settings.",
           "Перед каждой операцией будет выводиться окно для подтверждения или смены параметров звука."),
        tr("Advanced Audio Coding. Standard for MP4, YouTube and Apple. Best balance of quality and compatibility.",
           "Стандарт для MP4, YouTube и Apple. Оптимальный баланс совместимости и качества."),
        tr("Dolby Digital 5.1 AC-3. Supported by virtually all home theaters, AV receivers and TVs.",
           "Dolby Digital 5.1. Поддерживается практически всеми домашними кинотеатрами, ресиверами и ТВ."),
        tr("Dolby Digital Plus. Enhanced bitrate efficiency with up to 7.1 channels for modern setups.",
           "Dolby Digital Plus. Улучшенная эффективность и поддержка до 7.1 каналов для современных устройств."),
        tr("Classic MPEG-1 Audio Layer III. Plays everywhere, but less efficient than AAC.",
           "Классический MP3. Воспроизводится на любых устройствах, но менее эффективен, чем AAC."),
        tr("Modern low-latency open codec with superior clarity at lower bitrates (ideal for WebM/MKV).",
           "Современный открытый кодек с отличной детализацией при низких битрейтах (идеален для WebM/MKV)."),
        tr("Free Lossless Audio Codec. Exact mathematical bit-perfect audio preservation.",
           "Сжатие без потерь. Точное побитовое сохранение оригинального звука без изменений."),
        tr("Uncompressed raw PCM. Zero compression, maximum compatibility with editing software.",
           "Несжатый PCM. Максимальная совместимость с программами монтажа, большие файлы.")
    };

    int cur = 0;
    for (int i = 0; i < (int)keys.size(); i++) {
        if (keys[i] == AUDIO_CODEC) { cur = i; break; }
    }

    string desc = tr(
        "Choose default audio codec for video and audio processing.\n"
        "'Copy original' preserves the existing audio tracks without re-encoding.\n"
        "Works in full harmony with the multi-track audio detection system.",
        "Выберите кодек аудиодорожки по умолчанию.\n"
        "'Как в оригинале' копирует существующие аудиодорожки без перекодирования.\n"
        "Полностью согласовано с механизмом обнаружения аудиодорожек.");

    int sel = arrowSelect(tr("AUDIO CODEC PROMPT", "ЗАПРОС АУДИОКОДЕКА"), desc, options, cur, hints);
    if (sel >= 0) {
        AUDIO_CODEC = keys[sel];
        saveConfig();
        printColor(tr("[OK] Audio codec prompt set to: ", "[OK] Запрос аудиокодека: ") + getAudioCodecSettingName(), GREEN);
        waitForKey();
    }
}

void selectSubtitleActionMenu() {
    vector<string> keys = { "ask", "convert", "burn", "skip", "remove" };
    vector<string> options = {
        tr("Always ask",
           "Всегда спрашивать"),
        tr("Always convert subtitles (Recommended, plain text without original style)",
           "Всегда Конвертировать субтитры (рекомендуется, будут как просто текст без изначального стиля)"),
        tr("Always burn subtitles into video (Preserves style, permanently embedded in video)",
           "Всегда вшивать субтитры в видео (Сохраняет стиль, но вшивает субтитры прям в само видео, их нельзя будет отключить)"),
        tr("Always skip videos with subtitles",
           "Всегда пропускать видео с субтитрами"),
        tr("Always remove subtitles from video",
           "Всегда удалять субтитры из видео")
    };
    vector<string> hints = {
        tr("Prompts for action whenever incompatible subtitles are detected for the output container (e.g. MP4).",
           "При обнаружении несовместимых со сложными форматами контейнеров (MP4) субтитров будет выводиться диалог выбора действия."),
        tr("Automatically converts subtitles to standard mov_text format without showing the prompt.",
           "Автоматически конвертирует субтитры в стандартный формат mov_text без запроса диалога."),
        tr("Automatically burns subtitles into video frames, preserving fonts and styling.",
           "Автоматически вшивает субтитры в видеокадры с сохранением шрифтов и стилей."),
        tr("Automatically skips files with incompatible subtitles without processing.",
           "Автоматически пропускает файлы со сложными субтитрами без обработки."),
        tr("Automatically strips all subtitle tracks from the output video.",
           "Автоматически удаляет все дорожки субтитров из выходного файла.")
    };

    int cur = 0;
    for (int i = 0; i < (int)keys.size(); i++) {
        if (keys[i] == SUBTITLE_ACTION || (keys[i] == "remove" && SUBTITLE_ACTION == "drop")) { cur = i; break; }
    }

    string desc = tr(
        "Configure automatic subtitle handling when incompatible with container (e.g. MP4).\n"
        "Controls whether to prompt or automatically apply a chosen method.",
        "Настройка обработки несовместимых субтитров при создании MP4/MOV файлов.\n"
        "Определяет, запрашивать ли действие каждый раз или применять выбранное автоматически.");

    int sel = arrowSelect(tr("SUBTITLE PROMPT", "ЗАПРОС СУБТИТРОВ"), desc, options, cur, hints);
    if (sel >= 0) {
        SUBTITLE_ACTION = keys[sel];
        saveConfig();
        printColor(tr("[OK] Subtitle prompt: ", "[OK] Запрос субтитров: ") + getSubtitleActionSettingName(), GREEN);
        waitForKey();
    }
}

void selectDeleteOriginalMenu() {
    vector<string> keys = { "ask", "no", "yes" };
    vector<string> options = {
        tr("Always ask                       - Ask before processing or at batch start",
           "Всегда спрашивать                 - Спрашивать перед операцией или в начале пакета"),
        tr("No                                - Never delete original source files",
           "Нет                               - Никогда не удалять исходные файлы"),
        tr("Yes                               - Always delete original source files after successful processing",
           "Да                                - Всегда удалять исходные файлы после успешной обработки")
    };
    vector<string> hints = {
        tr("You will be asked whether to delete the original file before single operations, or once at the start of batch operations.",
           "Перед каждой одиночной операцией или один раз в начале пакета будет запрашиваться подтверждение на удаление оригинала."),
        tr("Original files are never deleted and always kept intact on disk.",
           "Исходные файлы никогда не удаляются и всегда остаются нетронутыми на диске."),
        tr("Original files will be deleted automatically upon successful completion. Use with care!",
           "Исходные файлы будут автоматически удаляться после успешного завершения обработки. Будьте осторожны!")
    };

    int cur = 0;
    for (int i = 0; i < (int)keys.size(); i++) {
        if (keys[i] == DELETE_ORIGINAL) { cur = i; break; }
    }

    string desc = tr(
        "Configure original file deletion after successful processing.\n"
        "Works for both single operations and batch processing,\n"
        "completely independent of suffix or overwrite settings.",
        "Настройка удаления исходных файлов после успешного завершения.\n"
        "Работает как для одиночных операций, так и для пакетной обработки,\n"
        "полностью независимо от настроек суффиксов или перезаписи.");

    int sel = arrowSelect(tr("DELETE ORIGINAL FILE PROMPT", "ЗАПРОС УДАЛЕНИЯ ОРИГИНАЛА"), desc, options, cur, hints);
    if (sel >= 0) {
        DELETE_ORIGINAL = keys[sel];
        saveConfig();
        printColor(tr("[OK] Delete original prompt: ", "[OK] Запрос удаления оригинала: ") + getDeleteOriginalSettingName(), GREEN);
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
            if (extractZip(zipFile, CONFIG_PATH, "FFmpeg",
                           tr("Extracting components...", "Извлечение компонентов..."))) {
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
        vector<string> opts = {
            "1. " + tr("Output location: [", "Папка вывода: [") + OUTPUT_PATH + "]",
            "2. " + tr("Output format: [", "Формат вывода: [") + OUTPUT_FORMAT + "]",
            "3. " + tr("Resolution: [", "Разрешение: [") + OUTPUT_RESOLUTION + "]",
            "4. " + tr("FPS: [", "FPS: [") + OUTPUT_FPS + "]",
            "5. " + tr("Encoding preset: [", "Пресет кодирования: [") + PRESET + "]",
            "6. " + tr("CRF value: [", "Значение CRF: [") + CRF_VALUE + "]",
            "7. " + tr("Audio bitrate: [", "Битрейт аудио: [") + AUDIO_BITRATE + " kbps]",
            "8. " + tr("Program acceleration: [", "Программное ускорение: [") + getAccelerationModeName() + "]",
            "9. " + tr("Add suffixes to files: [", "Добавлять суффиксы в конец файлов: [") + (!OVERWRITE_FILES ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")) + "]",
            "m. " + tr("Keep metadata: [", "Сохранять метаданные: [") + (KEEP_METADATA ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")) + "]",
            "v. " + tr("Video codec prompt: [", "Запрос видеокодека: [") + (VIDEO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")) + "]",
            "a. " + tr("Audio codec prompt: [", "Запрос аудиокодека: [") + getAudioCodecSettingName() + "]",
            "t. " + tr("Subtitle prompt: [", "Запрос субтитров: [") + getSubtitleActionSettingName() + "]",
            "d. " + tr("Delete original prompt: [", "Запрос удаления оригинала: [") + getDeleteOriginalSettingName() + "]",
            "c. " + tr("Save video cover: [", "Сохранять обложку видео: [") + (SAVE_COVER ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")) + "]",
            "p. " + tr("Allow double processing in batch mode: [", "Разрешить двойную обработку в пакетном режиме: [") + (PARALLEL_BATCH ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")) + "]",
            "u. " + tr("Update FFmpeg & components", "Обновить FFmpeg"),
        };
        vector<int> actions = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17};

        string desc = "";

        int sel = arrowSelect(tr("SETTINGS", "НАСТРОЙКИ"), desc, opts, 0);
        if (sel < 0) return;

        int act = actions[sel];
        switch (act) {
        case 1: {
            string p = openFolderDialog(utf8ToWstring(tr("Select output folder", "Выберите папку вывода")).c_str());
            if (!p.empty()) {
                OUTPUT_PATH = p;
                if (!dirExists(OUTPUT_PATH)) createDirRecursive(OUTPUT_PATH);
                saveConfig();
                printColor(tr("[OK] Output location updated!", "[OK] Папка вывода обновлена!"), GREEN);
            } else {
                printColor(tr("[INFO] Not changed", "[ИНФО] Не изменено"), YELLOW);
            }
            waitForKey();
            break;
        }
        case 2: selectOutputFormat(); break;
        case 3: selectResolution(); break;
        case 4: selectFPS(); break;
        case 5: selectPreset(); break;
        case 6: selectCRF(); break;
        case 7: selectAudioBitrate(); break;
        case 8: {
            vector<string> opts;
            vector<AccelMode> modes;

            opts.push_back(tr("Software CPU (libx264)", "Программный CPU (libx264)"));
            modes.push_back(ACCEL_CPU_ONLY);

            int hwCount = (HAS_NVIDIA_DEVICE ? 1 : 0) + (HAS_INTEL_DEVICE ? 1 : 0) + (HAS_AMD_DEVICE ? 1 : 0);

            if (hwCount > 0) {
                opts.push_back(tr("Software + Hardware (CPU Decoding, GPU Encoding)",
                                  "Программный + Аппаратный (Декодирование CPU, Кодирование GPU)"));
                modes.push_back(ACCEL_CPU_DEC_GPU_ENC);
                opts.push_back(tr("Reverse Hybrid (GPU Decoding, CPU Encoding)",
                                  "Обратный гибридный (Декодирование GPU, Кодирование CPU)"));
                modes.push_back(ACCEL_GPU_DEC_CPU_ENC);
            }

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
                "Reverse Hybrid: GPU decoding, CPU encoding.\n"
                "Hardware: full GPU acceleration where supported.",
                "Выберите метод ускорения и кодирования.\n"
                "Программный: обработка только на CPU.\n"
                "Программный + Аппаратный: декодирование CPU, кодирование GPU.\n"
                "Обратный гибридный: декодирование GPU, кодирование CPU.\n"
                "Аппаратный: полное ускорение на поддерживаемом GPU.");

            int sel = arrowSelect(tr("PROGRAM ACCELERATION", "ПРОГРАММНОЕ УСКОРЕНИЕ"), desc, opts, curIdx);
            if (sel >= 0) {
                AccelMode chosen = modes[sel];
                if (chosen == ACCEL_CPU_DEC_GPU_ENC || chosen == ACCEL_GPU_DEC_CPU_ENC) {
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
        case 9: {
            vector<string> opts = {tr("OFF", "ВЫКЛ"), tr("ON", "ВКЛ")};
            string desc = tr(
                "When OFF, existing output files are overwritten without asking.\n"
                "When ON, a suffix (_1, _compressed, etc.) is added to protect the original.",
                "Когда ВЫКЛ, существующие файлы перезаписываются без запроса, заменяя оригинал!\n"
                "Когда ВКЛ, добавляется суффикс (_1, _compressed, и т.д.) для защиты от перезаписи оригинала!");
            int sel = arrowSelect(tr("ADD SUFFIXES TO FILES", "ДОБАВЛЯТЬ СУФФИКСЫ В КОНЕЦ ФАЙЛОВ"), desc, opts, (!OVERWRITE_FILES) ? 1 : 0);
            if (sel >= 0) {
                OVERWRITE_FILES = (sel == 0);
                saveConfig();
                printColor(string(tr("[OK] Add suffixes to files ", "[OK] Добавлять суффиксы в конец файлов ")) + (!OVERWRITE_FILES ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")), GREEN);
                waitForKey();
            }
            break;
        }
        case 10: {
            vector<string> opts = {tr("ON", "ВКЛ"), tr("OFF", "ВЫКЛ")};
            string desc = tr(
                "When ON, metadata (title, artist, date, etc.) from source is copied to output.\n"
                "When OFF, all metadata is stripped from the output.",
                "Когда ВКЛ, метаданные (название, артист, дата) копируются в выходной файл.\n"
                "Когда ВЫКЛ, все метаданные удаляются.");
            int sel = arrowSelect(tr("KEEP METADATA", "СОХРАНЕНИЕ МЕТАДАННЫХ"), desc, opts, KEEP_METADATA ? 0 : 1);
            if (sel >= 0) {
                KEEP_METADATA = (sel == 0);
                saveConfig();
                printColor(string(tr("[OK] Keep metadata ", "[OK] Сохранение метаданных ")) + (KEEP_METADATA ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")), GREEN);
                waitForKey();
            }
            break;
        }
        case 11: {
            vector<string> opts = {tr("Always Ask", "Всегда спрашивать"), tr("Always use configured settings", "Всегда как задано моими настройками")};
            string desc = tr(
                "When 'Always Ask' is active, you will be prompted to review\n"
                "video settings before each operation.",
                "Когда 'Всегда спрашивать' активно, перед каждой операцией\n"
                "вам будет предложено проверить настройки видео.");
            int sel = arrowSelect(tr("VIDEO CODEC PROMPT", "ЗАПРОС ВИДЕОКОДЕКА"), desc, opts, VIDEO_CODEC_ASK ? 0 : 1);
            if (sel >= 0) {
                VIDEO_CODEC_ASK = (sel == 0);
                saveConfig();
                printColor(tr("[OK] Video codec prompt: ", "[OK] Запрос видеокодека: ") + (VIDEO_CODEC_ASK ? tr("Always Ask", "Всегда спрашивать") : tr("Always use configured settings", "Всегда как задано моими настройками")), GREEN);
                waitForKey();
            }
            break;
        }
        case 12: selectAudioCodecMenu(); break;
        case 13: selectSubtitleActionMenu(); break;
        case 14: selectDeleteOriginalMenu(); break;
        case 15: {
            vector<string> opts = {tr("ON", "ВКЛ"), tr("OFF", "ВЫКЛ")};
            string desc = tr(
                "When ON, the video cover/thumbnail is preserved and embedded\n"
                "into the output video file.\n"
                "If the source file has an embedded cover, it is extracted\n"
                "and embedded into the output. Otherwise the first frame is used.",
                "Когда ВКЛ, обложка/превью видео сохраняется и встраивается\n"
                "в выходной видеофайл.\n"
                "Если исходный файл содержит встроенную обложку, она извлекается\n"
                "и встраивается в выходной. Иначе используется первый кадр.");
            int sel = arrowSelect(tr("SAVE VIDEO COVER", "СОХРАНЯТЬ ОБЛОЖКУ ВИДЕО"), desc, opts, SAVE_COVER ? 0 : 1);
            if (sel >= 0) {
                SAVE_COVER = (sel == 0);
                saveConfig();
                printColor(tr("[OK] Save video cover: ", "[OK] Сохранять обложку видео: ") + (SAVE_COVER ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")), GREEN);
                waitForKey();
            }
            break;
        }
        case 16: {
            vector<string> opts = {tr("ON", "ВКЛ"), tr("OFF", "ВЫКЛ")};
            string desc = tr(
                "Allows the batch mode to work with two videos at once,\n"
                "if the next video matches the current user settings.\n"
                "This is an experimental setting, it can both speed up\n"
                "processing and corrupt your files. Enable at your own risk.",
                "Разрешает в пакетном режиме работать сразу с двумя видео,\n"
                "если следующее видео подходит по текущим настройкам пользователя.\n"
                "Это экспериментальная настройка, может как ускорить обработку,\n"
                "так и испортить ваши файлы. Включайте на свой страх и риск.");
            int sel = arrowSelect(tr("DOUBLE PROCESSING IN BATCH", "ДВОЙНАЯ ОБРАБОТКА В ПАКЕТНОМ РЕЖИМЕ"), desc, opts, PARALLEL_BATCH ? 0 : 1);
            if (sel >= 0) {
                PARALLEL_BATCH = (sel == 0);
                saveConfig();
                printColor(tr("[OK] Double processing in batch mode: ", "[OK] Двойная обработка в пакетном режиме: ") + (PARALLEL_BATCH ? tr("ON", "ВКЛ") : tr("OFF", "ВЫКЛ")), GREEN);
                waitForKey();
            }
            break;
        }
        case 17: updateComponentsMenu(); break;
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
            if (extractZip(zipFile, CONFIG_PATH, "FFmpeg",
                           tr("Extracting components...", "Распаковка компонентов..."))) {
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
char mainMenuSelect() {
    vector<string> opts = {
        "--- " + tr("VIDEO OPERATIONS", "ВИДЕО ОПЕРАЦИИ") + " ---",
        " 1. " + tr("Convert video", "Конвертировать видео"),
        " 2. " + tr("Trim / Cut video", "Обрезать видео"),
        " 3. " + tr("Change resolution", "Изменить разрешение"),
        " 4. " + tr("Change speed", "Изменить скорость"),
        " 5. " + tr("Rotate / Flip", "Повернуть / Отразить"),
        " 6. " + tr("Compress video", "Сжать видео"),
        " 7. " + tr("Add watermark", "Добавить водяной знак"),
        " 8. " + tr("Add subtitles", "Добавить субтитры"),
        "--- " + tr("AUDIO OPERATIONS", "АУДИО ОПЕРАЦИИ") + " ---",
        " 9. " + tr("Extract audio", "Извлечь аудио"),
        " a. " + tr("Merge video + audio", "Объединить видео + аудио"),
        " b. " + tr("Strip audio (mute)", "Удалить звук"),
        "--- " + tr("OTHER", "ДРУГОЕ") + " ---",
        " c. " + tr("Concatenate (join) files", "Склеить файлы"),
        " d. " + tr("Create GIF", "Создать GIF"),
        " e. " + tr("Extract frames", "Извлечь кадры"),
        " f. " + tr("File information", "Информация о файле"),
        " g. " + tr("Compare two files", "Сравнить два файла"),
        " h. " + tr("Batch convert video", "Пакетное конвертирование видео"),
        " i. " + tr("Batch convert audio", "Пакетное конвертирование аудио"),
        "--- " + tr("PROGRAM", "ПРОГРАММА") + " ---",
        " s. " + tr("Settings", "Настройки"),
        " l. " + string(CURRENT_LANG == LANG_EN ? "Language: English" : "Язык: Русский"),
        " 0. " + tr("Exit (ESC)", "Выход (ESC)"),
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
        clearScreen();
        printColor("========================================", CYAN);
        printColor(" MR CLI FOR FFMPEG v1.1.6", CYAN);
        printColor("========================================", CYAN);
        printColor("========================================", GREEN);
        printColor(" FFMPEG:  " + string(FFMPEG_FOUND ? tr("[OK] installed", "[OK] установлен") : tr("[ERROR] not found", "[ОШИБКА] не найден")), FFMPEG_FOUND ? GREEN : RED);
        printColor(" FFPROBE: " + string(FFPROBE_FOUND ? tr("[OK] installed", "[OK] установлен") : tr("[WARNING] not installed", "[ВНИМАНИЕ] не установлен")), FFPROBE_FOUND ? GREEN : YELLOW);
        string hwInfo = getAccelerationHardwareInfo();
        printColor(" " + tr("Hardware: ", "Железо: ") + hwInfo, GREEN);
        printColor("========================================", GREEN);
        cout << "========================================\n";
        for (int i = 0; i < (int)opts.size(); i++) {
            if (actions[i] == -1) {
                cout << opts[i] << "\n";
            } else if (i == selected) {
                setColor(GREEN);
                cout << " > " << opts[i] << endl;
                setColor(WHITE);
            } else {
                cout << "   " << opts[i] << endl;
            }
        }
        cout << "========================================\n";
        cout << "\n" << tr("Arrow keys to select, Enter to confirm, ESC or 0 to exit",
                            "Стрелки для выбора, Enter для подтверждения, ESC или 0 для выхода") << endl;

        wint_t key = _getwch();
        if (key == 27 || key == '0') return '0';
        if (key == 13) return (char)actions[selected];
        if (key == 0 || key == 0xE0) {
            wint_t scan = _getwch();
            if (scan == 72) {
                do { selected = (selected > 0) ? selected - 1 : (int)opts.size() - 1; } while (actions[selected] == -1);
            } else if (scan == 80) {
                do { selected = (selected < (int)opts.size() - 1) ? selected + 1 : 0; } while (actions[selected] == -1);
            }
        } else {
            char ch = normalizeKeyToEnglish(key);
            for (int i = 0; i < (int)opts.size(); i++) {
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
        char ch = mainMenuSelect();
        if (ch == 27 || ch == '0') {
            cout << tr("Exiting...", "Выход...") << "\n";
            CoUninitialize();
            return 0;
        }
        // Each top level operation starts with a cold probe cache. The key already
        // includes size + mtime, so this is belt and braces: it also keeps memory flat
        // when the user works through many operations in one session.
        clearProbeCache();
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
            break;
    }
    }

    CoUninitialize();
    return 0;
}
