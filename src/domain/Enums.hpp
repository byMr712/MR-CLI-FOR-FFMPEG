#pragma once

namespace mr {
namespace domain {

enum class Language {
    English = 0,
    Russian = 1,
    EN = 0,
    RU = 1
};

enum class AccelMode {
    CpuOnly = 0,        // Software CPU (libx264)
    CpuDecGpuEnc = 1,   // Hybrid (CPU decode, GPU encode)
    Nvidia = 2,         // Hardware NVIDIA (NVENC)
    Intel = 3,          // Hardware Intel (QSV)
    Amd = 4,            // Hardware AMD (AMF)
    GpuDecCpuEnc = 5,   // Reverse hybrid (GPU decode, CPU encode)

    CPU_ONLY = 0,
    CPU_DEC_GPU_ENC = 1,
    NVIDIA = 2,
    INTEL = 3,
    AMD = 4,
    GPU_DEC_CPU_ENC = 5
};

enum class SubtitleIncompatAction {
    ConvertText = 0,
    BurnHard = 1,
    DropSubs = 2,
    ChangeToMkv = 3,
    SkipCurrent = 4,
    CancelAll = 5,

    CONVERT_SRT = 0,
    HARDSUB = 1,
    DROP = 2,
    CANCEL = 5
};

enum class ProcessFileResult {
    Success = 0,
    Fail = 1,
    CancelBatch = 2,
    Skip = 3,
    PairHandled = 4,    // A pair finished inside worker; both files counted
    PairCancelled = 5   // A pair was interrupted and user cancelled batch
};

} // namespace domain
} // namespace mr
