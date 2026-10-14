#pragma once
#include <string>
#include <vector>
#include "../domain/TrackInfo.hpp"
#include "FfprobeService.hpp"

namespace mr {
namespace services {

struct ExactTrimCodecs {
    std::string videoArgs;
    std::string audioArgs;
    std::string videoDesc;
    std::string audioDesc;
    std::string videoCodecArgs;
    std::string pixelFormatArgs;
    std::string audioCodecArgs;
};

struct ExactTrimMultiArgs {
    std::string videoArgs;
    std::string audioArgs;
};

class ExactTrimService {
public:
    static bool exactKeepPixelFormat(const std::string& pixFmtLower);
    static std::string exactVideoEncoderOpts(const std::string& videoCodec, std::string* descOut);
    static std::string exactAudioEncoderOpts(const std::string& audioCodec, const std::string& audioBitRateBps, std::string* descOut);

    static ExactTrimCodecs buildCodecs(const std::string& videoCodec,
                                       const std::string& videoPixFmt,
                                       const std::string& audioCodec,
                                       const std::string& audioBitRateBps);

    static ExactTrimCodecs buildExactTrimCodecs(const std::string& videoCodec,
                                                const std::string& videoPixFmt,
                                                const std::string& audioCodec,
                                                const std::string& audioBitRateBps) {
        return buildCodecs(videoCodec, videoPixFmt, audioCodec, audioBitRateBps);
    }

    static ExactTrimCodecs buildExactTrimCodecs(const std::string& filePath,
                                                int selectedVideoIdx,
                                                const std::string& forcedAudioCodec,
                                                const FfprobeService& probe) {
        auto vt = probe.getVideoTracks(filePath);
        auto at = probe.getAudioTracks(filePath);
        std::string vc = "h264", vpf = "yuv420p";
        if (selectedVideoIdx >= 0 && selectedVideoIdx < static_cast<int>(vt.size())) {
            vc = vt[selectedVideoIdx].codec;
            vpf = vt[selectedVideoIdx].pixFmt;
        } else if (!vt.empty()) {
            vc = vt[0].codec;
            vpf = vt[0].pixFmt;
        }
        std::string ac = forcedAudioCodec.empty() ? "aac" : forcedAudioCodec;
        std::string abr = "192000";
        if (!at.empty()) {
            if (forcedAudioCodec.empty() || forcedAudioCodec == "copy") ac = at[0].codec;
            abr = at[0].bitRate;
        }
        return buildCodecs(vc, vpf, ac, abr);
    }

    static ExactTrimMultiArgs buildMultiArgs(const std::vector<domain::VideoTrack>& vtracks,
                                             const std::vector<domain::AudioTrack>& atracks);

    static ExactTrimMultiArgs buildExactTrimMultiArgs(const std::vector<domain::VideoTrack>& vtracks,
                                                      const std::vector<domain::AudioTrack>& atracks) {
        return buildMultiArgs(vtracks, atracks);
    }

    static double getTrimPreRoll(const std::string& filePath,
                                 const std::string& videoMapArg,
                                 double timeSec,
                                 const FfprobeService& probe,
                                 const std::string& ffmpegPath);

    static void printTrimLengthReport(double requested, double actual, const std::string& startLabel, bool exactMode);
};

} // namespace services
} // namespace mr
