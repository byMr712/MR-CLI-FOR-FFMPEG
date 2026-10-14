#pragma once
#include <string>
#include <vector>
#include "../domain/Enums.hpp"
#include "../domain/TrackInfo.hpp"
#include "../domain/EncodingProblem.hpp"
#include "../domain/AppConfig.hpp"
#include "../services/FfprobeService.hpp"

namespace mr {
namespace ui {

struct EncodingDialogResult {
    int action = -1;
    bool applyToAll = false;
    std::string chosenFormat;
    bool autoAlignResolution = false;
    bool downscaleTo4K = false;
};

class Dialogs {
public:
    static bool selectVideoTrackForFile(const std::string& filePath,
                                        const services::FfprobeService& probe,
                                        std::string& mapArgs,
                                        bool allowAllTracks = true,
                                        bool isBatchMode = false,
                                        bool* outApplyToAllBatch = nullptr,
                                        int* outSelectedTrackIndex = nullptr);

    static bool selectAudioTrackForFile(const std::string& filePath,
                                        const services::FfprobeService& probe,
                                        std::string& mapArgs,
                                        bool allowAllTracks = true,
                                        bool isBatchMode = false,
                                        bool* outKeepAllForBatch = nullptr,
                                        int* outSelectedTrackIndex = nullptr,
                                        bool* outApplyToAllBatch = nullptr);

    static domain::SubtitleDialogResult dialogSubtitleIncompatibility(const std::string& filePath,
                                                                      const std::string& detectedSubTypes,
                                                                      bool batchMode = false);

    static EncodingDialogResult dialogEncodingProblem(const domain::EncodingProblem& ep, bool batchMode);

    static bool promptVideoCodecSettings(const domain::AppConfig& cfg);
    static std::string selectAudioCodecForOperation();
    static bool promptAudioCodecSettings(const domain::AppConfig& cfg, std::string& chosenCodec);
    static bool promptDeleteOriginal(bool isBatch, const std::string& deleteOriginalSetting, bool& outDelete);
    static void handleOriginalDeletion(const std::string& srcPath, const std::string& dstPath, bool isTemp, bool shouldDelete);
    static int askBatchPauseAction(const std::string& fileLabel);
};

} // namespace ui
} // namespace mr
