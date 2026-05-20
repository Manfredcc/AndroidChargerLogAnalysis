#include "chargerlog/cache_manager.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace chargerlog {

namespace {

// ── FNV-1a 64-bit ────────────────────────────────────────────
// 确定性哈希，不依赖 std::hash 的随机种子
uint64_t fnv1a_64(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

/// 将 file_time_type 的 tick count 转为 uint64_t
/// 用于指纹计算（不关心真实时间，只需稳定可比较）
uint64_t mtime_to_uint64(const std::filesystem::file_time_type& ft) {
    return static_cast<uint64_t>(ft.time_since_epoch().count());
}

/// 安全将 double 转为字符串，"nan" 用于 NAN
std::string double_to_str(double val) {
    if (std::isnan(val)) return "nan";
    char buf[64];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec == std::errc()) {
        return std::string(buf, ptr);
    }
    return "nan";
}

/// 安全将字符串转为 double，"nan" → NAN
double str_to_double(const std::string& s) {
    if (s == "nan") return NAN;
    double val = 0.0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec == std::errc()) return val;
    return NAN;
}

}  // anonymous namespace

// ── 指纹计算 ───────────────────────────────────────────────────
uint64_t CacheManager::computeFingerprint(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        return 0;
    }

    // 收集文件信息: (相对路径, mtime, file_size)
    struct FileEntry {
        std::string rel_path;
        uint64_t mtime;
        uint64_t size;
    };
    std::vector<FileEntry> entries;

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        // 跳过缓存文件自身
        if (entry.path().filename() == DEFAULT_CACHE_FILENAME) continue;

        auto rel = std::filesystem::relative(entry.path(), dir);
        entries.push_back({
            rel.string(),
            mtime_to_uint64(entry.last_write_time()),
            static_cast<uint64_t>(entry.file_size())
        });
    }

    if (entries.empty()) return 0;

    // 按路径排序保证稳定性
    std::sort(entries.begin(), entries.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  return a.rel_path < b.rel_path;
              });

    // 构建哈希输入
    std::string input;
    for (const auto& e : entries) {
        input += e.rel_path + "|" + std::to_string(e.mtime) + "|" + std::to_string(e.size) + "\n";
    }

    return fnv1a_64(input);
}

// ── 保存缓存 ───────────────────────────────────────────────────
bool CacheManager::save(const std::filesystem::path& cache_path,
                        uint64_t fingerprint,
                        std::vector<ChargerDataPoint>& points) {

    // 先按 elapsed_ms 排序（保证 StatsCalculator 的不变性）
    std::sort(points.begin(), points.end(),
              [](const ChargerDataPoint& a, const ChargerDataPoint& b) {
                  return a.elapsed_ms < b.elapsed_ms;
              });

    // 原子写入: 先写 .tmp 再 rename
    std::filesystem::path tmp_path = cache_path;
    tmp_path += ".tmp";

    std::ofstream out(tmp_path);
    if (!out.is_open()) {
        std::cerr << "[缓存] 警告: 无法写入缓存文件 " << tmp_path.u8string() << std::endl;
        return false;
    }

    // 第 1 行: 版本 + 指纹
    char fp_hex[17];
    std::snprintf(fp_hex, sizeof(fp_hex), "%016llx",
                  static_cast<unsigned long long>(fingerprint));
    out << "V1|" << fp_hex << "\n";

    // 数据行
    for (const auto& pt : points) {
        out << pt.elapsed_ms << "|"
            << double_to_str(pt.battery_voltage_mv) << "|"
            << double_to_str(pt.battery_temperature_c) << "|"
            << double_to_str(pt.battery_current_ma) << "|"
            << double_to_str(pt.battery_level_pct) << "|"
            << double_to_str(pt.bus_voltage_mv) << "|"
            << double_to_str(pt.bus_current_ma) << "|"
            << pt.timestamp_str << "\n";
    }

    out.close();

    std::error_code ec;
    std::filesystem::rename(tmp_path, cache_path, ec);
    if (ec) {
        std::cerr << "[缓存] 警告: 写入缓存失败: " << ec.message() << std::endl;
        return false;
    }

    return true;
}

// ── 加载缓存 ───────────────────────────────────────────────────
std::optional<std::vector<ChargerDataPoint>>
CacheManager::load(const std::filesystem::path& cache_path, uint64_t fingerprint) {

    if (!std::filesystem::exists(cache_path)) {
        return std::nullopt;
    }

    std::ifstream in(cache_path);
    if (!in.is_open()) {
        std::cerr << "[缓存] 警告: 无法读取缓存文件 " << cache_path.u8string() << std::endl;
        return std::nullopt;
    }

    // ── 读取第 1 行: 版本 + 指纹 ────────────
    std::string header;
    if (!std::getline(in, header)) {
        return std::nullopt;  // 空文件
    }

    if (header.size() < 18 || header.substr(0, 3) != "V1|") {
        return std::nullopt;  // 版本不匹配或格式错误
    }

    std::string stored_fp = header.substr(3);

    // 比较指纹
    char expected_fp[17];
    std::snprintf(expected_fp, sizeof(expected_fp), "%016llx",
                  static_cast<unsigned long long>(fingerprint));

    if (stored_fp != expected_fp) {
        return std::nullopt;  // 指纹不匹配
    }

    // ── 读取数据行 ───────────────────────────
    std::vector<ChargerDataPoint> points;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) continue;  // 跳过空行

        ChargerDataPoint pt;
        std::vector<std::string> tokens;
        size_t start = 0;
        size_t end;

        // 分割 8 个字段（用 | 分隔）
        // timestamp_str 是最后一个字段，可能包含空格但不含 |
        while ((end = line.find('|', start)) != std::string::npos && tokens.size() < 7) {
            tokens.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        // 剩余部分是 timestamp_str
        tokens.push_back(line.substr(start));

        // 需要恰好 8 个字段
        if (tokens.size() != 8) {
            return std::nullopt;  // 格式错误
        }

        // 解析字段
        // tokens[0]: elapsed_ms
        auto [ptr, ec] = std::from_chars(tokens[0].data(), tokens[0].data() + tokens[0].size(),
                                          pt.elapsed_ms);
        if (ec != std::errc()) {
            return std::nullopt;
        }

        pt.battery_voltage_mv    = str_to_double(tokens[1]);
        pt.battery_temperature_c = str_to_double(tokens[2]);
        pt.battery_current_ma    = str_to_double(tokens[3]);
        pt.battery_level_pct     = str_to_double(tokens[4]);
        pt.bus_voltage_mv        = str_to_double(tokens[5]);
        pt.bus_current_ma        = str_to_double(tokens[6]);
        pt.timestamp_str         = tokens[7];

        points.push_back(std::move(pt));
    }

    if (points.empty() && in.bad()) {
        return std::nullopt;  // 读取出错
    }

    return points;
}

}  // namespace chargerlog
