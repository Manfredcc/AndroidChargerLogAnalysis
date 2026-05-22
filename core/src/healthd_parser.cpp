#include "chargerlog/healthd_parser.h"

#include <charconv>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace chargerlog {

namespace {

/// 解析 healthd key=value 行的工具函数
/// 输入: "battery l=65 v=3920 t=28.5 h=2 st=2 c=1200 chg=450"
/// 输出: map {l→65, v→3920, ...}
std::unordered_map<std::string, std::string> parseKeyValuePairs(const std::string& body) {
    std::unordered_map<std::string, std::string> kv;
    std::regex re(R"((\w+)=([^\s]*))");
    auto begin = std::sregex_iterator(body.begin(), body.end(), re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        kv[(*it)[1]] = (*it)[2];
    }
    return kv;
}

/// 解析时间戳 "04-06 09:33:59.273" → 年内毫秒数 (自 Jan 1 00:00:00.000 起)
/// Android 日志通常是 MM-DD HH:MM:SS.mmm 格式
int64_t parseTimestampToMs(const std::string& ts_str) {
    std::regex re(R"((\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})\.(\d{3}))");
    std::smatch m;
    if (std::regex_search(ts_str, m, re)) {
        int month = std::stoi(m[1]);
        int day   = std::stoi(m[2]);
        int hh    = std::stoi(m[3]);
        int mm    = std::stoi(m[4]);
        int ss    = std::stoi(m[5]);
        int ms    = std::stoi(m[6]);

        static const int days_before[] = {
            0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
        };
        int day_of_year = days_before[month - 1] + (day - 1);

        return static_cast<int64_t>(day_of_year) * 86400000LL
             + static_cast<int64_t>(hh) * 3600000
             + static_cast<int64_t>(mm) * 60000
             + static_cast<int64_t>(ss) * 1000
             + ms;
    }
    return 0;
}

/// 安全地将字符串转为 double
double safeToDouble(const std::string& s) {
    if (s.empty()) return NAN;
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec == std::errc()) return val;
    return NAN;
}

}  // anonymous namespace

std::string HealthdParser::platformName() const {
    return "android_healthd";
}

bool HealthdParser::canParse(const std::string& line) const {
    // 匹配 healthd(pid): 或 healthd: 两种格式，且必须包含 battery
    auto pos = line.find("healthd");
    if (pos == std::string::npos) return false;
    // healthd 之后必须出现 "battery"，排除 ramoops 中的非电池行 (如 "healthd peak:...")
    return line.find("battery", pos) != std::string::npos;
}

std::optional<ChargerDataPoint> HealthdParser::parseLine(const std::string& line) const {
    if (!canParse(line)) {
        return std::nullopt;
    }

    ChargerDataPoint pt;

    // ── 提取原始时间戳 ─────────────────────────────────
    std::regex ts_re(R"(^(\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}))");
    std::smatch ts_match;
    if (!std::regex_search(line, ts_match, ts_re)) {
        return std::nullopt;  // 无有效时间戳，跳过此数据点
    }
    pt.timestamp_str = ts_match[1];
    pt.elapsed_ms = parseTimestampToMs(pt.timestamp_str);

    // ── 提取 healthd 后第一个冒号后面的 k=v ────────────
    // 兼容: "healthd: battery ..." 和 "healthd(    0): battery ..."
    auto healthd_pos = line.find("healthd");
    if (healthd_pos == std::string::npos) {
        return std::nullopt;
    }

    auto colon_pos = line.find(':', healthd_pos);
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    std::string after_colon = line.substr(colon_pos + 1);
    auto kv = parseKeyValuePairs(after_colon);

    // ── 字段映射 ──────────────────────────────────────
    auto readField = [&](const std::string& key) -> double {
        auto it = kv.find(key);
        if (it != kv.end()) return safeToDouble(it->second);
        return NAN;
    };

    pt.battery_voltage_mv = readField("v");
    pt.battery_temperature_c = readField("t");
    pt.battery_level_pct = readField("l");

    // 电池电流: c 字段, 单位 µA, 转为 mA
    double current_ua = readField("c");
    if (!std::isnan(current_ua)) {
        pt.battery_current_ma = current_ua / 1000.0;
    }

    pt.charge_cycle_count = readField("cc");

    // bus_voltage / bus_current: healthd 不提供, 保持 NAN

    return pt;
}

std::vector<std::string> HealthdParser::supportedFields() const {
    return {
        "battery_current_ma",
        "battery_voltage_mv",
        "battery_temperature_c",
        "battery_level_pct",
        "charge_cycle_count",
    };
}

}  // namespace chargerlog
