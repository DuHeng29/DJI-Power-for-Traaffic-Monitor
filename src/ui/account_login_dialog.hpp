// 本文件声明紧凑的 DJI Home 账号登录窗口；敏感信息仅在内存中短暂存在。
#pragma once

#include "cloud/pair_key_provider.hpp"
#include <windows.h>
#include <vector>

namespace dji_power {
std::vector<CloudDevice> ShowAccountLoginDialog(HWND parent);
} // namespace dji_power
