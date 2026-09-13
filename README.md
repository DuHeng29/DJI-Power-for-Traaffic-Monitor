# DJI Power for TrafficMonitor

一个面向 Windows 的原生 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 插件，通过本地蓝牙低功耗（BLE）读取 DJI Power 户外电源的实时状态。

插件以单个 DLL 运行，不依赖 Python、Home Assistant 或额外后台进程。DJI 账号仅用于首次获取设备的本地 Pair Key；配置完成后，日常监控不需要登录账号，也不依赖 DJI 云端。

```text
TrafficMonitor → DJIPowerPlugin.dll → Windows BLE → DJI Power
```

> [!IMPORTANT]
> 本项目是非官方社区项目，与 DJI 无隶属、合作或认可关系。项目使用逆向分析得到的非公开协议，设备固件或 DJI 账号接口更新可能导致部分功能失效。

## 功能

- 扫描附近的 DJI Power，结合广播名称、DJI 厂商数据、Service UUID 和已保存地址识别设备
- 使用 32 位十六进制 Pair Key 完成 `0x5A/0x6A` BLE 挑战鉴权
- 保持本地 BLE 长连接，接收约 1 Hz 的 `0x5A/0x61` 遥测推送
- 断线检测、自动重连和指数退避
- 向 TrafficMonitor 提供电量、输入功率、输出功率、净功率和剩余时间五个项目
- 支持手工粘贴 Pair Key
- 支持 DJI Home 账号、密码、图片验证码和可选二次验证码登录，一次性获取 Pair Key
- 兼容 `US_`、`CN_` 及其他合法大写区域前缀的 DJI Home Member Token
- 登录后始终显示独立设备选择窗口；只有一台设备时也需要确认
- 多台绑定设备可按名称和序列号选择
- Pair Key 使用当前 Windows 用户的 DPAPI 加密保存
- 原生 Win32 设置界面，支持 DPI 和 ClearType

## 支持的设备

| 型号 | 广播代码 | 状态 |
| --- | ---: | --- |
| DJI Power 1000 | `0x91` | 已实现识别，仍需更多实机验证 |
| DJI Power 1000 V2 | `0x97` | 已实现识别，仍需更多实机验证 |
| DJI Power 1000 Mini | `0x98` | 已实现识别，仍需更多实机验证 |
| DJI Power 2000 | `0x94` | 已实现识别，仍需更多实机验证 |

不同型号或固件版本的遥测字段可能存在差异。提交 Issue 时请说明型号、固件版本和脱敏后的现象。

## 当前限制

- 一次只连接和监控一台 DJI Power；账号多设备功能用于选择绑定对象，不代表同时监控多台
- 只实现鉴权和只读遥测，不提供电源控制功能
- DJI Power 通常只允许一个 BLE Central 同时连接；插件连接期间，手机应用可能无法连接同一设备
- DJI Home 登录接口不是公开 API，可能随时发生变化
- 已实测真实账号登录和 Pair Key 获取；长期 BLE 稳定性仍需更多设备验证

## 系统要求

- Windows 10 或 Windows 11
- 可用的 BLE 适配器
- 支持插件的 TrafficMonitor（插件机制自 1.82 起提供）
- 插件架构与 TrafficMonitor 一致，例如 x64 对应 x64

## 安装

1. 获取或自行构建 `DJIPowerPlugin.dll`。
2. 创建以下目录并放入 DLL：

   ```text
   TrafficMonitor/
   └─ plugins/
      └─ DJIPowerPlugin/
         └─ DJIPowerPlugin.dll
   ```

3. 重启 TrafficMonitor，在插件管理中确认插件已加载。
4. 打开插件设置，扫描并配置设备。

## 配置 Pair Key

### 使用 DJI Home 账号

1. 在插件设置页点击“账号登录”。
2. 输入账号、密码和图片验证码；需要时填写二次验证码。
3. 登录成功后，在独立窗口中确认设备。即使只有一台，该步骤也不会跳过。
4. Pair Key 回填后，选择扫描到的 BLE 设备并点击“测试连接”。
5. 测试成功后点击“确定”保存。

账号登录只用于读取 Pair Key。账号、密码、验证码、验证码票据和 Member Token 不会写入配置文件，相关内存副本会在使用后主动清除。

### 手工配置

如果已有 Pair Key，可直接粘贴 32 位十六进制字符串，再选择 BLE 设备并测试连接。

## 隐私与安全

- 日常遥测只通过本地 BLE 传输，本项目没有服务器
- 不包含遥测收集、崩溃上报或用户统计
- 账号登录直接连接 DJI 账号及 DJI Home 服务
- 只持久化设备名称、蓝牙地址、连接选项和经 DPAPI 加密的 Pair Key
- 不要在 Issue、日志或截图中公开账号、密码、Member Token、Pair Key 或完整序列号

## 从源码构建

需要 Visual Studio 2022 或更新版本（安装“使用 C++ 的桌面开发”）、Windows 10/11 SDK 和 CMake 3.24+。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

产物位于 `build/bin/Release/DJIPowerPlugin.dll`。CMake 默认从 TrafficMonitor 官方仓库下载 `PluginInterface.h`；也可以指定固定 SDK：

```powershell
cmake -S . -B build -A x64 `
  -DTRAFFICMONITOR_SDK_DIR=C:\src\TrafficMonitor\include
```

## 测试与诊断

自动化测试覆盖 DUML 帧与 CRC、遥测解析、DJI Home 移动端签名、区域 Token、多设备列表、DLL 导出和五个监控项目。

```powershell
# 扫描附近设备 12 秒
.\build\bin\Release\ble_scan_probe.exe

# 只获取图片验证码，不提交账号或密码
.\build\bin\Release\cloud_login_probe.exe

# 与插件使用相同登录及设备选择流程
.\build\bin\Release\DJI认证调试器.exe
```

## 项目结构

```text
src/
├─ ble/       Windows BLE 扫描、鉴权、遥测与重连
├─ cloud/     DJI Home 登录及 Pair Key 查询
├─ common/    配置、DPAPI 和遥测快照
├─ plugin/    TrafficMonitor 插件接口与监控项目
├─ protocol/  DUML 帧、CRC 和遥测解析
└─ ui/        Win32 设置、登录和设备选择窗口
tests/        自动化测试
tools/        独立认证调试工具
```

## 参与贡献

欢迎提交 Issue 和 Pull Request。提交前请确保 Release 构建及全部测试通过，不包含真实账号、Token、Pair Key 或设备序列号；新增协议字段时请说明设备型号、固件版本和验证方式，并在重要实现处添加必要的中文注释。

## 致谢

- [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)：提供插件宿主、插件接口和开发文档，使 DJI Power 数据能够显示在任务栏与主窗口中。
- [ha-dji-power-ble](https://github.com/zuyan9/ha-dji-power-ble)：为 DJI Power 本地 BLE、DUML 通信、设备识别和通过 DJI Home 一次性获取 Pair Key 提供了重要灵感及协议验证参考。

感谢以上项目的维护者和贡献者。本仓库不打包参考项目源码；第三方项目、接口文件和依赖继续适用其各自许可证。

## 许可证

本项目以 [MIT License](LICENSE) 开源。

DJI、DJI Power 和 DJI Home 是其各自权利人的商标。本许可证只适用于本仓库原创代码，不授予任何 DJI 固件、服务、商标或其他第三方材料的权利。
