#ifndef CHARGERLOG_HEALTHD_PARSER_H
#define CHARGERLOG_HEALTHD_PARSER_H

#include "chargerlog/base_charger_parser.h"

namespace chargerlog {

/// Android healthd 日志解析器
///
/// 解析格式:
///   01-01 00:00:05.123  1234  5678 I healthd: battery l=65 v=3920 t=28.5 h=2 st=2 c=1200 chg=450
///
/// 字段映射:
///   v   → battery_voltage_mv
///   t   → battery_temperature_c
///   chg → battery_current_ma
///   l   → battery_level_pct
///
/// 线上电压/电流 (bus_voltage/bus_current): healthd 不提供此数据
class HealthdParser : public BaseParser {
public:
    std::string platformName() const override;
    bool canParse(const std::string& line) const override;
    std::optional<ChargerDataPoint> parseLine(const std::string& line) const override;
    std::vector<std::string> supportedFields() const override;
};

}  // namespace chargerlog

#endif  // CHARGERLOG_HEALTHD_PARSER_H
