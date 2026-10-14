#pragma once
#include <string>

namespace mr {
namespace services {

struct EmbeddedCoverInfo {
    bool found = false;
    bool isAttachment = false; // true if MKV attachment stream
    int streamIndex = -1;
    std::string filename;
    std::string mimeType;
    std::string ext = "jpg";   // "jpg" or "png"
};

class CoverService {
public:
    explicit CoverService(const std::string& ffmpegPath = "", const std::string& ffprobePath = "");

    void setPaths(const std::string& ffmpegPath, const std::string& ffprobePath) {
        m_ffmpegPath = ffmpegPath;
        m_ffprobePath = ffprobePath;
    }

    EmbeddedCoverInfo detectEmbeddedCover(const std::string& filePath) const;
    bool extractCover(const std::string& sourcePath, const std::string& outCoverPath) const;
    static bool buildCoverAttachArgs(const std::string& outputExt, const std::string& coverPath, std::wstring& argsOut);
    bool embedCoverIntoFile(const std::string& videoPath, const std::string& coverPath) const;
    void processCover(const std::string& inputPath, const std::string& outputPath, bool saveCoverEnabled) const;

private:
    std::string m_ffmpegPath;
    std::string m_ffprobePath;
};

} // namespace services
} // namespace mr
