// 本文件验证生成的 DLL 可加载、导出正确且能枚举五个 TrafficMonitor 项目。
#include "PluginInterface.h"
#include <windows.h>
#include <cassert>
#include <string>

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    const auto module = LoadLibraryW(argv[1]);
    assert(module != nullptr);
    const auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    assert(entry != nullptr);
    const auto plugin = entry();
    assert(plugin != nullptr && plugin->GetAPIVersion() == 8);
    assert(std::wstring(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)) == L"DuHeng29");
    assert(std::wstring(plugin->GetInfo(ITMPlugin::TMI_COPYRIGHT)) ==
           L"Copyright (c) 2026 DuHeng29");
    assert(std::wstring(plugin->GetInfo(ITMPlugin::TMI_URL)) ==
           L"https://github.com/DuHeng29/DJI-Power-for-Traaffic-Monitor");
    for (int index = 0; index < 5; ++index) assert(plugin->GetItem(index) != nullptr);
    assert(plugin->GetItem(5) == nullptr);
    FreeLibrary(module);
    return 0;
}

