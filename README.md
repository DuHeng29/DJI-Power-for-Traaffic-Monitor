# DJI Power for TrafficMonitor

原生 Windows 单 DLL TrafficMonitor 插件，通过本地 BLE 读取 DJI Power 遥测数据。

```text
TrafficMonitor -> DJIPowerPlugin.dll -> Windows BLE -> DJI Power
```

## 已实现

- 无过滤扫描后综合设备名、DJI 厂商数据、Service UUID 和已保存地址识别电源
- 手工填写 32 字符十六进制 `pair_key`
- `0x5A/0x6A` 两步挑战鉴权
- 长连接接收约 1 Hz 的 `0x5A/0x61` 遥测推送
- BLE 断线检测、自动重连和指数退避
- 原生 Win32 配置与测试连接窗口
- 电量、输入、输出、净功率、剩余时间五个 TrafficMonitor 项目
- 使用一次性 DJI member token 获取设备 `pair_key`
- 配置保存在 DLL 同目录，`pair_key` 使用当前 Windows 用户 DPAPI 加密

> 本项目是非官方社区项目，与 DJI 无隶属或认可关系。当前版本只发送鉴权和读取请求，不实现电源控制。

## 构建

需要 Visual Studio、Windows 10/11 SDK 和 CMake 3.24+。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

若未设置 `TRAFFICMONITOR_SDK_DIR`，CMake 会从 TrafficMonitor 官方仓库下载当前 `PluginInterface.h`。发布构建建议传入固定 SDK 目录：

```powershell
cmake -S . -B build -A x64 -DTRAFFICMONITOR_SDK_DIR=C:\src\TrafficMonitor\include
```

构建产物：

```text
build/bin/Release/DJIPowerPlugin.dll
```

将 DLL 放入：

```text
TrafficMonitor/plugins/DJIPowerPlugin/DJIPowerPlugin.dll
```

## Pair Key

配置页提供两种方式：

1. 粘贴已有 `pair_key`。
2. 粘贴一次性 DJI `US_...` member token，点击“获取 Key”。

member token 只在请求期间驻留内存，不会写入配置。账号密码、验证码以及逆向登录签名没有内置；这样即使 DJI 登录接口变化，已有 `pair_key` 的本地 BLE 用户仍可继续使用。

## 验证状态

- MSVC Release 构建通过
- DUML 编解码、分包和遥测解析测试通过
- DLL 加载、`TMPluginGetInstance` 导出及五个项目枚举测试通过
- 实机 BLE、型号差异和长期重连仍需连接真实 DJI Power 验证

可使用与插件共用同一 BLE 管理器的诊断程序验证扫描：

```powershell
.\build\bin\Release\ble_scan_probe.exe
```

## 许可边界

- TrafficMonitor 插件接口从官方项目取得，并按其许可证使用。
- BLE/DUML 行为依据公开协议事实独立实现。
- `ha-dji-power-ble` 当前未声明开源许可证，本仓库不复制或派生其源码。

