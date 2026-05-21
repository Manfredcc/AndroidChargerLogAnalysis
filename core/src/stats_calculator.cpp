#include "chargerlog/base_charger_parser.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace chargerlog {

namespace {

/// 从数据点集合中筛选出某字段在时间范围内的有效值
std::vector<double> collectValues(
    const std::vector<ChargerDataPoint>& points,
    const std::string& field,
    int64_t start_ms,
    int64_t end_ms) {

    std::vector<double> values;
    for (const auto& pt : points) {
        if (pt.elapsed_ms < start_ms) continue;
        if (pt.elapsed_ms > end_ms) break;
        double val = pt.get(field);
        if (!std::isnan(val)) {
            values.push_back(val);
        }
    }
    return values;
}

}  // anonymous namespace

FieldStats StatsCalculator::calcField(
    const std::vector<ChargerDataPoint>& points,
    const std::string& field,
    int64_t start_ms,
    int64_t end_ms) {

    FieldStats stats;
    stats.field_name = field;

    auto values = collectValues(points, field, start_ms, end_ms);
    stats.count = static_cast<int>(values.size());

    if (values.empty()) {
        return stats;
    }

    // 排序 (用于中位数)
    std::sort(values.begin(), values.end());

    // min / max
    stats.min = values.front();
    stats.max = values.back();

    // avg
    double sum = 0;
    for (double v : values) sum += v;
    stats.avg = sum / values.size();

    // median
    size_t n = values.size();
    if (n % 2 == 0) {
        stats.median = (values[n / 2 - 1] + values[n / 2]) / 2.0;
    } else {
        stats.median = values[n / 2];
    }

    return stats;
}

std::vector<FieldStats> StatsCalculator::calcAllFields(
    const std::vector<ChargerDataPoint>& points,
    const std::vector<std::string>& fields,
    int64_t start_ms,
    int64_t end_ms) {

    std::vector<FieldStats> results;
    results.reserve(fields.size());
    for (const auto& f : fields) {
        results.push_back(calcField(points, f, start_ms, end_ms));
    }
    return results;
}

std::vector<FieldStats> StatsCalculator::calcWindowedStats(
    const std::vector<ChargerDataPoint>& points,
    const std::string& field,
    int64_t window_ms,
    int64_t step_ms) {

    if (points.empty() || window_ms <= 0 || step_ms <= 0) {
        return {};
    }

    auto global_min = points.front().elapsed_ms;
    auto global_max = points.back().elapsed_ms;

    std::vector<FieldStats> windows;
    for (auto w_start = global_min; w_start < global_max; w_start += step_ms) {
        auto w_end = w_start + window_ms;
        windows.push_back(calcField(points, field, w_start, w_end));
    }

    // 最后仍有一个余数窗口
    if (!windows.empty() && (windows.back().count == 0)) {
        windows.pop_back();
    }

    return windows;
}

std::vector<ChargerDataPoint> StatsCalculator::downsample(
    const std::vector<ChargerDataPoint>& points,
    size_t target_count) {

    if (target_count == 0) return {};
    if (points.size() <= target_count) return points;

    std::vector<ChargerDataPoint> result;
    result.reserve(target_count);

    for (size_t i = 0; i < target_count; i++) {
        size_t idx = (i * (points.size() - 1)) / (target_count - 1);
        result.push_back(points[idx]);
    }

    return result;
}

}  // namespace chargerlog
