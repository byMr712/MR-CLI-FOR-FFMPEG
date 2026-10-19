#include "FfprobeService.hpp"
#include "LocalizationService.hpp"
#include "../core/ProcessRunner.hpp"
#include "../core/NumberFormat.hpp"
#include "../core/StringUtil.hpp"
#include "ProbeCacheService.hpp"
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace mr {
namespace services {

static std::string runProbe(const std::string& cmd) {
    std::string out;
    if (ProbeCacheService::instance().get(cmd, out)) {
        return out;
    }
    out = core::ProcessRunner::runCommand(cmd);
    ProbeCacheService::instance().put(cmd, out);
    return out;
}

FfprobeService::FfprobeService(const std::string& ffprobePath)
    : m_ffprobePath(ffprobePath) {
}

std::vector<domain::VideoTrack> FfprobeService::getVideoTracks(const std::string& filePath) const {
    std::vector<domain::VideoTrack> tracks;
    if (m_ffprobePath.empty()) return tracks;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -select_streams v -show_entries stream=index,codec_name,width,height,r_frame_rate,pix_fmt,bit_rate,disposition:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    std::istringstream iss(output);
    std::string line;
    domain::VideoTrack cur;
    int currentVideoIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.videoIndex = currentVideoIdx++;
            if (cur.isCoverOrAttachedPic()) cur.isAttachedPic = true;
            tracks.push_back(cur);
            cur = domain::VideoTrack();
            inTrack = false;
        }
    };

    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = std::stoi(val); } catch (...) { cur.index = -1; }
        } else {
            inTrack = true;
            if (key == "codec_name") cur.codec = val;
            else if (key == "width") try { cur.width = std::stoi(val); } catch (...) {}
            else if (key == "height") try { cur.height = std::stoi(val); } catch (...) {}
            else if (key == "pix_fmt") cur.pixFmt = val;
            else if (key == "bit_rate") cur.bitRate = val;
            else if (key == "r_frame_rate") {
                size_t slash = val.find('/');
                if (slash != std::string::npos) {
                    double num = 0, den = 0;
                    if (core::NumberFormat::parseNumberLoose(val.substr(0, slash), num) &&
                        core::NumberFormat::parseNumberLoose(val.substr(slash + 1), den)) {
                        if (den > 0) {
                            cur.fps = core::NumberFormat::trimZeroDecimals(core::NumberFormat::formatDot(num / den, 2));
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

std::vector<domain::AudioTrack> FfprobeService::getAudioTracks(const std::string& filePath) const {
    std::vector<domain::AudioTrack> tracks;
    if (m_ffprobePath.empty()) return tracks;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -select_streams a -show_entries stream=index,codec_name,channels,channel_layout,sample_rate,bit_rate:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    std::istringstream iss(output);
    std::string line;
    domain::AudioTrack cur;
    int currentAudioIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.audioIndex = currentAudioIdx++;
            tracks.push_back(cur);
            cur = domain::AudioTrack();
            inTrack = false;
        }
    };

    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = std::stoi(val); } catch (...) { cur.index = -1; }
        } else {
            inTrack = true;
            if (key == "codec_name") cur.codec = val;
            else if (key == "channels") try { cur.channels = std::stoi(val); } catch (...) {}
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

std::vector<domain::SubtitleTrack> FfprobeService::getSubtitleTracks(const std::string& filePath) const {
    std::vector<domain::SubtitleTrack> tracks;
    if (m_ffprobePath.empty()) return tracks;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -select_streams s -show_entries stream=index,codec_name,disposition:stream_tags=language,title -of default=noprint_wrappers=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    std::istringstream iss(output);
    std::string line;
    domain::SubtitleTrack cur;
    int currentSubIdx = 0;
    bool inTrack = false;

    auto pushCurrent = [&]() {
        if (inTrack) {
            cur.subIndex = currentSubIdx++;
            tracks.push_back(cur);
            cur = domain::SubtitleTrack();
            inTrack = false;
        }
    };

    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "index") {
            pushCurrent();
            inTrack = true;
            try { cur.index = std::stoi(val); } catch (...) { cur.index = -1; }
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

domain::VideoSourceProperties FfprobeService::getVideoProperties(const std::string& filePath) const {
    domain::VideoSourceProperties vp;
    if (m_ffprobePath.empty()) return vp;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -select_streams v:0 -show_entries stream=codec_name,pix_fmt,width,height,r_frame_rate,field_order -of default=noprint_wrappers=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "codec_name") vp.codecName = val;
        else if (key == "pix_fmt") vp.pixFmt = val;
        else if (key == "width") try { vp.width = std::stoi(val); } catch (...) {}
        else if (key == "height") try { vp.height = std::stoi(val); } catch (...) {}
        else if (key == "r_frame_rate") vp.fps = val;
        else if (key == "field_order") {
            vp.fieldOrder = val;
            if (val == "tt" || val == "bb" || val == "tb" || val == "bt") vp.isInterlaced = true;
        }
    }

    std::string lower = vp.pixFmt;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    vp.is10bit = (lower.find("10") != std::string::npos || lower.find("p010") != std::string::npos || lower.find("p016") != std::string::npos);
    vp.isSubsampled422 = (lower.find("422") != std::string::npos);
    vp.isSubsampled444 = (lower.find("444") != std::string::npos);

    return vp;
}

double FfprobeService::getMediaDuration(const std::string& filePath) const {
    if (m_ffprobePath.empty()) return 0;

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    while (!output.empty() && (output.back() == '\r' || output.back() == '\n' || output.back() == ' ')) {
        output.pop_back();
    }
    double dur = 0;
    if (!core::NumberFormat::parseNumberLoose(output, dur)) return 0;
    return dur;
}

domain::MediaProperties FfprobeService::parseMediaProperties(const std::string& filePath) const {
    domain::MediaProperties mp;
    mp.path = filePath;

    std::error_code ec;
    auto fsize = fs::file_size(fs::u8path(filePath), ec);
    if (!ec) {
        mp.sizeBytes = static_cast<double>(fsize);
        char buf[64];
        if (fsize > 1024ULL * 1024 * 1024) {
            snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(fsize) / (1024.0 * 1024.0 * 1024.0));
        } else if (fsize > 1024 * 1024) {
            snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(fsize) / (1024.0 * 1024.0));
        } else {
            snprintf(buf, sizeof(buf), "%.2f KB", static_cast<double>(fsize) / 1024.0);
        }
        mp.sizeStr = buf;
    }

    if (m_ffprobePath.empty()) return mp;

    mp.videoTracks = getVideoTracks(filePath);
    mp.audioTracks = getAudioTracks(filePath);
    mp.subtitleTracks = getSubtitleTracks(filePath);

    std::string cmd = "\"" + m_ffprobePath + "\" -v quiet -show_entries format=format_name,duration,bit_rate,nb_streams -of default=noprint_wrappers=1 \"" + filePath + "\"";
    std::string output = runProbe(cmd);

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "format_name") {
            mp.format = domain::cleanFormatName(val);
        } else if (key == "duration") {
            double dur = 0;
            if (core::NumberFormat::parseNumberLoose(val, dur)) {
                mp.durationSec = dur;
                int h = static_cast<int>(dur / 3600);
                int m = static_cast<int>((dur - h * 3600) / 60);
                int s = static_cast<int>(dur) % 60;
                char buf[64];
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
                mp.durationStr = buf;
            }
        } else if (key == "bit_rate") {
            double br = 0;
            if (core::NumberFormat::parseNumberLoose(val, br)) {
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
        int total = static_cast<int>(mp.videoTracks.size() + mp.audioTracks.size() + mp.subtitleTracks.size());
        if (total > 0) mp.nbStreams = std::to_string(total);
    }

    const domain::VideoTrack* mainVideo = nullptr;
    for (const auto& vt : mp.videoTracks) {
        if (!vt.isCoverOrAttachedPic()) {
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
            mp.width = std::to_string(mainVideo->width);
            mp.height = std::to_string(mainVideo->height);
            mp.resolution = mp.width + "x" + mp.height;
        }
        mp.fps = mainVideo->fps;
        mp.pixFmt = mainVideo->pixFmt;
        if (!mainVideo->bitRate.empty()) {
            double br = 0;
            if (core::NumberFormat::parseNumberLoose(mainVideo->bitRate, br)) {
                mp.videoBitrateVal = br;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                mp.videoBitrateStr = buf;
            }
        }
    }

    if (!mp.audioTracks.empty()) {
        const auto& at = mp.audioTracks[0];
        mp.audioCodec = at.codec;
        if (!at.channelLayout.empty()) {
            mp.channels = at.channelLayout;
        } else if (at.channels > 0) {
            mp.channels = std::to_string(at.channels) + " ch";
        }
        mp.sampleRate = at.sampleRate;
        if (!at.bitRate.empty()) {
            double br = 0;
            if (core::NumberFormat::parseNumberLoose(at.bitRate, br)) {
                mp.audioBitrateVal = br;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.0f kbps", br / 1000.0);
                mp.audioBitrateStr = buf;
            }
        }
    }

    return mp;
}

std::string FfprobeService::formatMediaPropertiesDisplay(const domain::MediaProperties& mp, domain::Language lang) const {
    const bool isRu = (lang == domain::Language::Russian);
    std::string res;

    res += (isRu ? "  Формат: " : "  Format: ") + (mp.format.empty() ? (isRu ? "Неизвестно" : "Unknown") : mp.format) + "\n";
    if (mp.durationSec > 0 || !mp.durationStr.empty()) {
        char buf[128];
        int h = static_cast<int>(mp.durationSec / 3600);
        int m = static_cast<int>((mp.durationSec - h * 3600) / 60);
        int s = static_cast<int>(mp.durationSec) % 60;
        snprintf(buf, sizeof(buf), isRu ? "  Длительность: %02d:%02d:%02d (%.1f сек)\n" : "  Duration: %02d:%02d:%02d (%.1f sec)\n", h, m, s, mp.durationSec);
        res += buf;
    }
    if (!mp.sizeStr.empty()) {
        res += (isRu ? "  Размер: " : "  Size: ") + mp.sizeStr + "\n";
    }

    std::vector<const domain::VideoTrack*> realVideos;
    std::vector<const domain::VideoTrack*> covers;
    for (const auto& vt : mp.videoTracks) {
        if (vt.isCoverOrAttachedPic()) {
            covers.push_back(&vt);
        } else {
            realVideos.push_back(&vt);
        }
    }

    if (!realVideos.empty()) {
        res += "\n" + std::string(isRu ? "  [Видеопотоки]" : "  [Video Streams]") + "\n";
        for (size_t i = 0; i < realVideos.size(); ++i) {
            res += "    " + std::string(isRu ? "Поток " : "Stream ") + std::to_string(i + 1) + ": " + realVideos[i]->getDisplayString(lang) + "\n";
        }
    }

    if (!covers.empty()) {
        res += "\n" + std::string(isRu ? "  [Обложка]" : "  [Cover Art]") + "\n";
        for (size_t i = 0; i < covers.size(); ++i) {
            res += "    " + std::string(isRu ? "Встроенная обложка: " : "Embedded cover: ") + covers[i]->getDisplayString(lang) + "\n";
        }
    }

    if (!mp.audioTracks.empty()) {
        res += "\n" + std::string(isRu ? "  [Аудиодорожки]" : "  [Audio Tracks]") + "\n";
        for (size_t i = 0; i < mp.audioTracks.size(); ++i) {
            res += "    " + std::string(isRu ? "Дорожка " : "Track ") + std::to_string(i + 1) + ": " + mp.audioTracks[i].getDisplayString(lang) + "\n";
        }
    }

    if (!mp.subtitleTracks.empty()) {
        res += "\n" + std::string(isRu ? "  [Субтитры]" : "  [Subtitle Tracks]") + "\n";
        for (size_t i = 0; i < mp.subtitleTracks.size(); ++i) {
            res += "    " + std::string(isRu ? "Субтитры " : "Subtitle ") + std::to_string(i + 1) + ": " + mp.subtitleTracks[i].getDisplayString(lang) + "\n";
        }
    }

    return res;
}

std::string FfprobeService::buildStreamMapArgs(const std::string& videoMapArg, const std::string& audioMapArg) {
    if (videoMapArg.empty() && audioMapArg.empty()) return "";
    if (!videoMapArg.empty() && audioMapArg.empty()) return videoMapArg + " -map 0:a?";
    if (videoMapArg.empty() && !audioMapArg.empty()) return " -map 0:v:0?" + audioMapArg;
    return videoMapArg + audioMapArg;
}

int FfprobeService::matchAudioPreference(const std::vector<domain::AudioTrack>& tracks, const domain::AudioTrackPreference& pref) {
    if (pref.keepAll) return 9999;
    int matchIdx = -1;
    if (!pref.preferredLanguage.empty() && pref.preferredLanguage != "und") {
        for (size_t t = 0; t < tracks.size(); ++t) {
            if (!tracks[t].language.empty() && _stricmp(tracks[t].language.c_str(), pref.preferredLanguage.c_str()) == 0) {
                matchIdx = static_cast<int>(t);
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredTitle.empty()) {
        for (size_t t = 0; t < tracks.size(); ++t) {
            if (!tracks[t].title.empty() &&
                (tracks[t].title.find(pref.preferredTitle) != std::string::npos ||
                 pref.preferredTitle.find(tracks[t].title) != std::string::npos)) {
                matchIdx = static_cast<int>(t);
                break;
            }
        }
    }
    if (matchIdx == -1 && pref.preferredLanguage.empty() && pref.preferredTitle.empty()) {
        if (pref.preferredAudioIndex >= 0 && pref.preferredAudioIndex < static_cast<int>(tracks.size())) {
            matchIdx = pref.preferredAudioIndex;
        }
    }
    return matchIdx;
}

int FfprobeService::matchVideoPreference(const std::vector<domain::VideoTrack>& vTracks, const std::vector<int>& realVideoIndices, const domain::VideoTrackPreference& pref) {
    if (pref.keepAll) return 9999;
    if (pref.usePrimaryOnly) return realVideoIndices.empty() ? 0 : realVideoIndices[0];
    int matchIdx = -1;
    if (pref.preferredWidth > 0 && pref.preferredHeight > 0) {
        for (size_t t = 0; t < vTracks.size(); ++t) {
            if (vTracks[t].width == pref.preferredWidth && vTracks[t].height == pref.preferredHeight) {
                matchIdx = static_cast<int>(t);
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredLanguage.empty() && pref.preferredLanguage != "und") {
        for (size_t t = 0; t < vTracks.size(); ++t) {
            if (!vTracks[t].language.empty() && _stricmp(vTracks[t].language.c_str(), pref.preferredLanguage.c_str()) == 0) {
                matchIdx = static_cast<int>(t);
                break;
            }
        }
    }
    if (matchIdx == -1 && !pref.preferredTitle.empty()) {
        for (size_t t = 0; t < vTracks.size(); ++t) {
            if (!vTracks[t].title.empty() &&
                (vTracks[t].title.find(pref.preferredTitle) != std::string::npos ||
                 pref.preferredTitle.find(vTracks[t].title) != std::string::npos)) {
                matchIdx = static_cast<int>(t);
                break;
            }
        }
    }
    if (matchIdx == -1 && pref.preferredVideoIndex >= 0 && pref.preferredVideoIndex < static_cast<int>(vTracks.size())) {
        matchIdx = pref.preferredVideoIndex;
    }
    return matchIdx;
}

void FfprobeService::recordVideoPreference(domain::VideoTrackPreference& pref, int selectedTrackIdx, const std::vector<domain::VideoTrack>& vTracks) {
    pref.hasPreference = true;
    if (selectedTrackIdx == -3) {
        pref.keepAll = true;
        pref.usePrimaryOnly = false;
        pref.displayName = tr("All video streams (All videos in batch)", "Все видеопотоки (для всех видео в пакете)");
    } else if (selectedTrackIdx == -1) {
        pref.keepAll = false;
        pref.usePrimaryOnly = true;
        pref.displayName = tr("Primary video stream (All videos in batch)", "Основной видеопоток (для всех видео в пакете)");
    } else if (selectedTrackIdx >= 0 && selectedTrackIdx < static_cast<int>(vTracks.size())) {
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

void FfprobeService::recordAudioPreference(domain::AudioTrackPreference& pref, int selectedTrackIdx, bool keepAllForBatch, const std::vector<domain::AudioTrack>& tracks) {
    if (keepAllForBatch || selectedTrackIdx == -1) {
        pref.hasPreference = true;
        pref.keepAll = true;
        pref.displayName = tr("All audio tracks (All videos in batch)", "Все аудиодорожки (для всех видео в пакете)");
    } else if (selectedTrackIdx >= 0 && selectedTrackIdx < static_cast<int>(tracks.size())) {
        pref.hasPreference = true;
        pref.keepAll = false;
        const auto& trk = tracks[selectedTrackIdx];
        pref.preferredAudioIndex = trk.audioIndex;
        pref.preferredLanguage = trk.language;
        pref.preferredTitle = trk.title;
        pref.preferredCodec = trk.codec;
        pref.displayName = trk.getDisplayString() + tr(" (All videos in batch)", " (для всех видео в пакете)");
    }
}

} // namespace services
} // namespace mr
