#pragma once
#include <string>

namespace mr {
namespace core {

class ShellDialogs {
public:
    static std::string openFolderDialog(const std::wstring& title = L"Select folder");
    static std::string openFileDialogMedia(bool isRussian = false, const std::wstring& title = L"");
};

} // namespace core
} // namespace mr
