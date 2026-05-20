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

/// 解析时间戳 "01-01 00:00:05.123" → 当天毫秒数
/// Android 日志通常是 MM-DD HH:MM:SS.mmm 格式
int64_t parseTimestampToMs(const std::string& ts_str) {
    // 只取 HH:MM:SS 部分, 忽略日期
    std::regex time_re(R"((\d{2}):(\d{2}):(\d{2})\.(\d{3}))");
    std::smatch m;
    if (std::regex_search(ts_str, m, time_re)) {
        int hh = std::stoi(m[1]);
        int mm = std::stoi(m[2]);
        int ss = std::stoi(m[3]);
        int ms = std::stoi(m[4]);
        return static_cast<int64_t>(hh) * 3600000
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
    // 检测是否包含 "healthd:" 关键字
    return line.find("healthd:") != std::string::npos;
}

std::optional<ChargerDataPoint> HealthdParser::parseLine(const std::string& line) const {
    if (!canParse(line)) {
        return std::nullopt;
    }

    ChargerDataPoint pt;

    // ── 提取原始时间戳 ─────────────────────────────────
    // Android logcat 时间戳通常在行首: "01-01 00:00:05.123 ..."
    // 也支持纯时间格式 "00:00:05.123" 或时间戳在前12个字符内
    std::regex ts_re(R"(^(\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}))");
    std::smatch ts_match;
    if (std::regex_search(line, ts_match, ts_re)) {
        pt.timestamp_str = ts_match[1];
        pt.elapsed_ms = parseTimestampToMs(pt.timestamp_str);
    }

    // ── 提取 healthd 关键字后的 k=v ─────────────────────
    auto pos = line.find("healthd:");
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    std::string after_healthd = line.substr(pos + 8);  // 跳过 "healthd:"
    auto kv = parseKeyValuePairs(after_healthd);

    // ── 字段映射 ──────────────────────────────────────
    auto readField = [&](const std::string& key) -> double {
        auto it = kv.find(key);
        if (it != kv.end()) return safeToDouble(it->second);
        return NAN;
    };

    pt.battery_voltage_mv = readField("v");
    pt.battery_temperature_c = readField("t");
    pt.battery_current_ma = readField("chg");
    pt.battery_level_pct = readField("l");

    // bus_voltage / bus_current: healthd 不提供, 保持 NAN

    return pt;
}

std::vector<std::string> HealthdParser::supportedFields() const {
    return {
        "battery_current_ma",
        "battery_voltage_mv",
        "battery_temperature_c",
        "battery_level_pct",
    };
}

}  // namespace chargerlog
