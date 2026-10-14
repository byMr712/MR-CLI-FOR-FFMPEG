#include "CoverService.hpp"
#include "../core/WinLibs.hpp"
#include "../core/StringUtil.hpp"
#include "../core/PathProvider.hpp"
#include "../core/ProcessRunner.hpp"
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace mr {
namespace services {

CoverService::CoverService(const std::string& ffmpegPath, const std::string& ffprobePath)
    : m_ffmpegPath(ffmpegPath), m_ffprobePath(ffprobePath) {
}

EmbeddedCoverInfo CoverService::detectEmbeddedCover(const std::string& filePath) const {
    EmbeddedCoverInfo ci;
    if (m_ffprobePath.empty()) return ci;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -show_entries stream=index,codec_type,codec_name,disposition:stream_tags=filename,mimetype -of default \"" + filePath + "\"";
    std::string out = core::ProcessRunner::runCommand(cmd);
    if (out.empty()) return ci;

    std::istringstream iss(out);
    std::string line;

    int curIndex = -1;
    std::string curCodecType;
    std::string curCodecName;
    int curAttachedPic = 0;
    std::string curFilename;
    std::string curMime;

    auto checkCurrentStream = [&]() {
        if (curIndex < 0) return;
        std::string lowerFilename = curFilename;
        std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);
        std::string lowerMime = curMime;
        std::transform(lowerMime.begin(), lowerMime.end(), lowerMime.begin(), ::tolower);
        std::string lowerCodec = curCodecName;
        std::transform(lowerCodec.begin(), lowerCodec.end(), lowerCodec.begin(), ::tolower);

        // Case 1: Attachment stream (MKV)
        if (curCodecType == "attachment") {
            bool isImg = false;
            if (lowerMime.rfind("image/", 0) == 0) isImg = true;
            if (lowerFilename.find(".jpg") != std::string::npos || lowerFilename.find(".jpeg") != std::string::npos ||
                lowerFilename.find(".png") != std::string::npos || lowerFilename.find(".bmp") != std::string::npos ||
                lowerFilename.find(".webp") != std::string::npos) isImg = true;
            if (lowerCodec == "mjpeg" || lowerCodec == "png" || lowerCodec == "jpeg" || lowerCodec == "bmp") isImg = true;
            if (lowerFilename.find("cover") != std::string::npos || lowerFilename.find("poster") != std::string::npos ||
                lowerFilename.find("folder") != std::string::npos) isImg = true;

            if (isImg && !ci.found) {
                ci.found = true;
                ci.isAttachment = true;
                ci.streamIndex = curIndex;
                ci.filename = curFilename;
                ci.mimeType = curMime;
                if (lowerFilename.find(".png") != std::string::npos || lowerMime == "image/png" || lowerCodec == "png") {
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

    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        if (line == "[STREAM]") {
            curIndex = -1;
            curCodecType.clear();
            curCodecName.clear();
            curAttachedPic = 0;
            curFilename.clear();
            curMime.clear();
        } else if (line == "[/STREAM]") {
            checkCurrentStream();
        } else {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                if (k == "index") {
                    try { curIndex = std::stoi(v); } catch (...) {}
                } else if (k == "codec_type") {
                    curCodecType = v;
                } else if (k == "codec_name") {
                    curCodecName = v;
                } else if (k == "DISPOSITION:attached_pic") {
                    try { curAttachedPic = std::stoi(v); } catch (...) {}
                } else if (k == "TAG:filename") {
                    curFilename = v;
                } else if (k == "TAG:mimetype") {
                    curMime = v;
                }
            }
        }
    }
    checkCurrentStream();

    return ci;
}

bool CoverService::extractCover(const std::string& sourcePath, const std::string& outCoverPath) const {
    if (m_ffmpegPath.empty()) return false;
    EmbeddedCoverInfo ci = detectEmbeddedCover(sourcePath);
    if (ci.found) {
        if (ci.isAttachment) {
            std::wstring wCmd = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\"";
            wCmd += L" -loglevel quiet -nostats -dump_attachment:s:" + std::to_wstring(ci.streamIndex) + L" \"" + core::StringUtil::utf8ToWstring(outCoverPath) + L"\"";
            wCmd += L" -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(sourcePath)) + L"\" -y";
            (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd));
            if (core::PathProvider::fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;

            std::wstring wCmd2 = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\"";
            wCmd2 += L" -loglevel quiet -nostats -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(sourcePath)) + L"\"";
            wCmd2 += L" -map 0:" + std::to_wstring(ci.streamIndex) + L" -c copy -y \"" + core::StringUtil::utf8ToWstring(outCoverPath) + L"\"";
            (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd2));
            if (core::PathProvider::fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;
        } else {
            std::wstring wCmd = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\"";
            wCmd += L" -loglevel quiet -nostats -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(sourcePath)) + L"\"";
            wCmd += L" -map 0:" + std::to_wstring(ci.streamIndex) + L" -vframes 1 -c:v copy -y \"" + core::StringUtil::utf8ToWstring(outCoverPath) + L"\"";
            (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd));
            if (core::PathProvider::fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;
        }
    }

    // Fallback: extract first frame as thumbnail / cover
    std::wstring wCmd = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\"";
    wCmd += L" -loglevel quiet -nostats -ss 00:00:01 -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(sourcePath)) + L"\"";
    wCmd += L" -vframes 1 -q:v 2 -update 1 -y \"" + core::StringUtil::utf8ToWstring(outCoverPath) + L"\"";
    (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd));
    if (core::PathProvider::fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0) return true;

    std::wstring wCmd0 = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\"";
    wCmd0 += L" -loglevel quiet -nostats -ss 00:00:00 -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(sourcePath)) + L"\"";
    wCmd0 += L" -vframes 1 -q:v 2 -update 1 -y \"" + core::StringUtil::utf8ToWstring(outCoverPath) + L"\"";
    (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd0));
    return core::PathProvider::fileExists(outCoverPath) && fs::file_size(fs::u8path(outCoverPath)) > 0;
}

bool CoverService::buildCoverAttachArgs(const std::string& outputExt, const std::string& coverPath, std::wstring& argsOut) {
    std::string ext = outputExt;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    fs::path cp = fs::u8path(coverPath);
    std::string coverExt = cp.extension().u8string();
    std::transform(coverExt.begin(), coverExt.end(), coverExt.begin(), ::tolower);
    std::string mime = (coverExt == ".png") ? "image/png" : "image/jpeg";

    argsOut.clear();

    if (ext == ".mkv") {
        argsOut += L" -attach \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(coverPath)) + L"\"";
        argsOut += L" -metadata:s:t mimetype=\"" + core::StringUtil::utf8ToWstring(mime) + L"\"";
        argsOut += L" -metadata:s:t:0 filename=\"cover" + core::StringUtil::utf8ToWstring(coverExt.empty() ? ".jpg" : coverExt) + L"\"";
        return true;
    }

    bool isMp4Family = (ext == ".mp4" || ext == ".m4v" || ext == ".mov" || ext == ".m4a");
    bool isAudio = (ext == ".mp3" || ext == ".flac");
    if (!isMp4Family && !isAudio) return false;

    argsOut += L" -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(coverPath)) + L"\"";
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

bool CoverService::embedCoverIntoFile(const std::string& videoPath, const std::string& coverPath) const {
    if (!core::PathProvider::fileExists(videoPath) || !core::PathProvider::fileExists(coverPath) || m_ffmpegPath.empty()) {
        return false;
    }

    fs::path vp = fs::u8path(videoPath);
    std::string ext = vp.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::string tempOut = videoPath.substr(0, videoPath.length() - ext.length()) + "_tmp_cover" + ext;

    std::wstring attachArgs;
    if (!buildCoverAttachArgs(ext, coverPath, attachArgs)) return true;

    std::wstring wCmd = L"\"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(m_ffmpegPath)) + L"\" -loglevel quiet -nostats";
    wCmd += L" -i \"" + core::StringUtil::utf8ToWstring(core::PathProvider::getSafeFFmpegPath(videoPath)) + L"\"";
    wCmd += attachArgs;
    wCmd += L" -c copy -y \"" + core::StringUtil::utf8ToWstring(tempOut) + L"\"";

    (void)core::ProcessRunner::runCommand(core::StringUtil::wstringToUtf8(wCmd));

    if (core::PathProvider::fileExists(tempOut) && fs::file_size(fs::u8path(tempOut)) > 0) {
        std::wstring wTemp = core::StringUtil::utf8ToWstring(tempOut);
        std::wstring wTarget = core::StringUtil::utf8ToWstring(videoPath);
        DWORD tAttrs = GetFileAttributesW(wTarget.c_str());
        if (tAttrs != INVALID_FILE_ATTRIBUTES && (tAttrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(wTarget.c_str(), tAttrs & ~FILE_ATTRIBUTE_READONLY);
        }
        if (ReplaceFileW(wTarget.c_str(), wTemp.c_str(), NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
            return true;
        }

        DeleteFileW(wTarget.c_str());
        if (MoveFileExW(wTemp.c_str(), wTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }

        DeleteFileW(wTemp.c_str());
    }
    return false;
}

void CoverService::processCover(const std::string& inputPath, const std::string& outputPath, bool saveCoverEnabled) const {
    if (!saveCoverEnabled) return;
    if (!core::PathProvider::fileExists(outputPath) || inputPath.empty()) return;

    fs::path outP = fs::u8path(outputPath);
    std::string tempCover = outP.parent_path().u8string() + "\\.~mr_tmp_cover_" + outP.stem().u8string() + ".jpg";

    if (extractCover(inputPath, tempCover)) {
        embedCoverIntoFile(outputPath, tempCover);
        std::error_code ec;
        fs::remove(fs::u8path(tempCover), ec);
    }
}

} // namespace services
} // namespace mr
