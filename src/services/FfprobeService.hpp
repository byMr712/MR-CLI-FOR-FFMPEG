#pragma once
#include <string>
#include <vector>
#include "../domain/Enums.hpp"
#include "../domain/TrackInfo.hpp"
#include "../domain/TrackPreference.hpp"
#include "../domain/MediaProperties.hpp"
#include "ProbeCacheService.hpp"
#include "LocalizationService.hpp"

namespace mr {
namespace services {

class FfprobeService {
public:
    explicit FfprobeService(const std::string& ffprobePath = "");

    void setFfprobePath(const std::string& path) { m_ffprobePath = path; }
    const std::string& getFfprobePath() const { return m_ffprobePath; }
    bool isAvailable() const { return !m_ffprobePath.empty(); }
    void clearCache() const { ProbeCacheService::instance().clear(); }

    std::vector<domain::VideoTrack> getVideoTracks(const std::string& filePath) const;
    std::vector<domain::AudioTrack> getAudioTracks(const std::string& filePath) const;
    std::vector<domain::SubtitleTrack> getSubtitleTracks(const std::string& filePath) const;
    domain::VideoSourceProperties getVideoProperties(const std::string& filePath) const;
    double getMediaDuration(const std::string& filePath) const;
    domain::MediaProperties parseMediaProperties(const std::string& filePath) const;
    std::string formatMediaPropertiesDisplay(const domain::MediaProperties& mp, domain::Language lang) const;
    std::string formatMediaPropertiesDisplay(const domain::MediaProperties& mp) const {
        return formatMediaPropertiesDisplay(mp, LocalizationService::getLanguage());
    }

    static std::string buildStreamMapArgs(const std::string& videoMapArg, const std::string& audioMapArg);
    static int matchAudioPreference(const std::vector<domain::AudioTrack>& tracks, const domain::AudioTrackPreference& pref);
    static int matchVideoPreference(const std::vector<domain::VideoTrack>& vTracks, const std::vector<int>& realVideoIndices, const domain::VideoTrackPreference& pref);
    static int matchVideoPreference(const std::vector<domain::VideoTrack>& vTracks, const domain::VideoTrackPreference& pref) {
        std::vector<int> real;
        for (size_t i = 0; i < vTracks.size(); i++) {
            if (!vTracks[i].isCoverOrAttachedPic()) real.push_back(static_cast<int>(i));
        }
        return matchVideoPreference(vTracks, real, pref);
    }

    static void recordVideoPreference(domain::VideoTrackPreference& pref, int selectedTrackIdx, const std::vector<domain::VideoTrack>& vTracks);
    static void recordVideoPreference(int selectedTrackIdx, const std::vector<domain::VideoTrack>& vTracks, domain::VideoTrackPreference& pref) {
        recordVideoPreference(pref, selectedTrackIdx, vTracks);
    }

    static void recordAudioPreference(domain::AudioTrackPreference& pref, int selectedTrackIdx, bool keepAllForBatch, const std::vector<domain::AudioTrack>& tracks);
    static void recordAudioPreference(int selectedTrackIdx, const std::vector<domain::AudioTrack>& tracks, domain::AudioTrackPreference& pref) {
        recordAudioPreference(pref, selectedTrackIdx, false, tracks);
    }

private:
    std::string m_ffprobePath;
};

} // namespace services
} // namespace mr
