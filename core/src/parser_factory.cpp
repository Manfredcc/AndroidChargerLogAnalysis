#include "chargerlog/parser_factory.h"

#include "chargerlog/healthd_parser.h"

namespace chargerlog {

std::unique_ptr<BaseParser> ParserFactory::create(const std::string& platform_name) {
    if (platform_name == "android_healthd") {
        return std::make_unique<HealthdParser>();
    }
    // TODO: 未来在此注册新平台解析器
    // if (platform_name == "linux_xxx") { return std::make_unique<LinuxXXXParser>(); }
    return nullptr;
}

std::vector<std::string> ParserFactory::availablePlatforms() {
    return {
        "android_healthd",
        // "linux_xxx",
    };
}

}  // namespace chargerlog
