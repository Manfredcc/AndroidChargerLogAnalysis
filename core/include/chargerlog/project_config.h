#ifndef CHARGERLOG_PROJECT_CONFIG_H
#define CHARGERLOG_PROJECT_CONFIG_H

#include <string>
#include <unordered_map>

namespace chargerlog {

/// 项目配置
/// 将项目名称映射到对应的平台解析器
///
/// 配置示例 (由外部供给, 非本类的职责):
///   projects: {
///     "230":   { platform: "android_healthd" },
///     "傅里叶": { platform: "linux_xxx" }
///   }
struct ProjectConfig {
    std::string project_name;       ///< 项目名称 (如 "230", "傅里叶")
    std::string platform;           ///< 平台标识 (如 "android_healthd")

    /// 平台 → 显示名称映射
    static const std::unordered_map<std::string, std::string>& platformDisplayNames() {
        static const std::unordered_map<std::string, std::string> names = {
            {"android_healthd", "Android Healthd"},
            {"linux_xxx",       "Linux (通用)"},
        };
        return names;
    }

    /// 判断平台是否存在
    static bool isValidPlatform(const std::string& platform) {
        return platformDisplayNames().count(platform) > 0;
    }
};

/// 项目配置管理器
class ProjectConfigManager {
public:
    /// 添加或更新一个项目配置
    void setConfig(const std::string& project_name, const std::string& platform);

    /// 根据项目名称获取平台
    std::string getPlatform(const std::string& project_name) const;

    /// 获取所有配置
    const std::unordered_map<std::string, std::string>& allConfigs() const;

    /// 从 JSON 字符串加载
    void loadFromJson(const std::string& json_str);

private:
    std::unordered_map<std::string, std::string> configs_;  ///< project → platform
};

}  // namespace chargerlog

#endif  // CHARGERLOG_PROJECT_CONFIG_H
