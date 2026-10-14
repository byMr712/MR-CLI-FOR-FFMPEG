#pragma once
#include "WinLibs.hpp"
#include <string>

namespace mr {
namespace core {

struct ProcessResult {
    DWORD exitCode = 1;
    std::string stdOut;
    bool success = false;
};

class ProcessRunner {
public:
    [[nodiscard]] static std::string runCommand(const std::string& cmd);
    [[nodiscard]] static ProcessResult execute(const std::wstring& cmdLine, DWORD waitTimeoutMs = INFINITE);
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
