#pragma once
#include <string>
#include "../domain/Enums.hpp"

namespace mr {
namespace services {

class LocalizationService {
public:
    static LocalizationService& instance();

    static domain::Language getLanguage() { return instance().m_language; }
    static void setLanguage(domain::Language lang) { instance().m_language = lang; }

    void detectSystemLanguage();

    std::string translate(const std::string& en, const std::string& ru) const;

    static std::string tr(const std::string& en, const std::string& ru) {
        return instance().translate(en, ru);
    }
    static domain::Language currentLanguage() { return instance().m_language; }

private:
    LocalizationService();
    domain::Language m_language = domain::Language::Russian;
};

// Convenience global helper
inline std::string tr(const std::string& en, const std::string& ru) {
    return LocalizationService::tr(en, ru);
}

} // namespace services
} // namespace mr
