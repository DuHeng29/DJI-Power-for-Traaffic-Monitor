// 本文件定义插件本地配置及使用 Windows DPAPI 保护 pair_key 的持久化接口。
#pragma once
#include <cstdint>
#include <string>

namespace dji_power {
struct PluginConfig {
    std::uint64_t bluetooth_address{};
    std::wstring device_name;
    std::string pair_key;
    bool auto_connect{true};
    bool auto_reconnect{true};
};
std::wstring ModuleDirectory();
PluginConfig LoadConfig();
bool SaveConfig(const PluginConfig& config);
bool IsValidPairKey(const std::string& value);
} // namespace dji_power

