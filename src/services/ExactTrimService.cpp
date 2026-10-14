#include "ExactTrimService.hpp"
#include "LocalizationService.hpp"
#include "../core/Console.hpp"
#include "../core/NumberFormat.hpp"
#include "../core/ProcessRunner.hpp"
#include <algorithm>

namespace mr {
namespace services {

static std::string toLowerAscii(const std::string& str) {
    std::string out = str;
    for (char& c : out) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool ExactTrimService::exactKeepPixelFormat(const std::string& pixFmtLower) {
    static const char* keepable[] = {
        "yuv420p", "yuvj420p", "yuv420p10le", "yuv422p", "yuv422p10le",
        "yuv444p", "yuv444p10le", "gray", "yuv440p"
    };
    for (size_t i = 0; i < sizeof(keepable) / sizeof(keepable[0]); ++i) {
        if (pixFmtLower == keepable[i]) return true;
    }
    return false;
}

std::string ExactTrimService::exactVideoEncoderOpts(const std::string& videoCodec, std::string* descOut) {
    std::string vc = toLowerAscii(videoCodec);
    if (vc == "h264")                      { if (descOut) *descOut = "H.264 (libx264, CRF 18)";                return "libx264 -crf 18"; }
    else if (vc == "hevc" || vc == "h265") { if (descOut) *descOut = "H.265 (libx265, CRF 20)";                return "libx265 -crf 20"; }
    else if (vc == "vp9")                   { if (descOut) *descOut = "VP9 (libvpx-vp9, CRF 30)";              return "libvpx-vp9 -crf 30 -b:v 0"; }
    else if (vc == "vp8")                   { if (descOut) *descOut = "VP8 (libvpx, CRF 10)";                  return "libvpx -crf 10 -b:v 0"; }
    else if (vc == "av1")                   { if (descOut) *descOut = "AV1 (libsvtav1, CRF 30)";               return "libsvtav1 -crf 30"; }
    else if (vc == "mpeg2video")            { if (descOut) *descOut = "MPEG-2 (q 2)";                          return "mpeg2video -q:v 2"; }
    else if (vc == "mpeg4")                 { if (descOut) *descOut = "MPEG-4 Part 2 (q 2)";                   return "mpeg4 -q:v 2"; }
    else if (vc == "mjpeg")                 { if (descOut) *descOut = "MJPEG (q 2)";                           return "mjpeg -q:v 2"; }
    else if (vc == "theora")                { if (descOut) *descOut = "Theora (q 7)";                         return "libtheora -q:v 7"; }
    else if (vc == "prores")                { if (descOut) *descOut = "ProRes (profile 3)";                    return "prores -profile:v 3"; }
    else if (vc == "ffv1")                  { if (descOut) *descOut = "FFV1 (lossless)";                       return "ffv1"; }
    else                                    { if (descOut) *descOut = tr("H.264 (libx264, CRF 18) - the source codec is not recognised",
                                                                         "H.264 (libx264, CRF 18) - кодек источника не распознан"); return "libx264 -crf 18"; }
}

std::string ExactTrimService::exactAudioEncoderOpts(const std::string& audioCodec, const std::string& audioBitRateBps, std::string* descOut) {
    std::string ac = toLowerAscii(audioCodec);

    std::string bitrate = "192k";
    double bps = 0;
    if (core::NumberFormat::parseNumberLoose(audioBitRateBps, bps) && bps >= 32000 && bps <= 2000000) {
        int kbps = static_cast<int>(bps / 1000 + 0.5);
        bitrate = std::to_string(kbps) + "k";
    }

    if (ac.compare(0, 4, "pcm_") == 0)      { if (descOut) *descOut = ac + " (PCM, без пережатия)";                          return ac; }
    else if (ac == "aac")                    { if (descOut) *descOut = "AAC " + bitrate;                                       return "aac -b:a " + bitrate; }
    else if (ac == "ac3")                    { if (descOut) *descOut = "AC3 " + bitrate;                                       return "ac3 -b:a " + bitrate; }
    else if (ac == "eac3")                   { if (descOut) *descOut = "E-AC3 " + bitrate;                                      return "eac3 -b:a " + bitrate; }
    else if (ac == "mp3" || ac == "mp3float"){ if (descOut) *descOut = "MP3 " + bitrate;                                       return "libmp3lame -b:a " + bitrate; }
    else if (ac == "mp2" || ac == "mp2float"){ if (descOut) *descOut = "MP2 " + bitrate;                                       return "mp2 -b:a " + bitrate; }
    else if (ac == "opus")                   { if (descOut) *descOut = "Opus " + bitrate;                                      return "libopus -b:a " + bitrate; }
    else if (ac == "vorbis")                 { if (descOut) *descOut = "Vorbis (q 6)";                                         return "libvorbis -q:a 6"; }
    else if (ac == "flac")                   { if (descOut) *descOut = tr("FLAC (lossless)", "FLAC (без потерь)");            return "flac"; }
    else if (ac == "alac")                   { if (descOut) *descOut = "ALAC (lossless)";                                      return "alac"; }
    else if (ac == "wavpack")                { if (descOut) *descOut = "WavPack";                                              return "wavpack"; }
    else if (ac == "truehd")                 { if (descOut) *descOut = "TrueHD";                                               return "truehd"; }
    else if (ac == "dts")                    { if (descOut) *descOut = "DTS (DCA)";                                            return "dca"; }
    else                                     { if (descOut) *descOut = "AAC " + bitrate + tr(" - the source codec is not recognised",
                                                                                             " - кодек источника не распознан"); return "aac -b:a " + bitrate; }
}

ExactTrimCodecs ExactTrimService::buildCodecs(const std::string& videoCodec,
                                              const std::string& videoPixFmt,
                                              const std::string& audioCodec,
                                              const std::string& audioBitRateBps) {
    ExactTrimCodecs r;
    std::string pf = toLowerAscii(videoPixFmt);
    r.videoCodecArgs = "-c:v " + exactVideoEncoderOpts(videoCodec, &r.videoDesc);
    r.videoArgs = r.videoCodecArgs;
    if (exactKeepPixelFormat(pf)) {
        r.pixelFormatArgs = "-pix_fmt " + pf;
        r.videoArgs += " " + r.pixelFormatArgs;
    }
    r.audioArgs = "-c:a " + exactAudioEncoderOpts(audioCodec, audioBitRateBps, &r.audioDesc);
    return r;
}

ExactTrimMultiArgs ExactTrimService::buildMultiArgs(const std::vector<domain::VideoTrack>& vtracks,
                                                    const std::vector<domain::AudioTrack>& atracks) {
    ExactTrimMultiArgs m;
    for (size_t i = 0; i < vtracks.size(); ++i) {
        std::string slot = "-c:v:" + std::to_string(static_cast<int>(i));
        if (vtracks[i].isCoverOrAttachedPic()) {
            m.videoArgs += " " + slot + " copy";
            continue;
        }
        std::string desc;
        m.videoArgs += " " + slot + " " + exactVideoEncoderOpts(vtracks[i].codec, &desc);
        std::string pf = toLowerAscii(vtracks[i].pixFmt);
        if (exactKeepPixelFormat(pf)) m.videoArgs += " -pix_fmt:" + std::to_string(static_cast<int>(i)) + " " + pf;
    }
    for (size_t i = 0; i < atracks.size(); ++i) {
        std::string desc;
        m.audioArgs += " -c:a:" + std::to_string(static_cast<int>(i)) + " "
                     + exactAudioEncoderOpts(atracks[i].codec, atracks[i].bitRate, &desc);
    }
    return m;
}

double ExactTrimService::getTrimPreRoll(const std::string& filePath,
                                        const std::string& videoMapArg,
                                        double timeSec,
                                        const FfprobeService& probe,
                                        const std::string& ffmpegPath) {
    if (probe.getFfprobePath().empty() || timeSec <= 0) return -1;
    const double WINDOW = 120.0;
    double from = timeSec - WINDOW;
    if (from < 0) from = 0;

    std::string sel = "v:0";
    size_t p = videoMapArg.find("0:v:");
    if (p != std::string::npos) {
        size_t q = videoMapArg.find_first_not_of("0123456789", p + 4);
        std::string num = videoMapArg.substr(p + 4, (q == std::string::npos ? videoMapArg.size() : q) - (p + 4));
        if (!num.empty()) sel = "v:" + num;
    }

    std::string cmd = "\"" + probe.getFfprobePath() + "\" -v error -read_intervals " +
                      core::NumberFormat::formatDot(from, 3) + "%" +
                      core::NumberFormat::formatDot(timeSec, 3) + " -select_streams " + sel +
                      " -show_entries packet=pts_time,flags -of csv=p=0 \"" + filePath + "\"";

    std::string out = core::ProcessRunner::runCommand(cmd);
    double best = -1;
    size_t i = 0;
    while (i < out.size()) {
        size_t eol = out.find('\n', i);
        if (eol == std::string::npos) eol = out.size();
        std::string line = out.substr(i, eol - i);
        i = eol + 1;
        size_t c = line.find(',');
        if (c == std::string::npos) continue;
        double pts = 0;
        if (!core::NumberFormat::parseNumberLoose(line.substr(0, c), pts)) continue;
        if (pts > timeSec + 0.001) continue;
        if (line.find('K', c + 1) == std::string::npos) continue;
        if (pts > best) best = pts;
    }
    if (best < 0) return -1;
    return timeSec - best;
}

void ExactTrimService::printTrimLengthReport(double requested, double actual, const std::string& startLabel, bool exactMode) {
    if (actual <= 0) return;
    double extra = actual - requested;
    bool longer = extra > 0.25;

    core::Console::print("\n" + tr("Requested: ", "Запрошено: ") + core::NumberFormat::formatDot(requested, 3) + tr(" sec", " сек")
                         + tr("   Actual: ", "   Получилось: ") + core::NumberFormat::formatDot(actual, 3) + tr(" sec\n", " сек\n"),
                         longer ? core::ConsoleColor::YELLOW : core::ConsoleColor::GREEN);

    if (exactMode) {
        if (longer) {
            core::Console::print(tr("[NOTE] The cut is still longer than requested. This should not happen\n"
                                    "       in exact mode - please report it.\n",
                                    "[ПРИМЕЧАНИЕ] Обрезка всё ещё длиннее запрошенного. В точном режиме\n"
                                    "       так быть не должно - сообщите об этом, пожалуйста.\n"),
                                 core::ConsoleColor::YELLOW);
        }
        return;
    }
    if (!longer) return;

    core::Console::print(tr("[NOTE] The cut could not start exactly at ", "[ПРИМЕЧАНИЕ] Обрезка не смогла начаться точно в ")
                         + startLabel + tr(", so it started at the preceding keyframe.\n",
                                           ", поэтому началась с предыдущего ключевого кадра.\n"),
                         core::ConsoleColor::YELLOW);
    core::Console::print(tr("       The file is ", "       Файл длиннее на ") + core::NumberFormat::formatDot(extra, 3)
                         + tr(" sec: that extra time is content from BEFORE the requested start.\n",
                              " сек: это фрагмент ДО запрошенного начала.\n"),
                         core::ConsoleColor::YELLOW);
}

} // namespace services
} // namespace mr
