#ifndef CHARGERLOG_BASE_CHARGER_PARSER_H
#define CHARGERLOG_BASE_CHARGER_PARSER_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chargerlog/charger_data_point.h"

namespace chargerlog {

/// 平台解析器基类
/// 不同平台继承此类，实现自己的日志解析逻辑
class BaseParser {
public:
    virtual ~BaseParser() = default;

    /// 返回平台名称标识 (如 "android_healthd", "linux_xxx")
    virtual std::string platformName() const = 0;

    /// 判断本解析器能否处理该行日志
    virtual bool canParse(const std::string& line) const = 0;

    /// 解析一行日志，返回数据点
    /// 返回 nullopt 表示该行不是有效数据行
    virtual std::optional<ChargerDataPoint> parseLine(const std::string& line) const = 0;

    /// 获取该平台支持的字段列表
    virtual std::vector<std::string> supportedFields() const = 0;
};

/// 统计计算器
/// 对一组 ChargerDataPoint 按指定时间范围和字段计算统计值
class StatsCalculator {
public:
    /// 计算某字段在时间范围内的统计值
    /// @param points  数据点集合
    /// @param field   字段名
    /// @param start_ms 起始时间 (含, 默认从第一个)
    /// @param end_ms   结束时间 (含, 默认到最后一个)
    static FieldStats calcField(
        const std::vector<ChargerDataPoint>& points,
        const std::string& field,
        int64_t start_ms = 0,
        int64_t end_ms = INT64_MAX);

    /// 计算所有字段的统计值
    static std::vector<FieldStats> calcAllFields(
        const std::vector<ChargerDataPoint>& points,
        const std::vector<std::string>& fields,
        int64_t start_ms = 0,
        int64_t end_ms = INT64_MAX);

    /// 计算整段时间窗口的统计 (按窗口滑动)
    static std::vector<FieldStats> calcWindowedStats(
        const std::vector<ChargerDataPoint>& points,
        const std::string& field,
        int64_t window_ms,
        int64_t step_ms);

    /// 降采样：从 points 中等距取出最多 target_count 个点
    /// 保证首尾点始终保留，适合前端折线图渲染
    static std::vector<ChargerDataPoint> downsample(
        const std::vector<ChargerDataPoint>& points,
        size_t target_count);

    /// 计算某字段超过阈值的时间段和占比
    /// 使用线性插值精确定位阈值交叉点
    static ThresholdResult calcThreshold(
        const std::vector<ChargerDataPoint>& points,
        const std::string& field,
        double threshold,
        int64_t start_ms = 0,
        int64_t end_ms = INT64_MAX);
};

}  // namespace chargerlog

#endif  // CHARGERLOG_BASE_CHARGER_PARSER_H
