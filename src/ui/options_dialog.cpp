// 本文件以纯 Win32 控件实现设备扫描、Pair Key、连接测试和 Key 获取界面。
#include "ui/options_dialog.hpp"
#include "ble/ble_manager.hpp"
#include "cloud/pair_key_provider.hpp"
#include "common/config.hpp"
#include "protocol/duml.hpp"
#include <commctrl.h>
#include <string>
#include <vector>

extern HMODULE g_module;

namespace dji_power {
namespace {
enum : int { IDC_DEVICE=101, IDC_SCAN, IDC_PAIR_KEY, IDC_GET_KEY, IDC_TOKEN, IDC_AUTO_CONNECT, IDC_AUTO_RECONNECT, IDC_STATUS, IDC_VALUES, IDC_TEST };

class OptionsWindow {
public:
    explicit OptionsWindow(HWND parent) : parent_(parent), original_(LoadConfig()), editing_(original_) {}
    bool Show() {
        WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = g_module; wc.lpszClassName = L"DJIPowerOptionsWindow"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"DJI Power for TrafficMonitor", WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 590, 430, parent_, nullptr, g_module, this);
        if (!hwnd_) return false;
        RECT rect{}; GetWindowRect(hwnd_, &rect); const int width = rect.right-rect.left, height=rect.bottom-rect.top;
        SetWindowPos(hwnd_, nullptr, (GetSystemMetrics(SM_CXSCREEN)-width)/2, (GetSystemMetrics(SM_CYSCREEN)-height)/2, 0,0, SWP_NOSIZE|SWP_NOZORDER);
        EnableWindow(parent_, FALSE);
        MSG message{};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(hwnd_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
        EnableWindow(parent_, TRUE); SetForegroundWindow(parent_);
        return changed_;
    }
private:
    HWND parent_{}, hwnd_{}, device_{}, pair_{}, token_{}, status_{}, values_{};
    PluginConfig original_, editing_;
    std::vector<DiscoveredDevice> devices_;
    bool changed_{}, accepted_{};

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OptionsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<OptionsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = hwnd; }
        return self ? self->Handle(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }
    HWND Add(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id=0) {
        return CreateWindowExW(0, type, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_module,nullptr);
    }
    void Build() {
        Add(L"STATIC", L"连接方式：  ● 本地 BLE    ○ DJI Cloud（备用模式，暂未启用）", 0, 22,18,530,22);
        Add(L"STATIC", L"设备", 0,22,60,70,22);
        device_ = Add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,100,56,350,150,IDC_DEVICE);
        Add(L"BUTTON", L"扫描", BS_PUSHBUTTON|WS_TABSTOP,465,55,80,27,IDC_SCAN);
        Add(L"STATIC", L"Pair Key", 0,22,103,70,22);
        pair_ = Add(L"EDIT", L"", WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,100,99,350,25,IDC_PAIR_KEY);
        SendMessageW(pair_, EM_SETPASSWORDCHAR, L'●', 0);
        Add(L"BUTTON", L"获取 Key", BS_PUSHBUTTON|WS_TABSTOP,465,98,80,27,IDC_GET_KEY);
        Add(L"STATIC", L"Member Token（仅获取 Key 时使用，不保存）", 0,22,143,280,22);
        token_ = Add(L"EDIT", L"", WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,22,167,523,25,IDC_TOKEN);
        const auto ac = Add(L"BUTTON", L"自动连接", BS_AUTOCHECKBOX|WS_TABSTOP,22,210,130,24,IDC_AUTO_CONNECT);
        const auto ar = Add(L"BUTTON", L"断线自动重连", BS_AUTOCHECKBOX|WS_TABSTOP,170,210,160,24,IDC_AUTO_RECONNECT);
        SendMessageW(ac, BM_SETCHECK, editing_.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(ar, BM_SETCHECK, editing_.auto_reconnect ? BST_CHECKED : BST_UNCHECKED, 0);
        status_ = Add(L"STATIC", L"状态：未连接", 0,22,252,523,22,IDC_STATUS);
        values_ = Add(L"STATIC", L"输入：-- W    输出：-- W    电量：-- %", 0,22,279,523,22,IDC_VALUES);
        Add(L"BUTTON", L"测试连接", BS_PUSHBUTTON|WS_TABSTOP,272,335,95,30,IDC_TEST);
        Add(L"BUTTON", L"确定", BS_DEFPUSHBUTTON|WS_TABSTOP,375,335,80,30,IDOK);
        Add(L"BUTTON", L"取消", BS_PUSHBUTTON|WS_TABSTOP,465,335,80,30,IDCANCEL);
        SetWindowTextA(pair_, editing_.pair_key.c_str());
        RefreshDevices(); SetTimer(hwnd_, 1, 500, nullptr);
    }
    void RefreshDevices() {
        const auto selected_address = editing_.bluetooth_address;
        devices_ = BleManager::Instance().Devices();
        SendMessageW(device_, CB_RESETCONTENT, 0, 0);
        int selected = -1;
        for (std::size_t i=0; i<devices_.size(); ++i) {
            wchar_t address[20]{}; swprintf_s(address,L" [%012llX]",static_cast<unsigned long long>(devices_[i].address));
            const auto label = devices_[i].name + address;
            SendMessageW(device_, CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            if (devices_[i].address == selected_address) selected = static_cast<int>(i);
        }
        if (selected < 0 && !devices_.empty()) selected = 0;
        SendMessageW(device_, CB_SETCURSEL, selected, 0);
    }
    bool ReadForm(bool show_error) {
        char key[128]{}; GetWindowTextA(pair_, key, static_cast<int>(std::size(key)));
        try { editing_.pair_key = duml::NormalizePairKey(key); }
        catch (...) { if (show_error) MessageBoxW(hwnd_, L"Pair Key 必须是 32 个十六进制字符。", L"DJI Power", MB_ICONWARNING); return false; }
        const auto index = static_cast<int>(SendMessageW(device_, CB_GETCURSEL,0,0));
        if (index >= 0 && static_cast<std::size_t>(index) < devices_.size()) { editing_.bluetooth_address=devices_[index].address; editing_.device_name=devices_[index].name; }
        editing_.auto_connect = SendDlgItemMessageW(hwnd_,IDC_AUTO_CONNECT,BM_GETCHECK,0,0)==BST_CHECKED;
        editing_.auto_reconnect = SendDlgItemMessageW(hwnd_,IDC_AUTO_RECONNECT,BM_GETCHECK,0,0)==BST_CHECKED;
        return true;
    }
    void FetchKey() {
        wchar_t token[2048]{}; GetWindowTextW(token_,token,static_cast<int>(std::size(token)));
        SetWindowTextW(status_,L"状态：正在从 DJI 获取设备 Key…"); UpdateWindow(hwnd_);
        std::wstring error;
        const auto cloud_devices = DefaultPairKeyProvider().FetchWithMemberToken(token,error);
        SecureZeroMemory(token,sizeof(token)); SetWindowTextW(token_,L"");
        if (cloud_devices.empty()) { MessageBoxW(hwnd_,error.c_str(),L"获取 Key 失败",MB_ICONERROR); return; }
        SetWindowTextA(pair_,cloud_devices.front().pair_key.c_str());
        editing_.device_name=cloud_devices.front().name;
        std::wstring message=L"已获取 "+std::to_wstring(cloud_devices.size())+L" 台设备的信息，当前填入："+cloud_devices.front().name;
        MessageBoxW(hwnd_,message.c_str(),L"获取 Key 成功",MB_ICONINFORMATION);
    }
    void UpdateStatus() {
        const auto value=BleManager::Instance().Snapshot();
        SetWindowTextW(status_,(L"状态："+value.status).c_str());
        const auto field=[](int v){return v<0?std::wstring(L"--"):std::to_wstring(v);};
        const auto text=L"输入："+field(value.input_w)+L" W    输出："+field(value.output_w)+L" W    电量："+field(value.battery_percent)+L" %";
        SetWindowTextW(values_,text.c_str());
    }
    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        switch(message) {
        case WM_CREATE: Build(); return 0;
        case WM_TIMER: UpdateStatus(); return 0;
        case WM_COMMAND:
            switch(LOWORD(wp)) {
            case IDC_SCAN: RefreshDevices(); return 0;
            case IDC_GET_KEY: FetchKey(); return 0;
            case IDC_TEST: if(ReadForm(true)){auto test=editing_;test.auto_connect=true;BleManager::Instance().Reconfigure(test);BleManager::Instance().ConnectNow();} return 0;
            case IDOK: if(ReadForm(true)){changed_=SaveConfig(editing_);accepted_=true;BleManager::Instance().Reconfigure(editing_);DestroyWindow(hwnd_);} return 0;
            case IDCANCEL: BleManager::Instance().Reconfigure(original_); DestroyWindow(hwnd_); return 0;
            }
            break;
        case WM_CLOSE: if(!accepted_) BleManager::Instance().Reconfigure(original_); DestroyWindow(hwnd_); return 0;
        case WM_DESTROY: KillTimer(hwnd_,1); return 0;
        }
        return DefWindowProcW(hwnd_,message,wp,lp);
    }
};
} // namespace

bool ShowOptionsWindow(HWND parent) { OptionsWindow window(parent); return window.Show(); }
} // namespace dji_power

