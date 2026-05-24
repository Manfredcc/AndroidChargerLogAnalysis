#ifndef CHARGERLOG_PARSER_FACTORY_H
#define CHARGERLOG_PARSER_FACTORY_H

#include "chargerlog/base_charger_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace chargerlog {

/// 解析器工厂
/// 根据平台标识字符串创建对应的 BaseParser 实例
/// 新增平台解析器时，在此工厂中注册即可
class ParserFactory {
public:
    /// 根据平台名称创建解析器实例
    /// @param platform_name 平台标识 (如 "android_healthd")
    /// @return 对应解析器的 unique_ptr，未知平台返回 nullptr
    static std::unique_ptr<BaseParser> create(const std::string& platform_name);

    /// 获取当前已注册的所有平台标识列表
    static std::vector<std::string> availablePlatforms();
};

}  // namespace chargerlog

#endif  // CHARGERLOG_PARSER_FACTORY_H
