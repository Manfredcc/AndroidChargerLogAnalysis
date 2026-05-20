#include "chargerlog/healthd_parser.h"
#include "chargerlog/base_charger_parser.h"
#include "chargerlog/project_config.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

using namespace chargerlog;

// ── Helper ──────────────────────────────────────────────────────────
// 避免编译器警告: 将 do{...}while(0) 赋值给变量无法编译
// 改用逗号表达式让结果正确
#define TEST(name)                                                  \
    (std::cout << "[TEST] " << #name << " ... ",                   \
     test_##name() ?                                                \
        (std::cout << "PASS" << std::endl, true) :                 \
        (std::cout << "FAIL" << std::endl, false))

#define ASSERT(cond, msg)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            std::cerr << "\n  FAIL: " << msg << std::endl;       \
            return false;                                       \
        }                                                       \
    } while (0)

// ── Tests ──────────────────────────────────────────────────────────

static bool test_can_parse_healthd_line() {
    HealthdParser parser;
    ASSERT(parser.canParse("01-01 00:00:05 I healthd: battery l=65 v=3920"), "healthd line");
    ASSERT(!parser.canParse("01-01 00:00:05 I kernel: random stuff"),  "non-healthd line");
    ASSERT(!parser.canParse(""),                                       "empty line");
    return true;
}

static bool test_parse_basic_healthd() {
    HealthdParser parser;
    std::string line = "01-01 00:00:05.123  1234  5678 I healthd: battery l=65 v=3920 t=28.5 h=2 st=2 c=1200 chg=450";

    auto opt = parser.parseLine(line);
    ASSERT(opt.has_value(), "should parse successfully");

    const auto& pt = opt.value();

    // 时间戳
    ASSERT(pt.timestamp_str == "01-01 00:00:05.123", "timestamp string");
    ASSERT(pt.elapsed_ms == 5123, "elapsed ms (5s + 123ms)");

    // 字段值
    ASSERT(std::abs(pt.battery_level_pct - 65.0) < 0.01,       "level = 65%");
    ASSERT(std::abs(pt.battery_voltage_mv - 3920.0) < 0.01,    "voltage = 3920 mV");
    ASSERT(std::abs(pt.battery_temperature_c - 28.5) < 0.01,   "temp = 28.5 C");
    ASSERT(std::abs(pt.battery_current_ma - 450.0) < 0.01,     "current = 450 mA");

    // healthd 不提供总线数据
    ASSERT(std::isnan(pt.bus_voltage_mv), "bus_voltage = NAN");
    ASSERT(std::isnan(pt.bus_current_ma), "bus_current = NAN");

    return true;
}

static bool test_parse_invalid_line() {
    HealthdParser parser;
    auto opt = parser.parseLine("this is not a healthd log line");
    ASSERT(!opt.has_value(), "invalid line returns nullopt");
    return true;
}

static bool test_parse_healthd_pid_format() {
    HealthdParser parser;
    // 实际日志格式: healthd(    0): 中间插了 PID
    std::string line = "04-06 14:59:52.490 W/.(7)[527:health@2.1-serv]healthd(    0): battery l=0 v=4151 t=30.8 h=2 st=3 c=-918900 fc=2946000 cc=0 chg=";

    auto opt = parser.parseLine(line);
    ASSERT(opt.has_value(), "healthd(pid): format should parse");

    const auto& pt = opt.value();
    ASSERT(pt.timestamp_str == "04-06 14:59:52.490", "timestamp");
    ASSERT(std::abs(pt.battery_level_pct - 0.0) < 0.01,  "level = 0%");
    ASSERT(std::abs(pt.battery_voltage_mv - 4151.0) < 0.01, "voltage = 4151 mV");
    ASSERT(std::abs(pt.battery_temperature_c - 30.8) < 0.01, "temp = 30.8 C");
    // chg= 空值 → 应返回 NAN 而非 crash
    ASSERT(std::isnan(pt.battery_current_ma), "chg=empty → NAN");

    return true;
}

static bool test_parse_partial_data() {
    HealthdParser parser;
    // 只包含部分字段
    std::string line = "healthd: battery v=4000 t=30.0";
    auto opt = parser.parseLine(line);
    ASSERT(opt.has_value(), "partial data should parse");

    const auto& pt = opt.value();
    ASSERT(std::abs(pt.battery_voltage_mv - 4000.0) < 0.01, "voltage = 4000");
    ASSERT(std::abs(pt.battery_temperature_c - 30.0) < 0.01, "temp = 30.0");
    ASSERT(std::isnan(pt.battery_current_ma), "current = NAN (missing)");
    ASSERT(std::isnan(pt.battery_level_pct), "level = NAN (missing)");

    return true;
}

static bool test_available_fields() {
    HealthdParser parser;
    auto fields = parser.supportedFields();

    ASSERT(!fields.empty(), "should have fields");
    bool has_voltage = false, has_current = false;
    for (const auto& f : fields) {
        if (f == "battery_voltage_mv") has_voltage = true;
        if (f == "battery_current_ma") has_current = true;
    }
    ASSERT(has_voltage, "voltage field");
    ASSERT(has_current, "current field");

    return true;
}

static bool test_data_point_has_get() {
    ChargerDataPoint pt;
    pt.battery_voltage_mv = 4000;

    ASSERT(pt.has("battery_voltage_mv"), "has voltage");
    ASSERT(!pt.has("bus_voltage_mv"), "no bus voltage (NAN)");
    ASSERT(std::abs(pt.get("battery_voltage_mv") - 4000.0) < 0.01, "get voltage");

    return true;
}

static bool test_stats_calculator() {
    std::vector<ChargerDataPoint> points;
    for (int i = 0; i < 10; i++) {
        ChargerDataPoint pt;
        pt.elapsed_ms = i * 1000;  // 每隔 1s
        pt.battery_voltage_mv = 3900.0 + i * 10;  // 3900, 3910, ... 3990
        pt.battery_level_pct = 50.0 + i * 2;       // 50, 52, ... 68
        points.push_back(pt);
    }

    auto stats = StatsCalculator::calcField(points, "battery_voltage_mv");
    ASSERT(stats.count == 10,          "count = 10");
    ASSERT(stats.min == 3900.0,        "min = 3900");
    ASSERT(stats.max == 3990.0,        "max = 3990");
    ASSERT(std::abs(stats.avg - 3945.0) < 0.01, "avg = 3945");

    // 时间窗口
    auto windowed = StatsCalculator::calcField(points, "battery_voltage_mv", 2000, 7000);
    ASSERT(windowed.count == 6,        "window count = 6 (2s ~ 7s)");
    ASSERT(windowed.min == 3920.0,     "window min = 3920");

    // calcAllFields
    auto all_stats = StatsCalculator::calcAllFields(points, {"battery_voltage_mv", "battery_level_pct"});
    ASSERT(all_stats.size() == 2, "all stats count = 2");
    ASSERT(all_stats[1].field_name == "battery_level_pct", "second field is level");

    return true;
}

static bool test_project_config() {
    ProjectConfigManager mgr;
    mgr.setConfig("230", "android_healthd");
    mgr.setConfig("傅里叶", "linux_xxx");

    ASSERT(mgr.getPlatform("230") == "android_healthd", "project 230 → android");
    ASSERT(mgr.getPlatform("傅里叶") == "linux_xxx",    "project 傅里叶 → linux");
    ASSERT(mgr.getPlatform("unknown").empty(),           "unknown project");

    // JSON 加载
    mgr.loadFromJson(R"({ "test1": "android_healthd", "test2": "linux_xxx" })");
    ASSERT(mgr.getPlatform("test1") == "android_healthd", "json load test1");
    ASSERT(mgr.getPlatform("test2") == "linux_xxx",       "json load test2");

    return true;
}

// ── Main ────────────────────────────────────────────────────────────

int main() {
    bool all_pass = true;

    all_pass &= TEST(can_parse_healthd_line);
    all_pass &= TEST(parse_basic_healthd);
    all_pass &= TEST(parse_invalid_line);
    all_pass &= TEST(parse_healthd_pid_format);
    all_pass &= TEST(parse_partial_data);
    all_pass &= TEST(available_fields);
    all_pass &= TEST(data_point_has_get);
    all_pass &= TEST(stats_calculator);
    all_pass &= TEST(project_config);

    std::cout << "\n==== " << (all_pass ? "ALL PASS" : "SOME FAILED") << " ====" << std::endl;
    return all_pass ? 0 : 1;
}
