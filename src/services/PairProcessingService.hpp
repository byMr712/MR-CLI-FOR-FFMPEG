#pragma once
#include <string>
#include "../domain/PairProcessing.hpp"
#include "../domain/AppConfig.hpp"
#include "../domain/TrackPreference.hpp"
#include "FfprobeService.hpp"
#include "GpuDetector.hpp"

namespace mr {
namespace services {

class PairProcessingService {
public:
    static bool spawnJob(domain::PairJob& job, const std::wstring& cmdLine, const std::string& fileName, double duration);
    static void consumeOutput(domain::PairJob& job);
    static bool tryFinishJob(domain::PairJob& job);
    static void releaseJob(domain::PairJob& job);

    static std::string timeLabel(double seconds);
    static std::string statusLine(const domain::PairJob& job, int slot);
    static void renderStatus(const domain::PairJob& a, const domain::PairJob& b);
    static bool executePair(domain::PairJob& a, domain::PairJob& b, bool& outEscaped);

    static domain::PairFingerprint buildFingerprint(const std::string& filePath,
                                                    bool videoPrefStored,
                                                    bool audioPrefStored,
                                                    bool subtitlesLocked,
                                                    bool encodingLocked,
                                                    const domain::AppConfig& cfg,
                                                    const FfprobeService& probe,
                                                    const HardwareInfo& hw);

    static bool tryPrepare(const std::string& curPath,
                           const std::string& nextPath,
                           bool videoPrefStored,
                           bool audioPrefStored,
                           bool subsLocked,
                           bool encodingLocked,
                           const domain::VideoTrackPreference& vPref,
                           const domain::AudioTrackPreference& aPref,
                           const std::string& nextForcedAudioMap,
                           const domain::AppConfig& cfg,
                           const FfprobeService& probe,
                           const HardwareInfo& hw,
                           domain::PairPrep& outPrep);

    static bool printVerdict(bool enabled, bool paired, bool hasNext);
};

} // namespace services
} // namespace mr
