#pragma once
#include <string>
#include <string_view>
#include <cstdio>

namespace mr {
namespace core {

class NumberFormat {
public:
    [[nodiscard]] static bool parseNumberLoose(std::string_view s, double& out) noexcept {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) return false;

        bool negative = false;
        if (s[i] == '+' || s[i] == '-') {
            negative = (s[i] == '-');
            i++;
        }

        double value = 0.0;
        bool anyDigit = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value = value * 10.0 + (s[i] - '0');
            i++;
            anyDigit = true;
        }

        if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
            i++;
            double scale = 0.1;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
                value += (s[i] - '0') * scale;
                scale *= 0.1;
                i++;
                anyDigit = true;
            }
        }

        if (!anyDigit) return false;
        out = negative ? -value : value;
        return true;
    }

    [[nodiscard]] static double parseNumberLoose(std::string_view s) noexcept {
        double out = 0.0;
        (void)parseNumberLoose(s, out);
        return out;
    }

    [[nodiscard]] static double parseTimeStringToSeconds(std::string_view timeStr) noexcept {
        if (timeStr.empty()) return 0.0;
        int parts[3] = {0, 0, 0};
        int partCount = 0;
        double fracSec = 0.0;
        size_t start = 0;
        for (size_t i = 0; i <= timeStr.size(); i++) {
            if (i == timeStr.size() || timeStr[i] == ':') {
                std::string part(timeStr.substr(start, i - start));
                if (partCount < 3) {
                    double val = 0.0;
                    (void)parseNumberLoose(part, val);
                    if (partCount == 2 || (i == timeStr.size() && partCount > 0)) {
                        parts[partCount++] = (int)val;
                        fracSec = val - (int)val;
                    } else {
                        parts[partCount++] = (int)val;
                    }
                }
                start = i + 1;
            }
        }
        if (partCount == 1) {
            double s = 0.0;
            (void)parseNumberLoose(timeStr, s);
            return s;
        }
        if (partCount == 2) {
            return parts[0] * 60.0 + parts[1] + fracSec;
        }
        if (partCount == 3) {
            return parts[0] * 3600.0 + parts[1] * 60.0 + parts[2] + fracSec;
        }
        return 0.0;
    }

    [[nodiscard]] static std::string formatDot(double value, int decimals = 4) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        std::string s(buf);
        for (char& c : s) {
            if (c == ',') c = '.';
        }
        return s;
    }

    [[nodiscard]] static std::string sanitizeNumberArg(std::string_view input) {
        std::string out;
        out.reserve(input.size());
        for (char c : input) {
            if (c == ',') out += '.';
            else if ((c >= '0' && c <= '9') || c == '.' || c == '-') out += c;
        }
        return out;
    }

    [[nodiscard]] static std::string trimZeroDecimals(std::string s) {
        size_t dot = s.find('.');
        if (dot == std::string::npos) return s;
        while (s.size() > dot && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
};

} // namespace core
} // namespace mr

namespace core {
    using namespace mr::core;
}
