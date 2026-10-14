#pragma once
#include <string>
#include <vector>
#include "TrackInfo.hpp"

namespace mr {
namespace domain {

struct AudioTrackPreference {
    bool hasPreference = false;
    bool keepAll = false;
    int preferredAudioIndex = -1;
    std::string preferredLanguage;
    std::string preferredTitle;
    std::string preferredCodec;
    std::string displayName;
};

struct VideoTrackPreference {
    bool hasPreference = false;
    bool keepAll = false;
    bool usePrimaryOnly = false;
    int preferredVideoIndex = -1;
    int preferredWidth = 0;
    int preferredHeight = 0;
    std::string preferredCodec;
    std::string preferredLanguage;
    std::string preferredTitle;
    std::string displayName;
};

struct BatchAudioMismatchWarning {
    std::string fileName;
    std::string preferredTrack;
    std::string actualTrack;
};

struct BatchVideoMismatchWarning {
    std::string fileName;
    std::string preferredTrack;
    std::string actualTrack;
};

struct ConflictedVideoFile {
    std::string filePath;
    size_t originalIndex = 0;
    std::vector<AudioTrack> tracks;
};

} // namespace domain
} // namespace mr
