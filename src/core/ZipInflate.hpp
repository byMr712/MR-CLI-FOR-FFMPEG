#pragma once
#include <string>
#include <functional>

namespace mr {
namespace core {

class ZipInflate {
public:
    using ProgressCallback = std::function<void(double percent, const std::string& info)>;

    [[nodiscard]] static bool extractZip(
        const std::string& zipPath,
        const std::string& destDir,
        ProgressCallback onProgress = nullptr
    );

    [[nodiscard]] static bool extract(
        const std::string& zipPath,
        const std::string& destDir,
        const std::string& componentName = "",
        const std::string& statusMsg = ""
    ) {
        return extractZip(zipPath, destDir);
    }
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
