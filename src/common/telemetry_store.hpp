// 本文件定义跨 BLE 工作线程与 TrafficMonitor UI 线程共享的遥测快照。
#pragma once
#include <chrono>
#include <mutex>
#include <string>

namespace dji_power {
enum class ConnectionState { stopped, scanning, connecting, authenticating, connected, error };

struct TelemetrySnapshot {
    int battery_percent{-1};
    int input_w{-1};
    int output_w{-1};
    int runtime_min{-1};
    ConnectionState connection{ConnectionState::stopped};
    std::wstring device_name;
    std::wstring status{L"未启动"};
    std::chrono::steady_clock::time_point updated_at{};
};

class TelemetryStore {
public:
    TelemetrySnapshot Get() const;
    void Update(const TelemetrySnapshot& snapshot);
    void SetStatus(ConnectionState state, std::wstring status);
private:
    mutable std::mutex mutex_;
    TelemetrySnapshot snapshot_;
};
} // namespace dji_power

