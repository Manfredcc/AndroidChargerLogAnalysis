#ifndef CHARGERLOG_CACHE_MANAGER_H
#define CHARGERLOG_CACHE_MANAGER_H

#include "chargerlog/charger_data_point.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace chargerlog {

/// 目录解析缓存管理器
///
/// 缓存 CLI 扫描目录的解析结果，避免重复解析未变化的日志目录。
/// 缓存文件 (.chargerlog_cache) 存放在被扫描的目录中。
class CacheManager {
public:
    static constexpr const char* DEFAULT_CACHE_FILENAME = ".chargerlog_cache";

    /// 计算目录的稳定指纹（FNV-1a 64-bit）
    /// 遍历目录下所有 regular files，对 (相对路径, mtime, size) 做哈希。
    /// 返回 0 表示目录不存在或为空。
    static uint64_t computeFingerprint(const std::filesystem::path& dir);

    /// 保存数据点到缓存文件
    /// 保存前按 elapsed_ms 排序。写 .tmp 文件后 rename 实现原子写入。
    /// @param cache_path  缓存文件路径
    /// @param fingerprint 目录指纹
    /// @param points      数据点（会被排序）
    /// @return true 保存成功，false 失败（非致命，调用方继续）
    static bool save(const std::filesystem::path& cache_path,
                     uint64_t fingerprint,
                     std::vector<ChargerDataPoint>& points);

    /// 从缓存加载数据点
    /// @return 数据点向量（不排序，保存时已排序），失败时返回 nullopt
    static std::optional<std::vector<ChargerDataPoint>>
    load(const std::filesystem::path& cache_path, uint64_t fingerprint);
};

}  // namespace chargerlog

#endif  // CHARGERLOG_CACHE_MANAGER_H
