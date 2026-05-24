# core/ — C++ 核心库

`core/` 是 ChargerLogAnalysis 的 C++17 计算核心，负责日志解析、数据统计、阈值分析与缓存管理。Flask 服务端通过 `subprocess` 调用本库编译出的 `chargerlog` CLI 可执行文件，以 JSON 形式获取结果。

## 目录结构

```
core/
├── CMakeLists.txt                          # CMake 构建配置
├── cli_main.cpp                            # CLI 可执行文件入口 (chargerlog)
├── include/chargerlog/
│   ├── base_charger_parser.h               # 解析器抽象基类
│   ├── charger_data_point.h                # 数据模型 (DataPoint / FieldStats / ThresholdResult)
│   ├── healthd_parser.h                    # Android healthd 解析器
│   ├── stats_calculator.h                  # 统计 / 降采样 / 阈值分析
│   ├── parser_factory.h                    # 解析器工厂 (配置驱动实例化)
│   ├── cache_manager.h                     # 目录指纹缓存 (FNV-1a 64-bit)
│   └── project_config.h                    # 项目-平台映射配置
└── src/
    ├── charger_data_point.cpp              # 字段访问器实现
    ├── healthd_parser.cpp                  # healthd 日志解析实现
    ├── stats_calculator.cpp                # 统计 / 降采样 / 阈值分析
    ├── cache_manager.cpp                   # 缓存读写与指纹计算
    └── project_config.cpp                  # 配置管理器 (极简 JSON 解析)
```

## 构建

要求：CMake >= 3.20，支持 C++17 的编译器 (g++ 14.2 / MSVC / Clang)。

```bash
cd core
# 配置与编译
cmake -B build -S . -G "MinGW Makefiles"   # Windows MinGW
cmake --build build

# 产物
#   build/chargerlog.exe       — CLI 可执行文件
#   build/libchargerlog_core.a — 静态库
#   build/test_healthd_parser  — 解析器测试
#   build/test_cache_manager   — 缓存测试
```

### 构建选项

- **MinGW**：自动启用 `-static-libgcc -static-libstdc++`，产物零 DLL 依赖。
- **MSVC**：自动添加 `/utf-8` 编译选项，确保中文注释/字符串正确编译。

## 解析器体系（可扩展）

### BaseParser — 抽象基类

所有平台日志解析器继承此类，便于未来扩展新格式（如 `BatteryStatsParser`、`KernelLogParser`）：

| 方法 | 说明 |
|------|------|
| `platformName()` | 返回平台标识字符串 |
| `canParse(line)` | 判断单行是否属于本平台格式 |
| `parseLine(line)` | 解析单行，成功返回 `ChargerDataPoint`，失败返回 `nullopt` |
| `supportedFields()` | 返回该平台支持的字段列表 |

### ParserFactory — 解析器工厂

配置驱动实例化解析器，无需修改 CLI 入口即可支持新平台：

```cpp
auto parser = ParserFactory::create("android_healthd");
if (!parser) { /* 未知平台 */ }
```

新增平台时，在 `parser_factory.cpp` 的 `create()` 中注册即可。

### StatsCalculator — 统计计算（独立模块）

位于 `stats_calculator.h`，不依赖解析器头文件，仅通过 `charger_data_point.h` 共享数据模型。

### HealthdParser — Android healthd 解析器

解析实际 Android `healthd` 日志行：

```
04-06 09:33:59.273  1234  5678 I healthd: battery l=100 v=4099 t=31.2 h=2 st=3 c=-889500 fc=2946000 cc=5 chg=
```

**字段映射**：

| 源字段 | 目标字段 | 单位转换 |
|--------|----------|----------|
| `v` | `battery_voltage_mv` | mV |
| `t` | `battery_temperature_c` | °C |
| `c` | `battery_current_ma` | µA → mA（÷1000） |
| `l` | `battery_level_pct` | % |
| `cc` | `charge_cycle_count` | 次数 |

**过滤规则**：
- `canParse` 要求行内同时出现 `healthd` 和 `battery`，排除 ramoops 中的非电池行（如 `healthd peak:...`）。
- `parseLine` 必须从行首提取 `MM-DD HH:MM:SS.mmm` 格式时间戳，否则返回 `nullopt`，避免产出 `t=0` 的无效数据点。

**时间表示**：原始时间戳解析为**年内毫秒数**（自 Jan 1 00:00:00.000 起的毫秒偏移），前端显示时再格式化为 `MM-DD HH:MM:SS`。

## 数据模型

### ChargerDataPoint

单个时间戳对应的充电数据点：

```cpp
struct ChargerDataPoint {
    int64_t elapsed_ms = 0;              // 年内毫秒数
    std::string timestamp_str;           // 原始时间戳 (MM-DD HH:MM:SS.mmm)

    double battery_current_ma = NAN;     // 电池电流 (mA)
    double battery_voltage_mv = NAN;     // 电池电压 (mV)
    double bus_voltage_mv = NAN;         // VBUS 电压 (mV)
    double bus_current_ma = NAN;         // VBUS 电流 (mA)
    double battery_temperature_c = NAN;  // 电池温度 (°C)
    double battery_level_pct = NAN;      // 电池电量 (%)
    double charge_cycle_count = NAN;     // 充电循环次数

    bool has(const std::string& field) const;
    double get(const std::string& field) const;
    std::vector<std::string> availableFields() const;
};
```

### 统计与阈值结构

```cpp
struct FieldStats {
    std::string field_name;
    double min = NAN, max = NAN, avg = NAN, median = NAN;
    int count = 0;
};

struct ThresholdSegment {
    int64_t start_ms, end_ms;
};

struct ThresholdResult {
    std::string field_name;
    double threshold_value = NAN;
    int64_t total_time_ms = 0;
    int64_t above_time_ms = 0;
    double above_pct = 0.0;
    std::vector<ThresholdSegment> above_segments;
};
```

## 统计计算 (StatsCalculator)

```cpp
class StatsCalculator {
public:
    // 单字段统计 (min/max/avg/median/count)，支持时间范围过滤
    static FieldStats calcField(const vector<ChargerDataPoint>&, const string& field,
                                int64_t start_ms = 0, int64_t end_ms = INT64_MAX);

    // 批量统计多字段
    static vector<FieldStats> calcAllFields(const vector<ChargerDataPoint>&,
                                            const vector<string>& fields,
                                            int64_t start_ms = 0, int64_t end_ms = INT64_MAX);

    // 滑动窗口统计 (未在前端使用，接口已预留)
    static vector<FieldStats> calcWindowedStats(...);

    // 降采样：均匀间距保留最多 target_count 个点，首尾点必保留
    static vector<ChargerDataPoint> downsample(const vector<ChargerDataPoint>&, size_t target_count);

    // 阈值分析：线性插值计算某字段超过阈值的累计时间与各超限时段
    static ThresholdResult calcThreshold(const vector<ChargerDataPoint>&, const string& field,
                                         double threshold,
                                         int64_t start_ms = 0, int64_t end_ms = INT64_MAX);
};
```

**阈值分析算法**：遍历相邻数据点对，通过线性插值精确计算阈值交叉时刻，从而得到 `above_time_ms` 和 `above_segments`。前端 `utils/threshold.ts` 使用同一算法，保证前后端结果一致。

## 缓存管理 (CacheManager)

为避免重复扫描未变更的日志目录，解析结果以 `.chargerlog_cache` 文件缓存在被扫描目录中：

- **指纹算法**：FNV-1a 64-bit，对目录下所有 regular files 的 `(相对路径, mtime, size)` 排序后哈希。
- **格式**：第 1 行为 `V1|<fingerprint_16_hex>`，后续每行一个数据点，pipe (`|`) 分隔字段。
- **原子写入**：先写 `.tmp` 临时文件，再 `rename` 覆盖目标文件。
- **失效条件**：目录指纹不匹配、版本头不正确、格式损坏时自动重新扫描。

```cpp
uint64_t fp = CacheManager::computeFingerprint(dir);
CacheManager::save(dir / ".chargerlog_cache", fp, points);   // 写入
auto opt = CacheManager::load(dir / ".chargerlog_cache", fp); // 读取
```

## 项目配置 (ProjectConfig)

将项目名称映射到平台解析器（当前仅支持 `android_healthd`）：

```cpp
ProjectConfigManager mgr;
mgr.loadFromJson(R"({"230": "android_healthd", "傅里叶": "linux_xxx"})");
string platform = mgr.getPlatform("230");  // "android_healthd"
```

> `ParserFactory` 已实现，CLI 通过 `--platform` 参数由工厂创建对应解析器。新增平台时只需在 `parser_factory.cpp` 中注册，无需修改 CLI 入口。

## CLI 接口

Python 端通过 `subprocess.run()` 调用 `chargerlog`，解析 stdout 中的 JSON。

```bash
# 人类可读输出 (默认平台 android_healthd)
chargerlog /path/to/logs

# 指定平台解析
chargerlog --platform android_healthd /path/to/logs

# JSON 输出（供 Python 解析）
chargerlog --json /path/to/logs

# 含降采样数据点（前端折线图用）
chargerlog --json --points --downsample 500 /path/to/logs

# 跳过缓存强制重新扫描
chargerlog --json --no-cache /path/to/logs

# 时间范围过滤 + 多字段阈值分析
chargerlog --json --start 09:00:00 --end 18:00:00 \
    --threshold battery_voltage_mv=4200 \
    --threshold battery_temperature_c=40 \
    /path/to/logs
```

### `--json` 输出格式

```json
{
  "points_count": 1234,
  "cached": true,
  "time_range": { "start": "04-06 09:00:00", "end": "04-06 15:30:00" },
  "fields": [
    { "name": "battery_voltage_mv", "label": "电池电压", "unit": "mV",
      "count": 1234, "max": 4205.00, "min": 3890.00, "avg": 4050.50, "median": 4048.00 }
  ],
  "thresholds": [
    { "field": "battery_voltage_mv", "value": 4200,
      "total_time_ms": 23400000, "above_time_ms": 3600000, "above_pct": 15.38 }
  ],
  "points": [
    { "t": 123456789, "v": 4099.00, "tmp": 31.20, "cur": -889.50, "lvl": 100.00, "cc": 5.00 }
  ]
}
```

> `points` 数组仅在 `--points` 标志下输出，`t` 为年内毫秒数，字段缩写以节省 JSON 体积。

## 平台适配

### Windows 中文字符路径

`cli_main.cpp` 内联 `path_from_arg()` 函数：通过 Windows API `MultiByteToWideChar(CP_ACP)` 将命令行参数从系统 ANSI 编码（如 GBK）转为 UTF-16 `fs::path`，确保中文路径可正确打开。

### UTF-8 终端输出

Windows 下 `SetConsoleOutputCP(CP_UTF8)` 保证中文统计结果在终端正确显示。

## 测试

```bash
# 在 core/build 目录下
cd core/build

# 解析器测试 (验证 HealthdParser 字段提取、时间戳计算、统计与阈值)
./test_healthd_parser

# 缓存测试 (验证指纹计算、缓存读写、指纹不匹配失效)
./test_cache_manager
```

测试源码位于仓库根目录 `tests/` 下，通过 CMake `add_executable` 引入并链接 `chargerlog_core` 静态库。
