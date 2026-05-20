/// chargerlog CLI
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
#include <string>
#include <vector>

#include "chargerlog/healthd_parser.h"
#include "chargerlog/base_charger_parser.h"

namespace fs = std::filesystem;
using namespace chargerlog;

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
    const HealthdParser& parser,
    std::vector<ChargerDataPoint>& out_points) {

    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "  [跳过] 无法打开: " << file_path << std::endl;
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

    std::cout << "  " << file_path.filename().string()
              << ": " << line_count << " 行, "
              << parsed_count << " 个健康数据点" << std::endl;
}

/// 递归扫描目录
static void scan_directory(
    const fs::path& dir_path,
    const HealthdParser& parser,
    std::vector<ChargerDataPoint>& out_points) {

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        std::cerr << "错误: 目录不存在或不是目录: " << dir_path << std::endl;
        return;
    }

    std::cout << "\n扫描目录: " << dir_path << std::endl;

    for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        if (!likely_text_file(path)) continue;

        collect_points(path, parser, out_points);
    }
}

// ── 主入口 ─────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "用法: chargerlog <日志目录>" << std::endl;
        return 1;
    }

    fs::path log_dir = argv[1];

    // ── 1. 扫描目录, 解析所有日志 ──────────────────────────
    HealthdParser parser;
    std::vector<ChargerDataPoint> all_points;

    scan_directory(log_dir, parser, all_points);

    if (all_points.empty()) {
        std::cout << "\n未找到有效的健康数据点。" << std::endl;
        return 0;
    }

    std::cout << "\n总计: " << all_points.size() << " 个数据点" << std::endl;

    // ── 2. 计算统计 ────────────────────────────────────────
    auto voltage_stats = StatsCalculator::calcField(all_points, "battery_voltage_mv");
    auto temp_stats    = StatsCalculator::calcField(all_points, "battery_temperature_c");

    // ── 3. 输出结果 ────────────────────────────────────────
    std::cout << "\n========== 统计结果 ==========" << std::endl;

    if (voltage_stats.count > 0) {
        std::cout << "电池电压:" << std::endl;
        std::cout << "  最高: " << voltage_stats.max << " mV" << std::endl;
        std::cout << "  最低: " << voltage_stats.min << " mV" << std::endl;
        std::cout << "  平均: " << voltage_stats.avg << " mV" << std::endl;
    } else {
        std::cout << "电池电压: 无数据" << std::endl;
    }

    if (temp_stats.count > 0) {
        std::cout << "电池温度:" << std::endl;
        std::cout << "  最高: " << temp_stats.max << " °C" << std::endl;
        std::cout << "  最低: " << temp_stats.min << " °C" << std::endl;
        std::cout << "  平均: " << temp_stats.avg << " °C" << std::endl;
    } else {
        std::cout << "电池温度: 无数据" << std::endl;
    }

    return 0;
}
