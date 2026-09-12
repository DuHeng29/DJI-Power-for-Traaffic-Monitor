// 本文件声明紧凑的 DJI 短信登录窗口；账号凭据仅在内存中短暂存在。
#pragma once

#include "cloud/pair_key_provider.hpp"
#include <windows.h>
#include <vector>

namespace dji_power {
std::vector<CloudDevice> ShowSmsLoginDialog(HWND parent);
} // namespace dji_power
