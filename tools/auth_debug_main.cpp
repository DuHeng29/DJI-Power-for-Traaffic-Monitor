// 本文件提供独立的 DJI Home 账号登录调试进程，便于真实账号验证 Pair Key 流程。
#include "ui/account_login_dialog.hpp"

#include <commctrl.h>
#include <windows.h>

HMODULE g_module{};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_module = instance;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    // 调试器与插件调用同一实现，成功时仅显示设备数量，不输出任何凭据。
    const auto devices = dji_power::ShowAccountLoginDialog(nullptr);
    if (!devices.empty()) {
        const auto message = L"已取得 " + std::to_wstring(devices.size()) + L" 台 DJI Power 的 Key。";
        MessageBoxW(nullptr, message.c_str(), L"DJI 认证调试", MB_ICONINFORMATION);
    }
    return 0;
}
