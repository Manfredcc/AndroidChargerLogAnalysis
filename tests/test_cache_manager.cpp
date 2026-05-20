#include "chargerlog/cache_manager.h"
#include "chargerlog/charger_data_point.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace chargerlog;

// ── Test helpers ────────────────────────────────────────────────

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

namespace fs = std::filesystem;

// ── 临时目录管理 ───────────────────────────────────────────────

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("chargerlog_test_" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// ── 创建测试文件 ───────────────────────────────────────────────

static void create_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

// ── 辅助: 创建测试数据点 ───────────────────────────────────────

static ChargerDataPoint make_point(int64_t elapsed, double v, double t,
                                    double cur, double lvl, const std::string& ts) {
    ChargerDataPoint pt;
    pt.elapsed_ms = elapsed;
    pt.battery_voltage_mv = v;
    pt.battery_temperature_c = t;
    pt.battery_current_ma = cur;
    pt.battery_level_pct = lvl;
    pt.timestamp_str = ts;
    // bus_* 保持 NAN
    return pt;
}

// ── Tests ───────────────────────────────────────────────────────

/// 指纹: 空目录 → 0
static bool test_fingerprint_empty_dir() {
    TempDir tmp;
    auto fp = CacheManager::computeFingerprint(tmp.path);
    ASSERT(fp == 0, "empty dir fingerprint should be 0");
    return true;
}

/// 指纹: 同一目录两次计算应该一致
static bool test_fingerprint_stable() {
    TempDir tmp;
    create_file(tmp.path / "file1.log", "some log content");
    create_file(tmp.path / "file2.log", "different content");

    auto fp1 = CacheManager::computeFingerprint(tmp.path);
    auto fp2 = CacheManager::computeFingerprint(tmp.path);
    ASSERT(fp1 == fp2, "same dir should have same fingerprint");
    ASSERT(fp1 != 0, "non-empty dir should have non-zero fingerprint");
    return true;
}

/// 指纹: 新增文件后指纹应变化
static bool test_fingerprint_changes_on_new_file() {
    TempDir tmp;
    create_file(tmp.path / "a.log", "content");

    auto fp_before = CacheManager::computeFingerprint(tmp.path);
    create_file(tmp.path / "b.log", "more content");
    auto fp_after = CacheManager::computeFingerprint(tmp.path);

    ASSERT(fp_before != fp_after, "adding a file should change fingerprint");
    return true;
}

/// 指纹: 修改文件内容后指纹应变化
static bool test_fingerprint_changes_on_modify() {
    TempDir tmp;
    create_file(tmp.path / "data.log", "original content");

    auto fp_before = CacheManager::computeFingerprint(tmp.path);

    // 确保内容长度也不同 (文件大小是指纹输入的一部分)
    fs::remove(tmp.path / "data.log");
    create_file(tmp.path / "data.log", "modified content that is much longer");

    auto fp_after = CacheManager::computeFingerprint(tmp.path);
    ASSERT(fp_before != fp_after, "modifying a file should change fingerprint");
    return true;
}

/// 指纹: 子目录中的文件也应计入
static bool test_fingerprint_includes_subdirs() {
    TempDir tmp;
    create_file(tmp.path / "aplog" / "events.gz", "compressed");

    auto fp = CacheManager::computeFingerprint(tmp.path);
    ASSERT(fp != 0, "subdir files should be included in fingerprint");
    return true;
}

/// 保存 & 加载: 基本 round-trip
static bool test_save_load_roundtrip() {
    TempDir tmp;

    // 创建测试文件使指纹有效
    create_file(tmp.path / "dummy.log", "data");

    auto fp = CacheManager::computeFingerprint(tmp.path);
    ASSERT(fp != 0, "fingerprint should be non-zero");

    std::vector<ChargerDataPoint> points;
    points.push_back(make_point(1000, 3900.0, 28.5, 450.0, 65.0, "01-01 00:00:01.000"));
    points.push_back(make_point(2000, 3920.0, 28.8, 460.0, 66.0, "01-01 00:00:02.000"));
    points.push_back(make_point(3000, 3950.0, 29.0, 470.0, 67.0, "01-01 00:00:03.000"));

    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    ASSERT(CacheManager::save(cache_path, fp, points), "save should succeed");

    // 验证保存后 points 已排序 (输入已有序，检查不变)
    ASSERT(points[0].elapsed_ms == 1000, "points sorted after save");

    // 加载
    auto loaded = CacheManager::load(cache_path, fp);
    ASSERT(loaded.has_value(), "load should succeed");

    ASSERT(loaded->size() == 3, "loaded 3 points");

    ASSERT(loaded->at(0).elapsed_ms == 1000, "point 0 elapsed_ms");
    ASSERT(loaded->at(1).elapsed_ms == 2000, "point 1 elapsed_ms");
    ASSERT(loaded->at(2).elapsed_ms == 3000, "point 2 elapsed_ms");

    ASSERT(std::abs(loaded->at(0).battery_voltage_mv - 3900.0) < 0.01, "point 0 voltage");
    ASSERT(std::abs(loaded->at(2).battery_temperature_c - 29.0) < 0.01, "point 2 temp");
    ASSERT(loaded->at(0).timestamp_str == "01-01 00:00:01.000", "point 0 timestamp");

    return true;
}

/// 保存 & 加载: 含 NAN 的 round-trip
static bool test_save_load_nan() {
    TempDir tmp;
    create_file(tmp.path / "dummy.log", "data");
    auto fp = CacheManager::computeFingerprint(tmp.path);

    std::vector<ChargerDataPoint> points;
    points.push_back(make_point(1000, 4000.0, NAN, NAN, 70.0, "01-01 00:00:01.000"));

    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    ASSERT(CacheManager::save(cache_path, fp, points), "save with NAN");

    auto loaded = CacheManager::load(cache_path, fp);
    ASSERT(loaded.has_value(), "load with NAN");

    ASSERT(std::isnan(loaded->at(0).battery_temperature_c), "temp should be NAN");
    ASSERT(std::isnan(loaded->at(0).battery_current_ma), "current should be NAN");
    ASSERT(!std::isnan(loaded->at(0).battery_voltage_mv), "voltage should not be NAN");
    ASSERT(std::abs(loaded->at(0).battery_level_pct - 70.0) < 0.01, "level = 70");

    return true;
}

/// 缓存失效: 指纹不匹配 → nullopt
static bool test_cache_miss_on_wrong_fingerprint() {
    TempDir tmp;
    create_file(tmp.path / "dummy.log", "data");

    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    uint64_t fp1 = 0x123456789ABCDEF0ULL;
    uint64_t fp2 = 0xDEADBEEFCAFEBABEULL;

    std::vector<ChargerDataPoint> points;
    points.push_back(make_point(1000, 3900, 28, 450, 65, "ts"));

    ASSERT(CacheManager::save(cache_path, fp1, points), "save with fp1");

    // 用不同的指纹加载 → 应失败
    auto loaded = CacheManager::load(cache_path, fp2);
    ASSERT(!loaded.has_value(), "load with wrong fp should fail");

    // 用正确的指纹加载 → 应成功
    auto loaded_ok = CacheManager::load(cache_path, fp1);
    ASSERT(loaded_ok.has_value(), "load with correct fp should succeed");

    return true;
}

/// 缓存不存在 → nullopt
static bool test_cache_miss_on_missing_file() {
    TempDir tmp;
    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    auto loaded = CacheManager::load(cache_path, 0x1234);
    ASSERT(!loaded.has_value(), "load non-existent cache should fail");
    return true;
}

/// 保存时自动排序
static bool test_save_sorts_points() {
    TempDir tmp;
    create_file(tmp.path / "dummy.log", "data");
    auto fp = CacheManager::computeFingerprint(tmp.path);

    std::vector<ChargerDataPoint> points;
    points.push_back(make_point(3000, 3950, 29, 470, 67, "ts3"));
    points.push_back(make_point(1000, 3900, 28, 450, 65, "ts1"));
    points.push_back(make_point(2000, 3920, 28, 460, 66, "ts2"));

    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    CacheManager::save(cache_path, fp, points);

    auto loaded = CacheManager::load(cache_path, fp);
    ASSERT(loaded.has_value(), "load sorted cache");
    ASSERT(loaded->at(0).elapsed_ms == 1000, "first point should be 1000ms");
    ASSERT(loaded->at(1).elapsed_ms == 2000, "second point should be 2000ms");
    ASSERT(loaded->at(2).elapsed_ms == 3000, "third point should be 3000ms");

    return true;
}

/// 空数据点向量
static bool test_save_empty_points() {
    TempDir tmp;
    create_file(tmp.path / "dummy.log", "data");
    auto fp = CacheManager::computeFingerprint(tmp.path);

    std::vector<ChargerDataPoint> empty;
    auto cache_path = tmp.path / CacheManager::DEFAULT_CACHE_FILENAME;
    ASSERT(CacheManager::save(cache_path, fp, empty), "save empty points");

    auto loaded = CacheManager::load(cache_path, fp);
    ASSERT(loaded.has_value(), "load empty cache");
    ASSERT(loaded->empty(), "loaded should be empty");

    return true;
}

// ── 入口 ────────────────────────────────────────────────────────

int main() {
    bool all_pass = true;

    all_pass &= TEST(fingerprint_empty_dir);
    all_pass &= TEST(fingerprint_stable);
    all_pass &= TEST(fingerprint_changes_on_new_file);
    all_pass &= TEST(fingerprint_changes_on_modify);
    all_pass &= TEST(fingerprint_includes_subdirs);
    all_pass &= TEST(save_load_roundtrip);
    all_pass &= TEST(save_load_nan);
    all_pass &= TEST(cache_miss_on_wrong_fingerprint);
    all_pass &= TEST(cache_miss_on_missing_file);
    all_pass &= TEST(save_sorts_points);
    all_pass &= TEST(save_empty_points);

    std::cout << "\n==== " << (all_pass ? "ALL PASS" : "SOME FAILED") << " ====" << std::endl;
    return all_pass ? 0 : 1;
}
