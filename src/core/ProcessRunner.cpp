#include "ProcessRunner.hpp"
#include "UniqueHandle.hpp"
#include "StringUtil.hpp"
#include <vector>

namespace mr {
namespace core {

std::string ProcessRunner::runCommand(const std::string& cmd) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
    UniqueHandle hReadPipe(hRead);
    UniqueHandle hWritePipe(hWrite);

    SetHandleInformation(hReadPipe.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe.get();
    si.hStdError = hWritePipe.get();
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring wcmd = utf8ToWstring(cmd);
    std::vector<wchar_t> mutableCmd(wcmd.begin(), wcmd.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return "";
    }

    hWritePipe.reset(); // Parent closes write end immediately so ReadFile sees EOF

    UniqueHandle hProcess(pi.hProcess);
    UniqueHandle hThread(pi.hThread);

    std::string output;
    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe.get(), buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }

    WaitForSingleObject(hProcess.get(), INFINITE);
    return output;
}

ProcessResult ProcessRunner::execute(const std::wstring& cmdLine, DWORD waitTimeoutMs) {
    ProcessResult res;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return res;
    UniqueHandle hReadPipe(hRead);
    UniqueHandle hWritePipe(hWrite);

    SetHandleInformation(hReadPipe.get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe.get();
    si.hStdError = hWritePipe.get();

    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi
    );

    hWritePipe.reset();

    if (!created) return res;

    UniqueHandle hProcess(pi.hProcess);
    UniqueHandle hThread(pi.hThread);

    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe.get(), buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        res.stdOut.append(buf, bytesRead);
    }

    WaitForSingleObject(hProcess.get(), waitTimeoutMs);
    GetExitCodeProcess(hProcess.get(), &res.exitCode);
    res.success = (res.exitCode == 0);
    return res;
}

} // namespace core
} // namespace mr
