#pragma once
#include <string>
#include <vector>
#include <utility>

namespace mr {
namespace services {

class ProbeCacheService {
public:
    static ProbeCacheService& instance();

    bool get(const std::string& cmd, std::string& outResult);
    void put(const std::string& cmd, const std::string& output);
    void clear();
    int getHits() const { return m_hits; }

    static std::string lastQuotedArg(const std::string& cmd);
    static bool commandRunsFfprobe(const std::string& cmd);
    static std::string probedFileSignature(const std::string& path);
    static std::string buildKey(const std::string& cmd);

private:
    ProbeCacheService() = default;
    std::vector<std::pair<std::string, std::string>> m_cache;
    size_t m_cacheLimit = 512;
    int m_hits = 0;
};

} // namespace services
} // namespace mr
