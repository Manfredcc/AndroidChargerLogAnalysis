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

}  // namespace chargerlog

#endif  // CHARGERLOG_BASE_CHARGER_PARSER_H
