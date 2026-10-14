#pragma once
#include <string>
#include <vector>
#include "Enums.hpp"
#include "../core/NumberFormat.hpp"

namespace mr {
namespace domain {

struct VideoTrack {
    int index = -1;             // Global stream index from ffprobe
    int videoIndex = -1;        // Video stream index (0:v:N)
    std::string codec;          // h264, hevc, av1, vp9, mjpeg, etc.
    int width = 0;
    int height = 0;
    std::string fps;            // 24, 30, 60, etc.
    std::string pixFmt;         // yuv420p, etc.
    std::string bitRate;        // in bps
    std::string language;       // eng, rus, etc.
    std::string title;          // Track title
    bool isAttachedPic = false; // Cover art (DISPOSITION:attached_pic=1)

    bool isCoverOrAttachedPic() const {
        if (isAttachedPic) return true;
        if (codec == "mjpeg" || codec == "png" || codec == "bmp") {
            double f = 0;
            if (fps.empty() || !core::NumberFormat::parseNumberLoose(fps, f)) return true;
            return (f <= 0.0 || f >= 1000.0);
        }
        return false;
    }

    std::string getDisplayString(Language lang = Language::Russian) const {
        const bool isRu = (lang == Language::Russian);
        std::string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        std::string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (width > 0 && height > 0) {
            if (!details.empty()) details += ", ";
            details += std::to_string(width) + "x" + std::to_string(height);
        }
        if (!fps.empty()) {
            if (!details.empty()) details += ", ";
            details += fps + (isRu ? " кадр/с" : " fps");
        }
        if (!pixFmt.empty()) {
            if (!details.empty()) details += ", ";
            details += pixFmt;
        }
        if (!bitRate.empty()) {
            try {
                long long br = std::stoll(bitRate);
                if (br > 1000) {
                    if (!details.empty()) details += ", ";
                    details += std::to_string(br / 1000) + " kbps";
                }
            } catch (...) {}
        }
        if (isCoverOrAttachedPic()) {
            if (!details.empty()) details += ", ";
            details += (isRu ? "обложка" : "attached cover");
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (isRu ? "Видеопоток #" : "Video Stream #") + std::to_string(videoIndex + 1);
        }
        return res;
    }
};

struct AudioTrack {
    int index = -1;             // Global stream index from ffprobe
    int audioIndex = -1;        // Audio stream index (0:a:N)
    std::string codec;          // aac, mp3, ac3, eac3, flac, etc.
    std::string language;       // rus, eng, jpn, und, etc.
    std::string title;          // Track title / tag
    int channels = 0;           // 2, 6, etc.
    std::string channelLayout;  // stereo, 5.1, etc.
    std::string sampleRate;     // 48000, 44100, etc.
    std::string bitRate;        // in bps

    std::string getDisplayString(Language lang = Language::Russian) const {
        const bool isRu = (lang == Language::Russian);
        std::string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        std::string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (!channelLayout.empty()) {
            if (!details.empty()) details += ", ";
            details += channelLayout;
        } else if (channels > 0) {
            if (!details.empty()) details += ", ";
            details += std::to_string(channels) + (isRu ? " кан." : " ch");
        }
        if (!sampleRate.empty()) {
            if (!details.empty()) details += ", ";
            details += sampleRate + (isRu ? " Гц" : " Hz");
        }
        if (!bitRate.empty()) {
            try {
                long long br = std::stoll(bitRate);
                if (br > 1000) {
                    if (!details.empty()) details += ", ";
                    details += std::to_string(br / 1000) + " kbps";
                }
            } catch (...) {}
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (isRu ? "Аудиодорожка #" : "Audio Track #") + std::to_string(audioIndex + 1);
        }
        return res;
    }
};

struct SubtitleTrack {
    int index = -1;             // Global stream index from ffprobe
    int subIndex = -1;          // Subtitle stream index (0:s:N)
    std::string codec;          // subrip, ass, mov_text, etc.
    std::string language;       // rus, eng, jpn, und, etc.
    std::string title;          // Track title / tag
    bool isDefault = false;     // DISPOSITION:default=1
    bool isForced = false;      // DISPOSITION:forced=1

    std::string getDisplayString(Language lang = Language::Russian) const {
        const bool isRu = (lang == Language::Russian);
        std::string res;
        if (!language.empty() && language != "und") {
            res += "[" + language + "] ";
        }
        if (!title.empty()) {
            res += title + " ";
        }
        std::string details;
        if (!codec.empty()) {
            details += codec;
        }
        if (isDefault) {
            if (!details.empty()) details += ", ";
            details += (isRu ? "по умолчанию" : "default");
        }
        if (isForced) {
            if (!details.empty()) details += ", ";
            details += (isRu ? "форсированные" : "forced");
        }
        if (!details.empty()) {
            res += "(" + details + ")";
        }
        if (res.empty()) {
            res = (isRu ? "Субтитры #" : "Subtitles #") + std::to_string(subIndex + 1);
        }
        return res;
    }
};

struct VideoSourceProperties {
    std::string pixFmt;
    std::string codecName;
    int width = 0;
    int height = 0;
    std::string fps;
    std::string fieldOrder;
    bool isInterlaced = false;
    bool is10bit = false;
    bool isSubsampled422 = false;
    bool isSubsampled444 = false;
};

} // namespace domain
} // namespace mr
