#ifndef CHARGERLOG_CHARGER_DATA_POINT_H
#define CHARGERLOG_CHARGER_DATA_POINT_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace chargerlog {

/// 单个时间戳对应的充电数据点
struct ChargerDataPoint {
    int64_t elapsed_ms = 0;            ///< 相对会话开始时间的毫秒数
    std::string timestamp_str;         ///< 原始时间戳字符串 (for display)

    // ── 常见充电数据字段 ──────────────────────────────
    // 使用 NAN 表示 "该平台无此字段"
    double battery_current_ma = NAN;       ///< 电池充电电流 (mA)
    double battery_voltage_mv = NAN;       ///< 电池电压 (mV)
    double bus_voltage_mv = NAN;           ///< 线上电压 VBUS (mV)
    double bus_current_ma = NAN;           ///< 线上电流 (mA)
    double battery_temperature_c = NAN;    ///< 电池温度 (°C)
    double battery_level_pct = NAN;        ///< 电池电量百分比 (%)
    double charge_cycle_count = NAN;       ///< 充电循环次数 (cc)

    /// 判断某字段是否有值
    bool has(const std::string& field) const;
    /// 获取某字段的值 (NAN 表示有效范围内)
    double get(const std::string& field) const;
    /// 获取所有有值的字段名
    std::vector<std::string> availableFields() const;
};

/// 某字段的统计数据
struct FieldStats {
    std::string field_name;   ///< 字段名
    double min = NAN;
    double max = NAN;
    double avg = NAN;
    double median = NAN;
    int count = 0;            ///< 有效数据点数
};

}  // namespace chargerlog

#endif  // CHARGERLOG_CHARGER_DATA_POINT_H
