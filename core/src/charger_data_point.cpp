#include "chargerlog/charger_data_point.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace chargerlog {

namespace {

/// 充电数据字段访问映射
using FieldGetter = double (ChargerDataPoint::*)() const;

struct FieldInfo {
    const char* name;
    FieldGetter getter;
    bool (ChargerDataPoint::*has_checker)() const;  // not used, we check via NAN
};

// 辅助: 获取各字段指针
#define FIELD_INFO(field) \
    { #field, [](const ChargerDataPoint& p) { return p.field; } }

// We use a simpler approach with direct field names and getters
struct FieldEntry {
    const char* name;
    double (ChargerDataPoint::*ptr);
};

static const FieldEntry kFields[] = {
    {"battery_current_ma",    &ChargerDataPoint::battery_current_ma},
    {"battery_voltage_mv",    &ChargerDataPoint::battery_voltage_mv},
    {"bus_voltage_mv",        &ChargerDataPoint::bus_voltage_mv},
    {"bus_current_ma",        &ChargerDataPoint::bus_current_ma},
    {"battery_temperature_c", &ChargerDataPoint::battery_temperature_c},
    {"battery_level_pct",     &ChargerDataPoint::battery_level_pct},
};

}  // anonymous namespace

bool ChargerDataPoint::has(const std::string& field) const {
    return !std::isnan(get(field));
}

double ChargerDataPoint::get(const std::string& field) const {
    for (const auto& f : kFields) {
        if (field == f.name) {
            return (*this).*f.ptr;
        }
    }
    return NAN;
}

std::vector<std::string> ChargerDataPoint::availableFields() const {
    std::vector<std::string> result;
    if (!std::isnan(battery_current_ma))    result.emplace_back("battery_current_ma");
    if (!std::isnan(battery_voltage_mv))    result.emplace_back("battery_voltage_mv");
    if (!std::isnan(bus_voltage_mv))        result.emplace_back("bus_voltage_mv");
    if (!std::isnan(bus_current_ma))        result.emplace_back("bus_current_ma");
    if (!std::isnan(battery_temperature_c)) result.emplace_back("battery_temperature_c");
    if (!std::isnan(battery_level_pct))     result.emplace_back("battery_level_pct");
    return result;
}

}  // namespace chargerlog
