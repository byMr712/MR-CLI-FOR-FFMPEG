#include "ShellDialogs.hpp"
#include "WinLibs.hpp"
#include "StringUtil.hpp"
#include <shobjidl.h>
#include <commdlg.h>

namespace mr {
namespace core {

std::string ShellDialogs::openFolderDialog(const std::wstring& title) {
    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (FAILED(hr)) return "";

    DWORD dwOptions = 0;
    pfd->GetOptions(&dwOptions);
    pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(title.c_str());

    hr = pfd->Show(nullptr);
    if (FAILED(hr)) {
        pfd->Release();
        return "";
    }

    IShellItem* psi = nullptr;
    hr = pfd->GetResult(&psi);
    if (FAILED(hr)) {
        pfd->Release();
        return "";
    }

    wchar_t* pPath = nullptr;
    hr = psi->GetDisplayName(SIGDN_FILESYSPATH, &pPath);
    psi->Release();
    pfd->Release();

    if (FAILED(hr) || !pPath) return "";

    std::wstring result(pPath);
    CoTaskMemFree(pPath);
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
        result += L'\\';
    }
    return StringUtil::wstringToUtf8(result);
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
