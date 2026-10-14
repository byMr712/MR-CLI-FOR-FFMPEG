#pragma once
#include <string>
#include "../core/WinLibs.hpp"

namespace mr {
namespace domain {

struct PairJob {
    PROCESS_INFORMATION pi = {};
    HANDLE hRead = INVALID_HANDLE_VALUE;
    std::string fileName;
    std::string pending;
    double currentSec = 0;
    double duration = 0;
    std::string speed;
    bool running = false;
    bool errorSeen = false;
    std::string lastError;
    DWORD exitCode = 1;
};

struct PairFingerprint {
    int realVideoStreams = 0;
    int audioTracks = 0;
    bool complexSubs = false;
    bool encodingProblem = false;
    bool oddDimensions = false;
    bool ultraHighRes = false;
    bool needsQuestion = false;
};

inline bool operator==(const PairFingerprint& a, const PairFingerprint& b) {
    return a.realVideoStreams == b.realVideoStreams &&
           a.audioTracks == b.audioTracks &&
           a.complexSubs == b.complexSubs &&
           a.encodingProblem == b.encodingProblem &&
           a.oddDimensions == b.oddDimensions &&
           a.ultraHighRes == b.ultraHighRes;
}

struct PairPrep {
    std::string nextPath;
    std::string nextStreamMapArg;
};

} // namespace domain
} // namespace mr
