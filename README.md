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
- 使用 DJI 账号图片验证码与短信验证码登录，自动获取设备 `pair_key`
- 登录会话、手机号、验证码和临时 member token 仅驻留内存，不写入配置
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
2. 点击“短信登录”，依次填写国家/地区代码、手机号、图片验证码和短信验证码；登录成功后自动填写账号中第一台支持设备的 `pair_key`。

短信只会在用户明确点击“发送验证码”后发送。账号输入、验证码、Cookie 和临时 member token 只在登录窗口生命周期内驻留内存，窗口关闭时会主动清除；插件只持久化经 DPAPI 加密的 `pair_key`。

当前登录流程调用 DJI 官方账号网页和 DJI Home 使用的非公开接口。DJI 若调整接口，自动获取功能可能需要随之更新，但已保存 `pair_key` 的本地 BLE 连接不受影响。手机号尚未注册时，DJI 当前流程可能同时完成账号注册；操作前请阅读并同意登录窗口中的 DJI 账号条款提示。

## 验证状态

- MSVC Release 构建通过
- DUML 编解码、分包和遥测解析测试通过
- DLL 加载、`TMPluginGetInstance` 导出及五个项目枚举测试通过
- 云端设备列表、member token 和错误返回解析测试通过
- 真实短信发送、实机 BLE、型号差异和长期重连仍需连接真实账号及 DJI Power 验证

可使用与插件共用同一 BLE 管理器的诊断程序验证扫描：

```powershell
.\build\bin\Release\ble_scan_probe.exe
```

也可单独验证登录页、会话初始化和图片验证码下载；该命令不会发送短信：

```powershell
.\build\bin\Release\cloud_login_probe.exe
```

## 许可边界

- TrafficMonitor 插件接口从官方项目取得，并按其许可证使用。
- BLE/DUML 行为依据公开协议事实独立实现。
- `ha-dji-power-ble` 当前未声明开源许可证，本仓库不复制或派生其源码。

