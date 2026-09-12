// 本文件提供人工运行的 BLE 扫描诊断程序，复用插件实际使用的 BleManager。
#include "ble/ble_manager.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

HMODULE g_module = nullptr;

int wmain() {
    g_module = GetModuleHandleW(nullptr);
    auto& manager = dji_power::BleManager::Instance();
    manager.Start();
    std::wcout << L"正在扫描 DJI Power（12 秒）……\n";
    std::this_thread::sleep_for(std::chrono::seconds(12));
    const auto devices = manager.Devices();
    manager.Stop();
    for (const auto& device : devices) {
        std::wcout << device.name << L"  地址=" << std::hex << device.address
                   << L"  RSSI=" << std::dec << device.rssi << L" dBm\n";
    }
    std::wcout << L"共发现 " << devices.size() << L" 台 DJI Power。\n";
    return devices.empty() ? 2 : 0;
}

