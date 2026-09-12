// 本文件保存 DLL 模块句柄；DllMain 中不启动线程或执行 BLE 操作。
#include <windows.h>

HMODULE g_module = nullptr;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

