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
    const HealthdParser& parser,
    std::vector<ChargerDataPoint>& out_points) {

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

    std::cout << "  " << file_path.filename().u8string()
              << ": " << line_count << " 行, "
              << parsed_count << " 个充电数据点" << std::endl;
}

/// 递归扫描目录
static void scan_directory(
    const fs::path& dir_path,
    const HealthdParser& parser,
    std::vector<ChargerDataPoint>& out_points) {

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
        std::cerr << "错误: 目录不存在或不是目录: " << dir_path.u8string() << std::endl;
        return;
    }

    std::cout << "\n扫描目录: " << dir_path.u8string() << std::endl;

    for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        if (!likely_text_file(path)) continue;

        collect_points(path, parser, out_points);
    }
}

// ── 主入口 ─────────────────────────────────────────────────────────

static void setup_console() {
#ifdef _WIN32
    // 让 Windows 终端正确显示 UTF-8 中文
    SetConsoleOutputCP(CP_UTF8);
#endif
}

int main(int argc, char* argv[]) {
    setup_console();

    // ── 参数解析 ───────────────────────────────────────────
    bool no_cache = false;
    fs::path log_dir;

    if (argc == 3 && std::string(argv[1]) == "--no-cache") {
        no_cache = true;
        log_dir = path_from_arg(argv[2]);
    } else if (argc == 2) {
        log_dir = path_from_arg(argv[1]);
    } else {
        std::cerr << "用法: chargerlog [--no-cache] <日志目录>" << std::endl;
        return 1;
    }

    // ── 1. 扫描目录, 解析所有日志 ──────────────────────────
    HealthdParser parser;
    std::vector<ChargerDataPoint> all_points;

    // 缓存查找
    uint64_t cache_fp = 0;
    if (!no_cache) {
        cache_fp = CacheManager::computeFingerprint(log_dir);
        if (cache_fp != 0) {
            auto cached = CacheManager::load(
                log_dir / CacheManager::DEFAULT_CACHE_FILENAME, cache_fp);
            if (cached.has_value()) {
                all_points = std::move(cached.value());
                std::cout << "\n从缓存加载 " << all_points.size() << " 个充电数据点" << std::endl;
            }
        }
    }

    // 缓存未命中时扫描
    if (all_points.empty()) {
        scan_directory(log_dir, parser, all_points);

        // 扫描后写入缓存
        if (!all_points.empty() && !no_cache && cache_fp != 0) {
            CacheManager::save(
                log_dir / CacheManager::DEFAULT_CACHE_FILENAME, cache_fp, all_points);
        }
    }

    if (all_points.empty()) {
        std::cout << "\n未找到充电数据点。" << std::endl;
        return 0;
    }

    if (no_cache || cache_fp == 0) {
        std::cout << "\n总计: " << all_points.size() << " 个数据点" << std::endl;
    }

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
