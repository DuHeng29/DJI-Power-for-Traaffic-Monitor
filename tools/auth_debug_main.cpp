// 本文件提供独立的 DJI 账号调试进程；窗口存活期间登录会话仅保存在进程内存中。
#include "ui/sms_login_dialog.hpp"

#include <commctrl.h>
#include <windows.h>

HMODULE g_module{};

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_module = instance;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    // 登录成功后对话框不会退出，而是保留会话并启用单独的“获取 Key”按钮。
    const auto devices = dji_power::ShowSmsLoginDialog(nullptr, true);
    if (!devices.empty()) {
        const auto message = L"已取得 " + std::to_wstring(devices.size()) + L" 台 DJI Power 的 Key。";
        MessageBoxW(nullptr, message.c_str(), L"DJI 认证调试", MB_ICONINFORMATION);
    }
    return 0;
}
