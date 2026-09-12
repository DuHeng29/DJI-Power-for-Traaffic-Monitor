// 本文件验证生成的 DLL 可加载、导出正确且能枚举五个 TrafficMonitor 项目。
#include "PluginInterface.h"
#include <windows.h>
#include <cassert>

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    const auto module = LoadLibraryW(argv[1]);
    assert(module != nullptr);
    const auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    assert(entry != nullptr);
    const auto plugin = entry();
    assert(plugin != nullptr && plugin->GetAPIVersion() == 8);
    for (int index = 0; index < 5; ++index) assert(plugin->GetItem(index) != nullptr);
    assert(plugin->GetItem(5) == nullptr);
    FreeLibrary(module);
    return 0;
}

