#include "ShellDialogs.hpp"
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include <shobjidl.h>
#include <commdlg.h>

namespace mr {
namespace core {

std::string ShellDialogs::openFolderDialog(const std::wstring& title) {
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(hrCom);

    HWND hwndOwner = GetConsoleWindow();
    if (hwndOwner) {
        SetForegroundWindow(hwndOwner);
    }

    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr) && pfd) {
        DWORD dwOptions = 0;
        pfd->GetOptions(&dwOptions);
        pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        pfd->SetTitle(title.c_str());

        hr = pfd->Show(hwndOwner);
        if (SUCCEEDED(hr)) {
            IShellItem* psi = nullptr;
            hr = pfd->GetResult(&psi);
            if (SUCCEEDED(hr) && psi) {
                wchar_t* pPath = nullptr;
                hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
                psi->Release();
                pfd->Release();

                if (SUCCEEDED(hr) && pPath) {
                    std::wstring result(pPath);
                    CoTaskMemFree(pPath);
                    if (needUninit) CoUninitialize();
                    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
                        result += L'\\';
                    }
                    return StringUtil::wstringToUtf8(result);
                }
            }
        }
        pfd->Release();

        // If user explicitly clicked Cancel in IFileDialog, return empty
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == (HRESULT)0x800704C7) {
            if (needUninit) CoUninitialize();
            return "";
        }
    }

    // Fallback: classic Windows folder browser (SHBrowseForFolderW)
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwndOwner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH] = { 0 };
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            if (needUninit) CoUninitialize();
            std::wstring result(path);
            if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
                result += L'\\';
            }
            return StringUtil::wstringToUtf8(result);
        }
        CoTaskMemFree(pidl);
    }

    if (needUninit) CoUninitialize();
    return "";
}

std::string ShellDialogs::openFileDialogMedia(bool isRussian, const std::wstring& title) {
    OPENFILENAMEW ofn = { 0 };
    wchar_t fn[32768] = L"";
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetConsoleWindow();
    ofn.lpstrFilter = isRussian ?
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
    ofn.nMaxFile = static_cast<DWORD>(sizeof(fn) / sizeof(fn[0]));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    std::wstring defaultTitle = isRussian ? L"Выберите медиафайл" : L"Select media file";
    ofn.lpstrTitle = !title.empty() ? title.c_str() : defaultTitle.c_str();

    if (GetOpenFileNameW(&ofn)) {
        return StringUtil::wstringToUtf8(std::wstring(fn));
    }
    return "";
}

} // namespace core
} // namespace mr
