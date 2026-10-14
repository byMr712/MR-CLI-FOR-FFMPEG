#include "LocalizationService.hpp"
#include "../core/WinLibs.hpp"

namespace mr {
namespace services {

LocalizationService& LocalizationService::instance() {
    static LocalizationService s_instance;
    return s_instance;
}

LocalizationService::LocalizationService() {
    detectSystemLanguage();
}

void LocalizationService::detectSystemLanguage() {
    WORD langId = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(langId);
    if (primary == LANG_RUSSIAN || primary == LANG_BELARUSIAN || primary == LANG_UKRAINIAN) {
        m_language = domain::Language::Russian;
    } else {
        m_language = domain::Language::English;
    }
}

std::string LocalizationService::translate(const std::string& en, const std::string& ru) const {
    return (m_language == domain::Language::Russian) ? ru : en;
}

} // namespace services
} // namespace mr
