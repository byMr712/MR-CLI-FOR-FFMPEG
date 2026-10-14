#pragma once
#include <string>
#include <vector>
#include "TrackInfo.hpp"

namespace mr {
namespace domain {

struct MediaProperties {
    std::string path;
    std::string format;
    std::string durationStr;
    double durationSec = 0;
    std::string sizeStr;
    double sizeBytes = 0;
    std::string bitrateStr;
    double bitrateVal = 0;

    // Primary video summary
    std::string videoCodec;
    std::string width;
    std::string height;
    std::string resolution;
    std::string fps;
    std::string pixFmt;
    std::string videoBitrateStr;
    double videoBitrateVal = 0;

    // Primary audio summary
    std::string audioCodec;
    std::string sampleRate;
    std::string channels;
    std::string audioBitrateStr;
    double audioBitrateVal = 0;

    // Stream counts & lists
    std::string nbStreams;
    std::vector<VideoTrack> videoTracks;
    std::vector<AudioTrack> audioTracks;
    std::vector<SubtitleTrack> subtitleTracks;
};

inline std::string cleanFormatName(const std::string& raw) {
    std::string lower = raw;
    for (char& c : lower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    if (lower.find("matroska") != std::string::npos && lower.find("webm") != std::string::npos) return "Matroska/WebM";
    if (lower.find("matroska") != std::string::npos) return "Matroska (MKV)";
    if (lower.find("webm") != std::string::npos) return "WebM";
    if (lower.find("mp4") != std::string::npos) return "MP4";
    if (lower.find("avi") != std::string::npos) return "AVI";
    if (lower.find("mov") != std::string::npos) return "MOV";
    if (lower.find("flac") != std::string::npos) return "FLAC";
    if (lower.find("mp3") != std::string::npos) return "MP3";
    if (lower.find("ogg") != std::string::npos) return "OGG";
    if (lower.find("wav") != std::string::npos) return "WAV";
    if (lower.find("3gp") != std::string::npos) return "3GP";
    if (lower.find("mpegts") != std::string::npos) return "MPEG-TS";
    if (lower.find("mpeg") != std::string::npos) return "MPEG";
    if (lower.find("m4a") != std::string::npos) return "M4A";
    return raw;
}

} // namespace domain
} // namespace mr
