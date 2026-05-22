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

ThresholdResult StatsCalculator::calcThreshold(
    const std::vector<ChargerDataPoint>& points,
    const std::string& field,
    double threshold,
    int64_t start_ms,
    int64_t end_ms) {

    ThresholdResult result;
    result.field_name = field;
    result.threshold_value = threshold;

    // 收集时间范围内有值的数据点
    struct ValPoint { int64_t t; double v; };
    std::vector<ValPoint> vps;
    for (const auto& pt : points) {
        if (pt.elapsed_ms < start_ms) continue;
        if (pt.elapsed_ms > end_ms) break;
        double v = pt.get(field);
        if (!std::isnan(v)) vps.push_back({pt.elapsed_ms, v});
    }

    if (vps.size() < 2) return result;

    result.total_time_ms = vps.back().t - vps.front().t;

    // 线性插值遍历
    int64_t seg_start = -1;
    for (size_t i = 0; i < vps.size() - 1; i++) {
        const auto& p1 = vps[i];
        const auto& p2 = vps[i + 1];
        int64_t dur = p2.t - p1.t;
        if (dur <= 0) continue;

        bool above1 = p1.v > threshold;
        bool above2 = p2.v > threshold;

        if (above1 && above2) {
            // 整个区间都在阈值之上
            result.above_time_ms += dur;
            if (seg_start < 0) seg_start = p1.t;
        } else if (above1 && !above2) {
            // 从上方穿过阈值到下方，线性插值交叉点
            double ratio = (threshold - p2.v) / (p1.v - p2.v);
            int64_t cross_t = p2.t - static_cast<int64_t>(ratio * dur);
            int64_t above_dur = std::max<int64_t>(0, cross_t - p1.t);
            result.above_time_ms += above_dur;
            if (seg_start < 0) seg_start = p1.t;
            result.above_segments.push_back({seg_start, cross_t});
            seg_start = -1;
        } else if (!above1 && above2) {
            // 从下方穿过阈值到上方
            double ratio = (threshold - p1.v) / (p2.v - p1.v);
            int64_t cross_t = p1.t + static_cast<int64_t>(ratio * dur);
            int64_t above_dur = std::max<int64_t>(0, p2.t - cross_t);
            result.above_time_ms += above_dur;
            seg_start = cross_t;
        }
        // both below: nothing
    }

    // 关闭最后一个未关闭的段
    if (seg_start >= 0) {
        result.above_segments.push_back({seg_start, vps.back().t});
    }

    if (result.total_time_ms > 0) {
        result.above_pct = static_cast<double>(result.above_time_ms)
                         / static_cast<double>(result.total_time_ms) * 100.0;
    }

    return result;
}

}  // namespace chargerlog
