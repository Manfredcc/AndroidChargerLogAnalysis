#include "chargerlog/project_config.h"

#include <sstream>
#include <stdexcept>

namespace chargerlog {

void ProjectConfigManager::setConfig(const std::string& project_name, const std::string& platform) {
    configs_[project_name] = platform;
}

std::string ProjectConfigManager::getPlatform(const std::string& project_name) const {
    auto it = configs_.find(project_name);
    if (it != configs_.end()) {
        return it->second;
    }
    return "";
}

const std::unordered_map<std::string, std::string>& ProjectConfigManager::allConfigs() const {
    return configs_;
}

void ProjectConfigManager::loadFromJson(const std::string& json_str) {
    // 极简 JSON 解析, 仅支持:
    //   { "project1": "platform1", "project2": "platform2" }
    // 不支持嵌套/转义/空格值
    // 后续可替换为 nlohmann/json
    std::string key, value;
    size_t pos = 0;

    // 跳过外层 { }
    auto open_brace = json_str.find('{');
    auto close_brace = json_str.find('}');
    if (open_brace == std::string::npos || close_brace == std::string::npos) {
        return;
    }

    std::string inner = json_str.substr(open_brace + 1, close_brace - open_brace - 1);

    std::istringstream iss(inner);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // 跳过空白
        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n\""));
            s.erase(s.find_last_not_of(" \t\r\n\"") + 1);
        };

        // 找冒号分隔 key:value
        auto colon = token.find(':');
        if (colon == std::string::npos) continue;

        std::string project = token.substr(0, colon);
        std::string platform = token.substr(colon + 1);
        trim(project);
        trim(platform);

        if (!project.empty() && !platform.empty()) {
            configs_[project] = platform;
        }
    }
}

}  // namespace chargerlog
