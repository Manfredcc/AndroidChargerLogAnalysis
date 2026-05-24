/// CLI 入口 — chargerlog 命令行可执行文件
/// 用法: chargerlog <日志目录>
///
/// 扫描目录下所有日志文件, 使用 HealthdParser 解析,
/// 输出最高电池电压和最高电池温度。

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "chargerlog/healthd_parser.h"
#include "chargerlog/base_charger_parser.h"
#include "chargerlog/stats_calculator.h"
#include "chargerlog/parser_factory.h"
#include "chargerlog/cache_manager.h"

namespace fs = std::filesystem;
using namespace chargerlog;

// ── Windows 中文字符路径修复 ────────────────────────────────────────
#ifdef _WIN32
#include <windows.h>
/// 将命令行参数字符串 (系统 ANSI 编码, 如 GBK) 转为 fs::path (UTF-16)
static fs::path path_from_arg(const char* arg) {
    int len = MultiByteToWideChar(CP_ACP, 0, arg, -1, nullptr, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, arg, -1, &wstr[0], len);
    wstr.resize(len - 1);
    return fs::path(wstr);
}
#else
static fs::path path_from_arg(const char* arg) {
    return fs::path(arg);
}
#endif

// ── 工具函数 ───────────────────────────────────────────────────────

/// 判断文件是否可能是文本 (跳过二进制文件)
static bool likely_text_file(const fs::path& path) {
    // 按扩展名初步过滤
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // 常见的日志/文本扩展名
    static const char* text_exts[] = {
        ".log", ".txt", ".gz", ".out", ".dmesg", ".kmsg", ".cat", ".dump"
    };
    // .gz 也会出现在这里 — 但注意我们的目录已经过 decompressor 处理,
    // 子目录中的 .gz 已被解压, 所以这里不会遇到 .gz

    for (const char* e : text_exts) {
        if (ext == e) return true;
    }

    // 无扩展名或短扩展名的也尝试
    if (ext.empty() || ext.size() > 5) {
        return true;  // 无扩展名可能是 Linux 风格日志文件
    }

    return false;
}

/// 读取文件所有行, 通过 parser 解析, 收集数据点
static void collect_points(
    const fs::path& file_path,
    const BaseParser& parser,
    std::vector<ChargerDataPoint>& out_points,
    bool quiet = false) {

    // 跳过缓存文件自身
    if (file_path.filename() == ".chargerlog_cache") {
        return;
    }

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "  [跳过] 无法打开: " << file_path.u8string() << std::endl;
        return;
    }

    std::string line;
    int line_count = 0;
    int parsed_count = 0;

    while (std::getline(file, line)) {
        line_count++;
        auto opt = parser.parseLine(line);
        if (opt.has_value()) {
            out_points.push_back(std::move(opt.value()));
            parsed_count++;
        }
    }

    if (!quiet) {
        std::cout << "  " << file_path.filename().u8string()
                  << ": " << line_count << " 行, "
                  << parsed_count << " 个充电数据点" << std::endl;
    }
}

/// 递归扫描目录
static void scan_directory(
    const fs::path& dir_path,
    const BaseParser& parser,
    std::vector<ChargerDataPoint>& out_points,
    bool quiet = false) {

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        std::cerr << "错误: 目录不存在或不是目录: " << dir_path.u8string() << std::endl;
        return;
    }

    if (!quiet) {
        std::cout << "\n扫描目录: " << dir_path.u8string() << std::endl;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        if (!likely_text_file(path)) continue;

        collect_points(path, parser, out_points, quiet);
    }
}

// ── 字段显示信息 ──────────────────────────────────────────────

struct FieldDisplay {
    const char* name;   // ChargerDataPoint 字段名
    const char* label;  // 中文显示名
    const char* unit;   // 单位
};

static const FieldDisplay kAllFields[] = {
    {"battery_voltage_mv",    "电池电压", "mV"},
    {"battery_current_ma",    "电池电流", "mA"},
    {"battery_temperature_c", "电池温度", "°C"},
    {"battery_level_pct",     "电池电量", "%" },
    {"bus_voltage_mv",        "VBUS电压", "mV"},
    {"bus_current_ma",        "VBUS电流", "mA"},
};

// ── 主入口 ─────────────────────────────────────────────────────────

static void setup_console() {
#ifdef _WIN32
    // 让 Windows 终端正确显示 UTF-8 中文
    SetConsoleOutputCP(CP_UTF8);
#endif
}

/// 解析 HH:MM:SS 为当日毫秒数
static int64_t parseTimeArg(const std::string& s) {
    int hh, mm, ss;
    char c1, c2;
    std::istringstream iss(s);
    if (iss >> hh >> c1 >> mm >> c2 >> ss && c1 == ':' && c2 == ':') {
        return static_cast<int64_t>(hh) * 3600000
             + static_cast<int64_t>(mm) * 60000
             + static_cast<int64_t>(ss) * 1000;
    }
    return -1;
}

/// JSON 字符串转义
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else { out += static_cast<char>(c); }
    }
    return out;
}

/// 将 double 转为 JSON 兼容字符串 (NAN → "null")
static std::string jsonDouble(double v) {
    if (std::isnan(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

/// 将年内毫秒数格式化为 MM-DD HH:MM:SS
static std::string msToHMS(int64_t ms) {
    if (ms < 0) return "";
    if (ms > 2147483647000LL) return "";  // 无限制标记

    static const int days_before[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
    };
    static const int64_t ms_per_day = 86400000LL;

    int doy = static_cast<int>(ms / ms_per_day);
    int64_t tod = ms % ms_per_day;

    int month = 12;
    for (int m = 1; m <= 12; m++) {
        if (doy < days_before[m]) { month = m; break; }
    }
    int day = doy - days_before[month - 1] + 1;

    int h = static_cast<int>(tod / 3600000);
    int m = static_cast<int>((tod % 3600000) / 60000);
    int s = static_cast<int>((tod % 60000) / 1000);
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d:%02d", month, day, h, m, s);
    return buf;
}

int main(int argc, char* argv[]) {
    setup_console();

    // ── 参数解析 ───────────────────────────────────────────
    bool no_cache = false;
    bool json_mode = false;
    bool points_mode = false;
    size_t downsample_count = 0;
    int64_t start_ms = 0;
    int64_t end_ms = INT64_MAX;
    std::string platform_name = "android_healthd";
    fs::path log_dir;

    struct ThresholdArg { std::string field; double value; };
    std::vector<ThresholdArg> cli_thresholds;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-cache") {
            no_cache = true;
        } else if (arg == "--json") {
            json_mode = true;
        } else if (arg == "--points") {
            points_mode = true;
            json_mode = true;
        } else if (arg == "--downsample" && i + 1 < argc) {
            i++;
            downsample_count = static_cast<size_t>(std::stoull(argv[i]));
        } else if (arg == "--start" && i + 1 < argc) {
            i++;
            start_ms = parseTimeArg(argv[i]);
            if (start_ms < 0) {
                std::cerr << "错误: --start 需要 HH:MM:SS 格式" << std::endl;
                return 1;
            }
        } else if (arg == "--end" && i + 1 < argc) {
            i++;
            end_ms = parseTimeArg(argv[i]);
            if (end_ms < 0) {
                std::cerr << "错误: --end 需要 HH:MM:SS 格式" << std::endl;
                return 1;
            }
        } else if (arg == "--threshold" && i + 1 < argc) {
            i++;
            std::string spec = argv[i];
            auto eq_pos = spec.find('=');
            if (eq_pos == std::string::npos) {
                std::cerr << "错误: --threshold 需要 field=value 格式" << std::endl;
                return 1;
            }
            ThresholdArg ta;
            ta.field = spec.substr(0, eq_pos);
            ta.value = std::stod(spec.substr(eq_pos + 1));
            cli_thresholds.push_back(ta);
        } else if (arg == "--platform" && i + 1 < argc) {
            i++;
            platform_name = argv[i];
        } else if (arg[0] != '-') {
            log_dir = path_from_arg(argv[i]);
        } else {
            std::cerr << "用法: chargerlog [--platform name] [--json] [--points] [--downsample N] [--no-cache] [--start HH:MM:SS] [--end HH:MM:SS] [--threshold field=value]... <日志目录>" << std::endl;
            return 1;
        }
    }

    if (log_dir.empty()) {
        std::cerr << "用法: chargerlog [--platform name] [--json] [--points] [--downsample N] [--no-cache] [--start HH:MM:SS] [--end HH:MM:SS] [--threshold field=value]... <日志目录>" << std::endl;
        return 1;
    }

    // ── 创建解析器 (根据 --platform 配置) ───────────────────
    auto parser = ParserFactory::create(platform_name);
    if (!parser) {
        std::cerr << "错误: 未知平台 '" << platform_name << "'" << std::endl;
        std::cerr << "可用平台: ";
        bool first = true;
        for (const auto& p : ParserFactory::availablePlatforms()) {
            if (!first) std::cerr << ", ";
            std::cerr << p;
            first = false;
        }
        std::cerr << std::endl;
        return 1;
    }

    std::vector<ChargerDataPoint> all_points;
    bool from_cache = false;

    // 缓存查找
    uint64_t cache_fp = 0;
    if (!no_cache) {
        cache_fp = CacheManager::computeFingerprint(log_dir);
        if (cache_fp != 0) {
            auto cached = CacheManager::load(
                log_dir / CacheManager::DEFAULT_CACHE_FILENAME, cache_fp);
            if (cached.has_value()) {
                all_points = std::move(cached.value());
                from_cache = true;
                if (!json_mode) {
                    std::cout << "\n从缓存加载 " << all_points.size() << " 个充电数据点" << std::endl;
                }
            }
        }
    }

    // 缓存未命中时扫描
    if (all_points.empty()) {
        scan_directory(log_dir, *parser, all_points, json_mode);

        // 扫描后写入缓存
        if (!all_points.empty() && !no_cache && cache_fp != 0) {
            CacheManager::save(
                log_dir / CacheManager::DEFAULT_CACHE_FILENAME, cache_fp, all_points);
        }
    }

    if (all_points.empty()) {
        if (json_mode) {
            std::cout << "{\"points_count\":0,\"cached\":false,\"time_range\":{\"start\":\"\",\"end\":\"\"},\"fields\":[],\"points\":[]}\n";
        } else {
            std::cout << "\n未找到充电数据点。" << std::endl;
        }
        return 0;
    }

    // ── 1.5 降采样 (作用于输出给前端的 points，不影响统计计算) ──
    std::vector<ChargerDataPoint> output_points;
    if (points_mode && downsample_count > 0 && all_points.size() > downsample_count) {
        output_points = StatsCalculator::downsample(all_points, downsample_count);
    } else if (points_mode) {
        output_points = all_points;
    }

    // ── 2. 计算统计 (全部字段, 指定时间范围) ───────────────
    std::vector<std::string> field_names;
    for (auto& fd : kAllFields) {
        field_names.push_back(fd.name);
    }
    auto all_stats = StatsCalculator::calcAllFields(all_points, field_names, start_ms, end_ms);

    // ── 2.5 阈值计算 (在全量数据上，非降采样) ─────────
    std::vector<ThresholdResult> threshold_results;
    for (const auto& ta : cli_thresholds) {
        threshold_results.push_back(
            StatsCalculator::calcThreshold(all_points, ta.field, ta.value, start_ms, end_ms));
    }

    // ── 3. 输出结果 ────────────────────────────────────────
    if (json_mode) {
        // 机器可读 JSON 输出
        std::cout << "{\n";
        std::cout << "  \"points_count\": " << all_points.size() << ",\n";
        std::cout << "  \"cached\": " << (from_cache ? "true" : "false") << ",\n";
        std::cout << "  \"time_range\": {\n";
        std::cout << "    \"start\": \"" << msToHMS(start_ms) << "\",\n";
        std::cout << "    \"end\": \"" << msToHMS(end_ms) << "\"\n";
        std::cout << "  },\n";
        std::cout << "  \"fields\": [\n";
        bool first_field = true;
        for (size_t i = 0; i < all_stats.size(); i++) {
            const auto& st = all_stats[i];
            const auto& fd = kAllFields[i];
            if (st.count == 0) continue;
            if (!first_field) std::cout << ",\n";
            first_field = false;
            std::cout << "    {\n";
            std::cout << "      \"name\": \"" << jsonEscape(fd.name) << "\",\n";
            std::cout << "      \"label\": \"" << jsonEscape(fd.label) << "\",\n";
            std::cout << "      \"unit\": \"" << jsonEscape(fd.unit) << "\",\n";
            std::cout << "      \"count\": " << st.count << ",\n";
            std::cout << "      \"max\": " << jsonDouble(st.max) << ",\n";
            std::cout << "      \"min\": " << jsonDouble(st.min) << ",\n";
            std::cout << "      \"avg\": " << jsonDouble(st.avg) << ",\n";
            std::cout << "      \"median\": " << jsonDouble(st.median) << "\n";
            std::cout << "    }";
        }
        std::cout << "\n  ]";
        if (!threshold_results.empty()) {
            std::cout << ",\n  \"thresholds\": [\n";
            for (size_t ti = 0; ti < threshold_results.size(); ti++) {
                if (ti > 0) std::cout << ",\n";
                const auto& tr = threshold_results[ti];
                std::cout << "    {\n"
                          << "      \"field\": \"" << jsonEscape(tr.field_name) << "\",\n"
                          << "      \"value\": " << jsonDouble(tr.threshold_value) << ",\n"
                          << "      \"total_time_ms\": " << tr.total_time_ms << ",\n"
                          << "      \"above_time_ms\": " << tr.above_time_ms << ",\n"
                          << "      \"above_pct\": " << jsonDouble(tr.above_pct) << "\n"
                          << "    }";
            }
            std::cout << "\n  ]";
        }
        if (points_mode) {
            std::cout << ",\n  \"points\": [\n";
            for (size_t pi = 0; pi < output_points.size(); pi++) {
                if (pi > 0) std::cout << ",\n";
                const auto& pt = output_points[pi];
                std::cout << "    {\"t\":" << pt.elapsed_ms
                          << ",\"v\":" << jsonDouble(pt.battery_voltage_mv)
                          << ",\"tmp\":" << jsonDouble(pt.battery_temperature_c)
                          << ",\"cur\":" << jsonDouble(pt.battery_current_ma)
                          << ",\"lvl\":" << jsonDouble(pt.battery_level_pct)
                          << ",\"cc\":" << jsonDouble(pt.charge_cycle_count)
                          << "}";
            }
            std::cout << "\n  ]";
        }
        std::cout << "\n}\n";
    } else {
        // 人类可读文本输出
        if (no_cache || cache_fp == 0) {
            std::cout << "\n总计: " << all_points.size() << " 个数据点" << std::endl;
        }

        std::cout << "\n========== 统计结果 ==========" << std::endl;
        if (start_ms > 0 || end_ms < INT64_MAX) {
            std::cout << "时间范围: " << msToHMS(start_ms)
                      << " ~ " << msToHMS(end_ms) << std::endl;
        }

        bool any = false;
        for (size_t i = 0; i < all_stats.size(); i++) {
            const auto& st = all_stats[i];
            const auto& fd = kAllFields[i];
            if (st.count == 0) continue;
            any = true;
            std::cout << "\n" << fd.label << " (" << st.count << " 个数据点):" << std::endl;
            std::cout << "  最高: " << st.max << " " << fd.unit << std::endl;
            std::cout << "  最低: " << st.min << " " << fd.unit << std::endl;
            std::cout << "  平均: " << st.avg << " " << fd.unit << std::endl;
            std::cout << "  中位数: " << st.median << " " << fd.unit << std::endl;
        }

        if (!any) {
            std::cout << "\n指定范围内无有效数据。" << std::endl;
        }

        if (!threshold_results.empty()) {
            std::cout << "\n========== 阈值分析 ==========" << std::endl;
            for (const auto& tr : threshold_results) {
                std::cout << "\n" << tr.field_name << " > " << tr.threshold_value << std::endl;
                if (tr.total_time_ms == 0) {
                    std::cout << "  无有效数据" << std::endl;
                    continue;
                }
                std::cout << "  总时长: "
                          << (tr.total_time_ms / 60000) << "m "
                          << ((tr.total_time_ms % 60000) / 1000) << "s" << std::endl;
                std::cout << "  超过阈值: "
                          << (tr.above_time_ms / 60000) << "m "
                          << ((tr.above_time_ms % 60000) / 1000) << "s"
                          << " (" << tr.above_pct << "%)" << std::endl;
                if (!tr.above_segments.empty()) {
                    std::cout << "  超限时段 (" << tr.above_segments.size() << " 段):" << std::endl;
                    for (const auto& seg : tr.above_segments) {
                        std::cout << "    " << msToHMS(seg.start_ms)
                                  << " ~ " << msToHMS(seg.end_ms) << std::endl;
                    }
                }
            }
        }
    }

    return 0;
}
