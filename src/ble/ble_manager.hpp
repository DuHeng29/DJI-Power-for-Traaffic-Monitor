// 本文件定义单 DLL 内部的 Windows BLE 扫描、鉴权、重连管理器。
#pragma once
#include "common/config.hpp"
#include "common/telemetry_store.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dji_power {
struct DiscoveredDevice {
    std::uint64_t address{};
    std::wstring name;
    short rssi{};
};

class BleManager {
public:
    static BleManager& Instance();
    void Start();
    void Stop();
    void Reconfigure(const PluginConfig& config);
    void Rescan();
    void ConnectNow();
    std::vector<DiscoveredDevice> Devices() const;
    TelemetrySnapshot Snapshot() const;
private:
    BleManager();
    ~BleManager();
    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dji_power

