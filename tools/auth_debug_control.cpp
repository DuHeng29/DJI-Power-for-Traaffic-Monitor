// 本文件通过显式调试消息驱动已登录的认证窗口；不会读取手机号、验证码、Cookie 或 Token。
#include "ui/sms_login_dialog.hpp"

#include <cstdio>
#include <iterator>
#include <windows.h>

namespace {
constexpr int kStatusControlId = 308;
}

int wmain() {
    const HWND window = FindWindowW(L"DJIPowerSmsLoginWindowV1", L"DJI 账号短信登录（调试会话）");
    if (!window) {
        std::fwprintf(stderr, L"未找到 DJI 认证调试窗口。\n");
        return 1;
    }

    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(window, dji_power::WM_DJI_AUTH_DEBUG_FETCH, 0, 0,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 30000, &result)) {
        std::fwprintf(stderr, L"认证调试窗口未在限定时间内响应。\n");
        return 2;
    }

    wchar_t status[512]{};
    GetWindowTextW(GetDlgItem(window, kStatusControlId), status,
                   static_cast<int>(std::size(status)));
    std::wprintf(L"%ls\n", status);
    return result == 1 ? 0 : 3;
}
