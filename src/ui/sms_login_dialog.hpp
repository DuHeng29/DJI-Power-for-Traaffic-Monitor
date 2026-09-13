// 本文件声明紧凑的 DJI 短信登录窗口；账号凭据仅在内存中短暂存在。
#pragma once

#include "cloud/pair_key_provider.hpp"
#include <windows.h>
#include <vector>

namespace dji_power {
inline constexpr UINT WM_DJI_AUTH_DEBUG_FETCH = WM_APP + 32;
std::vector<CloudDevice> ShowSmsLoginDialog(HWND parent, bool debug_mode = false);
} // namespace dji_power
