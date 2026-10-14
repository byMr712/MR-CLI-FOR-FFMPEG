#include "ProbeCacheService.hpp"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace mr {
namespace services {

ProbeCacheService& ProbeCacheService::instance() {
    static ProbeCacheService s_instance;
    return s_instance;
}

std::string ProbeCacheService::lastQuotedArg(const std::string& cmd) {
    size_t close = cmd.rfind('"');
    if (close == std::string::npos || close == 0) return "";
    size_t open = cmd.rfind('"', close - 1);
    if (open == std::string::npos) return "";
    return cmd.substr(open + 1, close - open - 1);
}

bool ProbeCacheService::commandRunsFfprobe(const std::string& cmd) {
    if (cmd.empty() || cmd[0] != '"') return false;
    size_t close = cmd.find('"', 1);
    if (close == std::string::npos) return false;
    std::string exe = cmd.substr(1, close - 1);
    std::string lower = exe;
    for (char& c : lower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return lower.find("ffprobe") != std::string::npos;
}

std::string ProbeCacheService::probedFileSignature(const std::string& path) {
    if (path.empty()) return "";
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    if (ec) return "?";
    auto mtime = fs::last_write_time(fs::u8path(path), ec);
    if (ec) return "?";
    char buf[96];
    snprintf(buf, sizeof(buf), "%llu|%lld",
             static_cast<unsigned long long>(size),
             static_cast<long long>(mtime.time_since_epoch().count()));
    return buf;
}

std::string ProbeCacheService::buildKey(const std::string& cmd) {
    return cmd + "\x01" + probedFileSignature(lastQuotedArg(cmd));
}

bool ProbeCacheService::get(const std::string& cmd, std::string& outResult) {
    if (!commandRunsFfprobe(cmd)) return false;
    std::string key = buildKey(cmd);
    for (const auto& item : m_cache) {
        if (item.first == key) {
            m_hits++;
            outResult = item.second;
            return true;
        }
    }
    return false;
}

void ProbeCacheService::put(const std::string& cmd, const std::string& output) {
    if (!commandRunsFfprobe(cmd)) return;
    if (m_cache.size() >= m_cacheLimit) {
        m_cache.erase(m_cache.begin());
    }
    m_cache.emplace_back(buildKey(cmd), output);
}

void ProbeCacheService::clear() {
    m_cache.clear();
    m_hits = 0;
}

} // namespace services
} // namespace mr
